#include "fs_casc.h"

#include "../../../vendor/casclib/src/CascLib.h"

#ifdef _WIN32
#undef far
#undef near
#undef GetClassName
#undef SetRect
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Reforged splits files across the base module, graphics overlays, and locale
 * overlays. Keep module resolution here so every filesystem consumer sees the
 * same preferred-locale and base-first rules. */
#define FS_CASC_MAX_LOCALES 16
#define FS_CASC_LOCALE_LEN  8

struct fsCascStorage_s {
    HANDLE handle;
    char root[4096];
    char preferred_locale[FS_CASC_LOCALE_LEN];
    uint32_t num_locales;
    char locales[FS_CASC_MAX_LOCALES][FS_CASC_LOCALE_LEN];
};

struct fsCascFile_s {
    HANDLE handle;
};

static bool FS_CascIsLocaleCode(char const *code) {
    return code && strlen(code) == 4 &&
           isalpha((unsigned char)code[0]) && isalpha((unsigned char)code[1]) &&
           isalpha((unsigned char)code[2]) && isalpha((unsigned char)code[3]);
}

static void FS_CascLocaleDir(char const *code, char *out, size_t out_size) {
    size_t i;
    if (!out || out_size == 0) return;
    for (i = 0; i + 1 < out_size && code && code[i]; i++)
        out[i] = (char)tolower((unsigned char)code[i]);
    out[i] = '\0';
}

/* Splits one field into alphanumeric tokens, returning the Nth (0-based). */
static bool FS_CascFieldToken(char const *field, uint32_t index, char *out, size_t out_size) {
    uint32_t token = 0;
    size_t n = 0;
    bool in_token = false;

    if (!field || !out || out_size == 0) return false;
    for (; *field; field++) {
        if (isalnum((unsigned char)*field)) {
            if (!in_token) {
                if (token == index) n = 0;
                in_token = true;
            }
            if (token == index && n + 1 < out_size) out[n++] = *field;
        } else if (in_token) {
            in_token = false;
            if (token == index) { out[n] = '\0'; return true; }
            token++;
        }
    }
    if (in_token && token == index) { out[n] = '\0'; return true; }
    return false;
}

/* The active text locale is the install's own choice, recorded in .build.info's
 * Tags column as "<locale> text".  Reading it here keeps the fallback order
 * authoritative instead of guessing a default language. */
static bool FS_CascPreferredLocale(char const *root, char *out, size_t out_size) {
    char path[4096];
    char *data, *header, *value, *field, *nl;
    FILE *fp;
    long length;
    int column = -1, index = 0;
    bool found = false;

    if (!root || !out || out_size == 0) return false;
    out[0] = '\0';
    if (snprintf(path, sizeof(path), "%s/.build.info", root) >= (int)sizeof(path)) return false;
    fp = fopen(path, "rb");
    if (!fp) return false;
    if (fseek(fp, 0, SEEK_END) != 0 || (length = ftell(fp)) <= 0 || length > 65536 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }
    data = malloc((size_t)length + 1);
    if (!data || fread(data, 1, (size_t)length, fp) != (size_t)length) {
        free(data);
        fclose(fp);
        return false;
    }
    data[length] = '\0';
    fclose(fp);

    header = data;
    value = strchr(header, '\n');
    if (!value) { free(data); return false; }
    *value++ = '\0';
    if ((nl = strchr(value, '\n'))) *nl = '\0';

    for (field = header; field && *field; index++) {
        char *bar = strchr(field, '|');
        if (bar) *bar = '\0';
        if (!strncmp(field, "Tags", 4)) column = index;
        field = bar ? bar + 1 : NULL;
    }
    if (column < 0) { free(data); return false; }

    field = value;
    for (index = 0; field && *field && index <= column; index++) {
        char *bar = strchr(field, '|');
        if (bar) *bar = '\0';
        if (index == column) {
            char token[64] = { 0 }, previous[64] = { 0 };
            uint32_t n;
            for (n = 0; FS_CascFieldToken(field, n, token, sizeof(token)); n++) {
                if (!strcasecmp(token, "text") && FS_CascIsLocaleCode(previous)) {
                    FS_CascLocaleDir(previous, out, out_size);
                    found = true;
                    break;
                }
                snprintf(previous, sizeof(previous), "%s", token);
            }
        }
        field = bar ? bar + 1 : NULL;
    }
    free(data);
    return found;
}

/* The storage's tag table is the authoritative list of locales it may carry. */
static void FS_CascCollectLocales(fsCascStorage_t *storage) {
    char buffer[8192];
    size_t needed = 0;
    PCASC_STORAGE_TAGS tags;
    uint32_t i;

    if (!CascGetStorageInfo(storage->handle, CascStorageTags, buffer, sizeof(buffer), &needed))
        return;
    tags = (PCASC_STORAGE_TAGS)buffer;
    for (i = 0; i < tags->TagCount && storage->num_locales < FS_CASC_MAX_LOCALES; i++) {
        char dir[FS_CASC_LOCALE_LEN];
        if (!FS_CascIsLocaleCode(tags->Tags[i].szTagName)) continue;
        FS_CascLocaleDir(tags->Tags[i].szTagName, dir, sizeof(dir));
        snprintf(storage->locales[storage->num_locales], FS_CASC_LOCALE_LEN, "%s", dir);
        storage->num_locales++;
    }
}

/* Put the install's text locale first so the common case resolves on one probe. */
static void FS_CascOrderLocales(fsCascStorage_t *storage) {
    uint32_t i;
    if (!storage->preferred_locale[0]) return;
    for (i = 0; i < storage->num_locales; i++) {
        if (strcasecmp(storage->locales[i], storage->preferred_locale)) continue;
        if (i) {
            char first[FS_CASC_LOCALE_LEN];
            snprintf(first, sizeof(first), "%s", storage->locales[0]);
            snprintf(storage->locales[0], FS_CASC_LOCALE_LEN, "%s", storage->locales[i]);
            snprintf(storage->locales[i], FS_CASC_LOCALE_LEN, "%s", first);
        }
        return;
    }
}

bool FS_CascOpenStorage(char const *root, char const *product, fsCascStorage_t **out) {
    CASC_OPEN_STORAGE_ARGS args = { sizeof(CASC_OPEN_STORAGE_ARGS) };
    fsCascStorage_t *storage;

    if (out) *out = NULL;
    if (!root || !*root || !product || !*product || !out) return false;
    storage = calloc(1, sizeof(*storage));
    if (!storage) return false;
    snprintf(storage->root, sizeof(storage->root), "%s", root);
    args.szLocalPath = root;
    args.szCodeName = product;
    args.dwLocaleMask = CASC_LOCALE_NONE;
    if (!CascOpenStorageEx(NULL, &args, false, &storage->handle)) {
        free(storage);
        return false;
    }
    FS_CascPreferredLocale(root, storage->preferred_locale, sizeof(storage->preferred_locale));
    FS_CascCollectLocales(storage);
    FS_CascOrderLocales(storage);
    *out = storage;
    return true;
}

void FS_CascCloseStorage(fsCascStorage_t *storage) {
    if (!storage) return;
    CascCloseStorage(storage->handle);
    free(storage);
}

/* A locale module that is not installed still carries file *names* in the
 * storage tree, so CascOpenFile succeeds for a phantom entry while its size
 * query fails.  Treat an entry without a readable size as absent, otherwise an
 * uninstalled locale would shadow the base module's real file. */
static bool FS_CascTryOpen(fsCascStorage_t *storage, char const *name, HANDLE *out) {
    HANDLE handle = NULL;
    ULONGLONG size;

    *out = NULL;
    if (!CascOpenFile(storage->handle, name, CASC_LOCALE_NONE, CASC_OPEN_BY_NAME, &handle))
        return false;
    if (!CascGetFileSize64(handle, &size)) {
        CascCloseFile(handle);
        return false;
    }
    *out = handle;
    return true;
}

static bool FS_CascTryGraphicsModule(fsCascStorage_t *storage, char const *module,
                                     char const *path, HANDLE *out) {
    char namespaced_path[4096];
    int length;

    if (storage->preferred_locale[0]) {
        length = snprintf(namespaced_path, sizeof(namespaced_path),
                          "war3.w3mod:%s.w3mod:_locales\\%s.w3mod:%s",
                          module, storage->preferred_locale, path);
        if (length < (int)sizeof(namespaced_path) &&
            FS_CascTryOpen(storage, namespaced_path, out)) return true;
    }
    length = snprintf(namespaced_path, sizeof(namespaced_path),
                      "war3.w3mod:%s.w3mod:%s", module, path);
    return length < (int)sizeof(namespaced_path) &&
           FS_CascTryOpen(storage, namespaced_path, out);
}

bool FS_CascOpenFile(fsCascStorage_t *storage, char const *path, fsCascFile_t **out) {
    char namespaced_path[4096];
    HANDLE handle = NULL;
    fsCascFile_t *file;
    uint32_t i;

    if (out) *out = NULL;
    if (!storage || !path || !*path || !out) return false;
    if (strchr(path, ':')) {
        FS_CascTryOpen(storage, path, &handle);
        if (!handle) return false;
        goto wrap;
    }
    /* Probe order: the install's text locale first (the common localized hit),
     * then the base module (most city/unit data), then remaining locales so a
     * file still resolves when the preferred locale is not installed. */
    if (snprintf(namespaced_path, sizeof(namespaced_path),
                 "war3.w3mod:_locales\\%s.w3mod:%s", storage->locales[0], path) <
        (int)sizeof(namespaced_path))
        FS_CascTryOpen(storage, namespaced_path, &handle);
    if (!handle && snprintf(namespaced_path, sizeof(namespaced_path), "war3.w3mod:%s", path) <
            (int)sizeof(namespaced_path))
        FS_CascTryOpen(storage, namespaced_path, &handle);
    /* Classic/base assets retain precedence. Warcraft III 3.0 stores some
     * shared DDS resources only in these nested graphics modules, including
     * textures referenced by otherwise base-module MDX models. */
    if (!handle) FS_CascTryGraphicsModule(storage, "_de", path, &handle);
    if (!handle) FS_CascTryGraphicsModule(storage, "_hd", path, &handle);
    for (i = 1; i < storage->num_locales && !handle; i++) {
        if (snprintf(namespaced_path, sizeof(namespaced_path),
                     "war3.w3mod:_locales\\%s.w3mod:%s", storage->locales[i], path) >=
            (int)sizeof(namespaced_path))
            continue;
        FS_CascTryOpen(storage, namespaced_path, &handle);
    }
    if (!handle)
        FS_CascTryOpen(storage, path, &handle);
    if (!handle) return false;
wrap:
    file = calloc(1, sizeof(*file));
    if (!file) {
        CascCloseFile(handle);
        return false;
    }
    file->handle = handle;
    *out = file;
    return true;
}

bool FS_CascRead(fsCascFile_t *file, void *buffer, uint32_t length, uint32_t *bytes_read) {
    DWORD read = 0;
    if (!file || (length && !buffer) || !CascReadFile(file->handle, buffer, length, &read))
        return false;
    if (bytes_read) *bytes_read = read;
    return true;
}

bool FS_CascSeek(fsCascFile_t *file, int64_t distance, uint32_t origin) {
    ULONGLONG position;
    return file && CascSetFilePointer64(file->handle, distance, &position, origin);
}

bool FS_CascSize(fsCascFile_t *file, uint64_t *size) {
    ULONGLONG file_size;
    if (!file || !size || !CascGetFileSize64(file->handle, &file_size)) return false;
    *size = (uint64_t)file_size;
    return true;
}

void FS_CascCloseFile(fsCascFile_t *file) {
    if (!file) return;
    CascCloseFile(file->handle);
    free(file);
}

uint32_t FS_CascLastError(void) {
    return (uint32_t)GetCascError();
}
