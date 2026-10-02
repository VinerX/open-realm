/*
 * cl_configstrings.c — Client-side configstring resource lifecycle.
 *
 * Parsing stores the server table; this module resolves media on demand and
 * invalidates handles when an authoritative configstring changes.
 */
#include <stdlib.h>

#include "client.h"
#include "sound/s_local.h"

static bool model_attempted[MAX_MODELS];
static bool model_pending[MAX_MODELS];
static bool portrait_attempted[MAX_MODELS];
static bool image_attempted[MAX_IMAGES];
static bool image_pending[MAX_IMAGES];
static uint32_t model_cursor = 1, image_cursor = 1;
static bool model_first = true;
static void CL_RegisterImageConfigString(uint32_t index, bool replace, cstring_t olds);

void CL_ResetConfigStringResources(void) {
    memset(model_attempted, 0, sizeof(model_attempted));
    memset(model_pending, 0, sizeof(model_pending));
    memset(portrait_attempted, 0, sizeof(portrait_attempted));
    memset(image_attempted, 0, sizeof(image_attempted));
    memset(image_pending, 0, sizeof(image_pending));
    model_cursor = image_cursor = 1;
    model_first = true;
}

/* Avoid reloading a resource when the server resends the same configstring. */
static bool CL_SameResource(void const *handle, cstring_t olds, cstring_t name) {
    return handle && name && name[0] && olds && !strcmp(olds, name);
}

/* Register a model configstring; portrait art is loaded only when requested. */
static void CL_RegisterModelConfigString(uint32_t index, bool replace, cstring_t olds) {
    uint32_t model = index - CS_MODELS;
    cstring_t name = cl.configstrings[index];
    model_attempted[model] = true;
    model_pending[model] = false;
    portrait_attempted[model] = false;
    if (!replace && cl.models[model]) return;
    if (replace && CL_SameResource(cl.models[model], olds, name)) return;
    if (cl.models[model]) SAFE_DELETE(cl.models[model], re.ReleaseModel);
    if (cl.portraits[model]) SAFE_DELETE(cl.portraits[model], re.ReleaseModel);
    if (!*name) return;
    cl.models[model] = re.LoadModel(name);
    if (!cl.models[model]) fprintf(stderr, "CL_RegisterModelConfigString: failed to load %s\n", name);
    if (replace) {
        PATHSTR portrait = { 0 };
        cstring_t ext = strstr(name, ".m");
        if (ext) {
            size_t base_len = (size_t)(ext - name);
            if (base_len >= sizeof(portrait)) base_len = sizeof(portrait) - 1;
            memcpy(portrait, name, base_len);
            portrait[base_len] = '\0';
            snprintf(portrait + base_len, sizeof(portrait) - base_len, "_Portrait%s", ext);
        }
        if (portrait[0] && FS_FileExists(portrait)) cl.portraits[model] = re.LoadModel(portrait);
    }
}

model_t const *CL_ModelForIndex(uint32_t index) {
    if (!index || index >= MAX_MODELS) return NULL;
    if (!cl.models[index] && !model_attempted[index] && *cl.configstrings[CS_MODELS + index])
        model_pending[index] = true;
    return cl.models[index];
}

texture_t const *CL_PicForIndex(uint32_t index) {
    if (!index || index >= MAX_IMAGES) return NULL;
    if (!cl.pics[index] && !image_attempted[index] && *cl.configstrings[CS_IMAGES + index])
        image_pending[index] = true;
    return cl.pics[index];
}

void CL_PumpMediaLoads(void) {
    FOR_LOOP(pass, 2) {
        bool const do_model = (pass == 0) == model_first;
        uint32_t *cursor = do_model ? &model_cursor : &image_cursor;
        uint32_t const count = do_model ? MAX_MODELS : MAX_IMAGES;
        bool *pending = do_model ? model_pending : image_pending;
        FOR_LOOP(n, count - 1) {
            uint32_t const index = 1 + (*cursor - 1 + n) % (count - 1);
            if (!pending[index]) continue;
            pending[index] = false;
            *cursor = index + 1;
            if (*cursor >= count) *cursor = 1;
            if (do_model) {
                if (!model_attempted[index] && *cl.configstrings[CS_MODELS + index])
                    CL_RegisterModelConfigString(CS_MODELS + index, false, NULL);
            } else if (!image_attempted[index] && *cl.configstrings[CS_IMAGES + index]) {
                CL_RegisterImageConfigString(CS_IMAGES + index, false, NULL);
            }
            model_first = !do_model;
            return;
        }
    }
}

model_t const *CL_PortraitForIndex(uint32_t index) {
    cstring_t name;
    char const *ext;
    PATHSTR portrait = { 0 };
    size_t base_len;

    if (!index || index >= MAX_MODELS) return NULL;
    if (cl.portraits[index] || portrait_attempted[index]) return cl.portraits[index];
    name = cl.configstrings[CS_MODELS + index];
    if (!*name) return NULL;
    portrait_attempted[index] = true;
    ext = strstr(name, ".m");
    if (!ext) return NULL;
    base_len = (size_t)(ext - name);
    if (base_len >= sizeof(portrait)) base_len = sizeof(portrait) - 1;
    memcpy(portrait, name, base_len);
    portrait[base_len] = '\0';
    snprintf(portrait + base_len, sizeof(portrait) - base_len, "_Portrait%s", ext);
    if (FS_FileExists(portrait)) cl.portraits[index] = re.LoadModel(portrait);
    return cl.portraits[index];
}

/* Register one image configstring during refresh preparation or a late update. */
static void CL_RegisterImageConfigString(uint32_t index, bool replace, cstring_t olds) {
    uint32_t image = index - CS_IMAGES;
    cstring_t name = cl.configstrings[index];
    image_attempted[image] = true;
    image_pending[image] = false;
    if (!replace && cl.pics[image]) return;
    if (replace && CL_SameResource(cl.pics[image], olds, name)) return;
    if (cl.pics[image]) {
        re.ReleaseTexture((texture_t *)cl.pics[image]);
        cl.pics[image] = NULL;
    }
    if (*name) cl.pics[image] = re.LoadTexture(name);
}

/* Register one font configstring after parsing its optional path,size encoding. */
static void CL_RegisterFontConfigString(uint32_t index, bool replace, cstring_t olds) {
    uint32_t font = index - CS_FONTS;
    cstring_t spec = cl.configstrings[index];
    if (cl.fonts[font]) return;
    if (replace && CL_SameResource(cl.fonts[font], olds, spec)) return;
    if (*spec) {
        cstring_t split = strstr(spec, ",");
        if (split) {
            PATHSTR name = { 0 };
            memcpy(name, spec, split - spec);
            cl.fonts[font] = re.LoadFont(name, atoi(split + 1));
        } else cl.fonts[font] = re.LoadFont(spec, 16);
    }
}

/* The server chooses point-order art; an empty slot explicitly disables that presentation. */
static void CL_RegisterOrderMarker(void) {
    cstring_t name = cl.configstrings[CS_ORDER_MARKER];
    SAFE_DELETE(cl.moveConfirmation, re.ReleaseModel);
    if (!*name) return;
    cl.moveConfirmation = re.LoadModel(name);
    if (!cl.moveConfirmation) fprintf(stderr, "CL_RegisterOrderMarker: failed to load %s\n", name);
}

void CL_RegisterConfigString(uint32_t index) {
    if (index == CS_ORDER_MARKER) CL_RegisterOrderMarker();
    if (index > CS_MODELS && index < CS_MODELS + MAX_MODELS) CL_RegisterModelConfigString(index, false, NULL);
    else if (index > CS_IMAGES && index < CS_IMAGES + MAX_IMAGES) CL_RegisterImageConfigString(index, false, NULL);
    else if (index > CS_FONTS && index < CS_FONTS + MAX_FONTSTYLES) CL_RegisterFontConfigString(index, false, NULL);
}

void CL_UpdateConfigString(uint32_t index, cstring_t olds) {
    if (index == CS_ORDER_MARKER && strcmp(olds, cl.configstrings[index])) CL_RegisterOrderMarker();
    if (index > CS_MODELS && index < CS_MODELS + MAX_MODELS) {
        uint32_t const model = index - CS_MODELS;
        cstring_t const name = cl.configstrings[index];
        if (CL_SameResource(cl.models[model], olds, name)) return;
        model_attempted[model] = false;
        model_pending[model] = false;
        portrait_attempted[model] = false;
        CL_RegisterModelConfigString(index, true, olds);
    } else if (index > CS_IMAGES && index < CS_IMAGES + MAX_IMAGES) {
        uint32_t const image = index - CS_IMAGES;
        cstring_t const name = cl.configstrings[index];
        if (CL_SameResource(cl.pics[image], olds, name)) return;
        image_attempted[image] = false;
        image_pending[image] = false;
        CL_RegisterImageConfigString(index, true, olds);
    }
    else if (index > CS_SOUNDS && index < CS_SOUNDS + MAX_SOUNDS && *cl.configstrings[index]) S_RegisterSound(cl.configstrings[index]);
    else if (index > CS_FONTS && index < CS_FONTS + MAX_FONTSTYLES) CL_RegisterFontConfigString(index, true, olds);
}

static void CL_RegisterLoadingFrameMedia(uiFrame_t const *frame) {
    uint32_t image = 0, image2 = 0;
    if (!frame) return;
    switch (frame->flags.type) {
    case FT_SPRITE:
    case FT_PORTRAIT:
        if (frame->tex.index && frame->tex.index < MAX_MODELS)
            CL_RegisterConfigString(CS_MODELS + frame->tex.index);
        break;
    case FT_TEXTURE:
    case FT_SIMPLESTATUSBAR:
    case FT_SEGMENTED_STATUSBAR:
    case FT_LOADING_BAR:
    case FT_COMMANDBUTTON:
        image = frame->tex.index;
        image2 = frame->tex.index2;
        break;
    default:
        break;
    }
    if (image && image < MAX_IMAGES) CL_RegisterConfigString(CS_IMAGES + image);
    if (image2 && image2 < MAX_IMAGES) CL_RegisterConfigString(CS_IMAGES + image2);
}

/* svc_loading_screen commits the screen after its dependencies; present before the bulk table/world arrives. */
void CL_PrepLoading(void) {
    if (cl.refresh_prepped) return;
    if (!cl.layout[LAYER_LOADING] || !*cl.configstrings[CS_WORLD]) {
        Com_Error(ERR_DROP, "Incomplete loading presentation");
        return;
    }
    if (cl.playerstate.client_ui_state != CLIENT_UI_LOADING)
        CL_BeginLoadingMap(cl.configstrings[CS_WORLD]);
    re.SetAssetScope(cl.configstrings[CS_ASSET_SCOPE]);
    SCR_Clear(cl.layout[LAYER_LOADING]);
    FOR_LOOP(i, SCR_NumFrames()) CL_RegisterLoadingFrameMedia(SCR_Frame(i));
    for (uint32_t i = 1; i < MAX_FONTSTYLES; i++)
        if (*cl.configstrings[CS_FONTS + i]) CL_RegisterConfigString(CS_FONTS + i);
    SCR_UpdateLoadingPlaque();
}

#ifdef BZ_TESTS
#include "shared/test.h"
static uint32_t cl_test_loading_model_calls;
static int cl_test_loading_model;
static model_t *CL_TestLoadingModel(cstring_t name) {
    T_ASSERT(!strcmp(name, "UI\\Glues\\Loading\\LoadBar\\LoadBar.mdx"));
    cl_test_loading_model_calls++;
    return (model_t *)&cl_test_loading_model;
}

TEST(client_loading, sprite_media_is_ready_before_first_paint) {
    __typeof__(cl) *saved = MemAlloc(sizeof(cl));
    __typeof__(re.LoadModel) old_load = re.LoadModel;
    uiFrame_t frame = {0};
    bool old_attempted = model_attempted[3], old_pending = model_pending[3];
    bool old_portrait_attempted = portrait_attempted[3];
    memcpy(saved, &cl, sizeof(cl));
    cl.models[3] = NULL;
    cl.portraits[3] = NULL;
    snprintf(cl.configstrings[CS_MODELS + 3], sizeof(cl.configstrings[CS_MODELS + 3]),
             "UI\\Glues\\Loading\\LoadBar\\LoadBar.mdx");
    frame.flags.type = FT_SPRITE;
    frame.tex.index = 3;
    frame.stat = UI_STAT_LOADING_PROGRESS;
    re.LoadModel = CL_TestLoadingModel;
    cl_test_loading_model_calls = 0;
    CL_RegisterLoadingFrameMedia(&frame);
    T_NOT_NULL(cl.models[3]);
    T_EQ(cl_test_loading_model_calls, 1);
    T_ASSERT(!model_pending[3]);
    CL_RegisterLoadingFrameMedia(&frame);
    T_EQ(cl_test_loading_model_calls, 1);
    re.LoadModel = old_load;
    memcpy(&cl, saved, sizeof(cl)); MemFree(saved);
    model_attempted[3] = old_attempted; model_pending[3] = old_pending;
    portrait_attempted[3] = old_portrait_attempted;
}
#endif
