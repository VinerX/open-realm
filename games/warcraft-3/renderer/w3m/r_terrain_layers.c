#include "r_terrain_layers.h"

void R_DrawTerrainSegment(mapsegment_t const *segment, uint32_t mask) {
    if (!segment || !Frustum_ContainsAABox(&tr.viewDef.frustum, &segment->bbox))
        return;
    FOR_EACH_LIST(maplayer_t, layer, segment->layers) {
        if (((1 << layer->type) & mask) == 0)
            continue;
        /* The head layer is the opaque base; later ground layers, cliffs and water
         * composite over it, so only the base disables alpha blending. */
        if (layer == segment->layers) {
            R_Call(glDisable, GL_BLEND);
        } else {
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        R_BindTexture(layer->texture, 0);
        R_ApplyShader(&tr.shader_default);
        R_DrawBuffer(layer->buffer, layer->num_vertices);
    }
}
