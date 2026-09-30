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

void CL_ResetConfigStringResources(void) {
    memset(model_attempted, 0, sizeof(model_attempted));
    memset(model_pending, 0, sizeof(model_pending));
    memset(portrait_attempted, 0, sizeof(portrait_attempted));
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
}

model_t const *CL_ModelForIndex(uint32_t index) {
    if (!index || index >= MAX_MODELS) return NULL;
    if (!cl.models[index] && !model_attempted[index] && *cl.configstrings[CS_MODELS + index])
        model_pending[index] = true;
    return cl.models[index];
}

void CL_PumpModelLoads(void) {
    static uint32_t cursor = 1;
    FOR_LOOP(n, MAX_MODELS - 1) {
        uint32_t const index = 1 + (cursor - 1 + n) % (MAX_MODELS - 1);
        if (!model_pending[index]) continue;
        model_pending[index] = false;
        cursor = index + 1;
        if (cursor >= MAX_MODELS) cursor = 1;
        if (!model_attempted[index] && *cl.configstrings[CS_MODELS + index])
            CL_RegisterModelConfigString(CS_MODELS + index, false, NULL);
        return;
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
        SAFE_DELETE(cl.models[model], re.ReleaseModel);
        SAFE_DELETE(cl.portraits[model], re.ReleaseModel);
        model_attempted[model] = false;
        model_pending[model] = false;
        portrait_attempted[model] = false;
    } else if (index > CS_IMAGES && index < CS_IMAGES + MAX_IMAGES) CL_RegisterImageConfigString(index, true, olds);
    else if (index > CS_SOUNDS && index < CS_SOUNDS + MAX_SOUNDS && *cl.configstrings[index]) S_RegisterSound(cl.configstrings[index]);
    else if (index > CS_FONTS && index < CS_FONTS + MAX_FONTSTYLES) CL_RegisterFontConfigString(index, true, olds);
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
    for (uint32_t i = 1; i < MAX_IMAGES; i++)
        if (*cl.configstrings[CS_IMAGES + i]) CL_RegisterConfigString(CS_IMAGES + i);
    for (uint32_t i = 1; i < MAX_FONTSTYLES; i++)
        if (*cl.configstrings[CS_FONTS + i]) CL_RegisterConfigString(CS_FONTS + i);
    SCR_UpdateLoadingPlaque();
}
