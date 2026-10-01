#ifndef __wc3_common_terrain_h__
#define __wc3_common_terrain_h__

#include "common/mapinfo.h"
#include "common/cmodel.h"

/* Warcraft III W3E terrain decode constants belong to the game module, not shared engine code. */
#define HEIGHT_COR (TILE_SIZE * 2) // world units; W3E layerHeight - 2 correction; used as the cliff baseline offset
#define WATER_HEIGHT_COR 80 // world units; W3E water baseline correction; used when decoding water vertices
#define DECODE_HEIGHT(x) (((x) - 0x2000) / 4) // raw W3E units; removes encoded bias and scales terrain height
#define WC3_PATH_BLIGHTED 0x20 // pathing flags; authored/runtime Undead Blight; used by WC3 placement and terrain queries

static inline uint32_t WC3_MapVertexEncodedSize(uint32_t version) {
    return version >= 12 ? 8u : 7u;
}

static inline void WC3_DecodeMapVertex(uint8_t const *raw, uint32_t version, war3mapVertex_t *vert) {
    uint16_t const water_and_edge = (uint16_t)(raw[2] | ((uint16_t)raw[3] << 8));
    uint16_t const texture_and_flags = version >= 12
        ? (uint16_t)(raw[4] | ((uint16_t)raw[5] << 8)) : raw[4];
    uint8_t const variation = raw[version >= 12 ? 6 : 5];
    uint8_t const cliff_and_layer = raw[version >= 12 ? 7 : 6];

    memset(vert, 0, sizeof(*vert));
    vert->accurate_height = (uint16_t)(raw[0] | ((uint16_t)raw[1] << 8));
    vert->waterlevel = water_and_edge & 0x3FFF;
    vert->mapedge = (water_and_edge & 0x4000) != 0;
    if (version >= 12) {
        vert->ground = texture_and_flags & 0x3F;
        vert->ramp = (texture_and_flags & 0x40) != 0;
        vert->blight = (texture_and_flags & 0x80) != 0;
        vert->water = (texture_and_flags & 0x100) != 0;
        vert->boundary = (texture_and_flags & 0x200) != 0;
    } else {
        vert->ground = texture_and_flags & 0x0F;
        vert->ramp = (texture_and_flags & 0x10) != 0;
        vert->blight = (texture_and_flags & 0x20) != 0;
        vert->water = (texture_and_flags & 0x40) != 0;
        vert->boundary = (texture_and_flags & 0x80) != 0;
    }
    vert->cliffVariation = (variation >> 5) & 0x07;
    vert->groundVariation = variation & 0x1F;
    vert->cliff = (cliff_and_layer >> 4) & 0x0F;
    vert->level = cliff_and_layer & 0x0F;
}

#ifdef WC3_DEBUG_BLIGHT
#define BLIGHT_LOG(...) do { fprintf(stderr, "WC3_BLIGHT "); fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define BLIGHT_LOG(...) ((void)0)
#endif

static inline bool WC3_ParseBlightTilesetLine(cstring_t line, char *key_out, string_t path_out) {
    char key = 0;
    if (!line || !key_out || !path_out) return false;
    /* 255 is MAX_PATHLEN-1; keeps WorldEditData values inside a PATHSTR. */
    if (sscanf(line, " %c = %*[^,] , %255[^\r\n]", &key, path_out) < 2) return false;
    *key_out = key;
    return true;
}

#endif
