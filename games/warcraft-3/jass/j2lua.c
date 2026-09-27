#include "jass.h"

#include <stdarg.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} luaWriter_t;

static bool lua_reserve(luaWriter_t *writer, size_t extra) {
    size_t capacity;
    char *data;

    if (writer->failed || extra > SIZE_MAX - writer->length - 1) {
        writer->failed = true;
        return false;
    }
    if (writer->length + extra + 1 <= writer->capacity) return true;
    capacity = writer->capacity ? writer->capacity : 4096;
    while (capacity < writer->length + extra + 1) {
        if (capacity > SIZE_MAX / 2) {
            writer->failed = true;
            return false;
        }
        capacity *= 2;
    }
    data = realloc(writer->data, capacity);
    if (!data) {
        writer->failed = true;
        return false;
    }
    writer->data = data;
    writer->capacity = capacity;
    return true;
}

static void lua_appendn(luaWriter_t *writer, cstring_t text, size_t length) {
    if (!text || !lua_reserve(writer, length)) return;
    memcpy(writer->data + writer->length, text, length);
    writer->length += length;
    writer->data[writer->length] = '\0';
}

static void lua_append(luaWriter_t *writer, cstring_t text) {
    if (text) lua_appendn(writer, text, strlen(text));
}

static void lua_printf(luaWriter_t *writer, cstring_t format, ...) {
    va_list args;
    va_list copy;
    int length;

    va_start(args, format);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0 || !lua_reserve(writer, (size_t)length)) {
        writer->failed = true;
        va_end(args);
        return;
    }
    vsnprintf(writer->data + writer->length, writer->capacity - writer->length, format, args);
    writer->length += (size_t)length;
    va_end(args);
}

static void lua_indent(luaWriter_t *writer, uint32_t depth) {
    while (depth--) lua_append(writer, "    ");
}

static void lua_identifier(luaWriter_t *writer, cstring_t name) {
    static cstring_t const keywords[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
        "true", "until", "while",
    };
    for (uint32_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (!strcmp(name, keywords[i])) {
            lua_printf(writer, "%s_jass", name);
            return;
        }
    }
    lua_append(writer, name);
}

static void lua_string(luaWriter_t *writer, cstring_t text) {
    lua_append(writer, "\"");
    for (cstring_t p = text; p && *p; ++p) {
        switch (*p) {
            case '\\': lua_append(writer, "\\\\"); break;
            case '"': lua_append(writer, "\\\""); break;
            case '\n': lua_append(writer, "\\n"); break;
            case '\r': lua_append(writer, "\\r"); break;
            case '\t': lua_append(writer, "\\t"); break;
            default: lua_appendn(writer, p, 1); break;
        }
    }
    lua_append(writer, "\"");
}

static cstring_t lua_default(cstring_t type) {
    if (!type) return "nil";
    if (!strcmp(type, "integer")) return "0";
    if (!strcmp(type, "real")) return "0.0";
    if (!strcmp(type, "boolean")) return "false";
    if (!strcmp(type, "string")) return "\"\"";
    return "nil";
}

static bool lua_expression(luaWriter_t *writer, token_t const *token);

static bool lua_expression_list(luaWriter_t *writer, token_t const *tokens) {
    bool first = true;
    FOR_EACH_LIST(token_t const, token, tokens) {
        if (!first) lua_append(writer, ", ");
        if (!lua_expression(writer, token)) return false;
        first = false;
    }
    return true;
}

static bool lua_function_args(luaWriter_t *writer, token_t const *args) {
    bool first = true;
    FOR_EACH_LIST(token_t const, arg, args) {
        if (!first) lua_append(writer, ", ");
        lua_identifier(writer, arg->secondary);
        first = false;
    }
    return true;
}

static bool lua_array_access(luaWriter_t *writer, token_t const *token) {
    token_t const *access = token;
    lua_identifier(writer, token->primary);
    while (access) {
        if (!access->index) {
            fprintf(stderr, "JASS-to-Lua: malformed array access '%s'\n", token->primary ? token->primary : "(unnamed)");
            return false;
        }
        lua_append(writer, "[");
        if (!lua_expression(writer, access->index)) return false;
        lua_append(writer, "]");
        access = access->body;
    }
    return true;
}

static bool lua_expression(luaWriter_t *writer, token_t const *token) {
    if (!token) {
        fprintf(stderr, "JASS-to-Lua: missing expression\n");
        return false;
    }
    switch (token->type) {
        case TT_INTEGER:
            if (token->primary[0] == '$')
                lua_printf(writer, "%ld", strtol(token->primary + 1, NULL, 16));
            else
                lua_append(writer, token->primary);
            return true;
        case TT_REAL:
            lua_append(writer, token->primary);
            return true;
        case TT_STRING:
            lua_string(writer, token->primary);
            return true;
        case TT_FOURCC:
            lua_append(writer, "FourCC(");
            lua_string(writer, token->primary);
            lua_append(writer, ")");
            return true;
        case TT_BOOLEAN:
            lua_append(writer, token->primary);
            return true;
        case TT_IDENTIFIER:
            if (!strcmp(token->primary, "null")) lua_append(writer, "nil");
            else lua_identifier(writer, token->primary);
            return true;
        case TT_ARRAYACCESS:
            return lua_array_access(writer, token);
        case TT_CALL:
            lua_identifier(writer, token->primary);
            lua_append(writer, "(");
            if (!lua_expression_list(writer, token->args)) return false;
            lua_append(writer, ")");
            return true;
        default:
            fprintf(stderr, "JASS-to-Lua: unsupported expression node %d\n", token->type);
            return false;
    }
}

static bool lua_statement(luaWriter_t *writer, token_t const *token, uint32_t depth, bool in_loop);
static bool lua_statement_impl(luaWriter_t *writer, token_t const *token, uint32_t depth, bool in_loop);

static bool lua_statements(luaWriter_t *writer, token_t const *tokens, uint32_t depth, bool in_loop) {
    FOR_EACH_LIST(token_t const, token, tokens) {
        if (!lua_statement(writer, token, depth, in_loop)) return false;
    }
    return true;
}

static bool lua_if(luaWriter_t *writer, token_t const *token, uint32_t depth, bool in_loop) {
    token_t const *branch = token;
    bool first = true;
    while (branch) {
        lua_indent(writer, depth);
        if (first) {
            lua_append(writer, "if ");
            if (!lua_expression(writer, branch->condition)) return false;
            lua_append(writer, " then\n");
        } else if (branch->condition) {
            lua_append(writer, "elseif ");
            if (!lua_expression(writer, branch->condition)) return false;
            lua_append(writer, " then\n");
        } else {
            lua_append(writer, "else\n");
        }
        if (!lua_statements(writer, branch->body, depth + 1, in_loop)) return false;
        branch = branch->elseblock;
        first = false;
    }
    lua_indent(writer, depth);
    lua_append(writer, "end\n");
    return true;
}

static bool lua_statement(luaWriter_t *writer, token_t const *token, uint32_t depth, bool in_loop) {
    if (token->flags & TF_DEBUG) {
        lua_indent(writer, depth);
        lua_append(writer, "if _DEBUG then\n");
        if (!lua_statement_impl(writer, token, depth + 1, in_loop)) return false;
        lua_indent(writer, depth);
        lua_append(writer, "end\n");
        return true;
    }
    return lua_statement_impl(writer, token, depth, in_loop);
}

static bool lua_statement_impl(luaWriter_t *writer, token_t const *token, uint32_t depth, bool in_loop) {
    lua_indent(writer, depth);
    switch (token->type) {
        case TT_SET:
            lua_identifier(writer, token->secondary);
            if (token->index) {
                lua_append(writer, "[");
                if (!lua_expression(writer, token->index)) return false;
                lua_append(writer, "]");
            }
            lua_append(writer, " = ");
            if (!lua_expression(writer, token->init)) return false;
            lua_append(writer, "\n");
            return true;
        case TT_VARDECL:
            lua_append(writer, "local ");
            lua_identifier(writer, token->secondary);
            lua_append(writer, " = ");
            if (token->flags & TF_ARRAY) {
                lua_printf(writer, "__jarray(%s)", lua_default(token->primary));
            } else if (token->init) {
                if (!lua_expression(writer, token->init)) return false;
            } else {
                lua_append(writer, lua_default(token->primary));
            }
            lua_append(writer, "\n");
            return true;
        case TT_IF:
            return lua_if(writer, token, depth, in_loop);
        case TT_LOOP:
            lua_append(writer, "while true do\n");
            if (!lua_statements(writer, token->body, depth + 1, true)) return false;
            lua_indent(writer, depth);
            lua_append(writer, "end\n");
            return true;
        case TT_EXITWHEN:
            if (!in_loop) {
                fprintf(stderr, "JASS-to-Lua: exitwhen outside loop\n");
                return false;
            }
            lua_append(writer, "if ");
            if (!lua_expression(writer, token->condition)) return false;
            lua_append(writer, " then break end\n");
            return true;
        case TT_RETURN:
            lua_append(writer, "return");
            if (token->body) {
                lua_append(writer, " ");
                if (!lua_expression_list(writer, token->body)) return false;
            }
            lua_append(writer, "\n");
            return true;
        case TT_CALL:
        case TT_IDENTIFIER:
        case TT_ARRAYACCESS:
            if (!lua_expression(writer, token)) return false;
            lua_append(writer, "\n");
            return true;
        default:
            fprintf(stderr, "JASS-to-Lua: unsupported statement node %d\n", token->type);
            return false;
    }
}

static bool lua_function(luaWriter_t *writer, token_t const *token) {
    if (token->flags & TF_NATIVE) return true;
    lua_append(writer, "function ");
    lua_identifier(writer, token->primary);
    lua_append(writer, "(");
    if (!lua_function_args(writer, token->args)) return false;
    lua_append(writer, ")\n");
    if (!lua_statements(writer, token->body, 1, false)) return false;
    lua_append(writer, "end\n\n");
    return true;
}

bool jass_transpile_to_lua(jass_t *j, cstring_t source, string_t *lua_source) {
    static cstring_t const delimiters = ",;()[]+-/*=<>!";
    wordExtractor_t parser;
    token_t *program;
    luaWriter_t writer = {0};
    string_t mutable_source;
    size_t source_length;
    bool ok = false;

    if (lua_source) *lua_source = NULL;
    if (!j || !source || !lua_source) return false;
    source_length = strlen(source);
    mutable_source = malloc(source_length + 1);
    if (!mutable_source) return false;
    memcpy(mutable_source, source, source_length + 1);
    jass_remove_comments(mutable_source);
    parser = MAKE(wordExtractor_t,
                  .buffer = mutable_source,
                  .start = mutable_source,
                  .delimiters = delimiters);
    program = JASS_ParseTokens(&parser);
    if (parser.error || !program) {
        fprintf(stderr, "JASS-to-Lua: source parse failed\n");
        goto cleanup;
    }

    lua_append(&writer, "-- JASS compatibility operators\n"
        "function __jarray(default)\n"
        "    return setmetatable({}, { __index = function() return default end })\n"
        "end\n"
        "function FourCC(id)\n"
        "    return string.byte(id, 1) * 0x1000000 + string.byte(id, 2) * 0x10000 + string.byte(id, 3) * 0x100 + string.byte(id, 4)\n"
        "end\n"
        "function __add(a, b)\n"
        "    if type(a) == 'string' and type(b) == 'string' then return a .. b end\n"
        "    return a + b\n"
        "end\n"
        "function __sub(a, b) return a - b end\n"
        "function __mul(a, b) return a * b end\n"
        "function __div(a, b)\n"
        "    if math.type and math.type(a) == 'integer' and math.type(b) == 'integer' then return math.modf(a / b) end\n"
        "    return a / b\n"
        "end\n"
        "function __unm(a) return -a end\n"
        "function __not(a) return not a end\n"
        "function __and(a, b) return not not a and not not b end\n"
        "function __or(a, b) return not not a or not not b end\n"
        "function __eq(a, b)\n"
        "    if a == nil then return b == nil or b == 0 or b == false end\n"
        "    if b == nil then return a == 0 or a == false end\n"
        "    return a == b\n"
        "end\n"
        "function __ne(a, b) return not __eq(a, b) end\n"
        "function __le(a, b) return (a or 0) <= (b or 0) end\n"
        "function __ge(a, b) return (a or 0) >= (b or 0) end\n"
        "function __lt(a, b) return (a or 0) < (b or 0) end\n"
        "function __gt(a, b) return (a or 0) > (b or 0) end\n"
        "function __lsh(a, b) return a << b end\n"
        "function __rsh(a, b) return a >> b end\n"
        "function __bor(a, b) return a | b end\n"
        "function __band(a, b) return a & b end\n"
        "function __xor(a, b) return a ~ b end\n\n");

    FOR_EACH_LIST(token_t const, token, program) {
        if (token->type == TT_GLOBAL) {
            lua_identifier(&writer, token->secondary);
            lua_append(&writer, " = ");
            if (token->flags & TF_ARRAY) {
                lua_printf(&writer, "__jarray(%s)", lua_default(token->primary));
            } else if (token->init) {
                if (!lua_expression(&writer, token->init)) goto cleanup;
            } else {
                lua_append(&writer, lua_default(token->primary));
            }
            lua_append(&writer, "\n");
        } else if (token->type == TT_FUNCTION) {
            if (!lua_function(&writer, token)) goto cleanup;
        } else if (token->type != TT_TYPEDEF) {
            fprintf(stderr, "JASS-to-Lua: unsupported top-level node %d\n", token->type);
            goto cleanup;
        }
        if (writer.failed) goto cleanup;
    }
    *lua_source = writer.data;
    writer.data = NULL;
    ok = true;

cleanup:
    if (program) JASS_FreeTokens(program);
    free(writer.data);
    free(mutable_source);
    return ok;
}
