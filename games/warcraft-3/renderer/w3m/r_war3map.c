#include "r_war3map.h"
#include "../mdx/r_mdx.h"
#include "renderer/r_shader.h"

mapsegment_t *g_mapSegments = NULL;
static cameraHeightMap_t w3_camera_height;
static uint32_t w3_map_load_started;

static uint32_t R_W3MapStageBegin(cstring_t phase) {
    uint32_t const now = SDL_GetTicks();
    fprintf(stderr, "WC3_MAP_LOAD phase=%s status=begin elapsed_ms=%u\n",
            phase, w3_map_load_started ? now - w3_map_load_started : 0);
    return now;
}

static void R_W3MapStageEnd(cstring_t phase, uint32_t started) {
    uint32_t const now = SDL_GetTicks();
    fprintf(stderr, "WC3_MAP_LOAD phase=%s status=done duration_ms=%u elapsed_ms=%u\n",
            phase, (unsigned)(now - started),
            w3_map_load_started ? now - w3_map_load_started : 0);
}

#define WC3_CAMERA_HEIGHT_RADIUS 4 // terrain cells; half-width of the client camera blur footprint
static float r_w3_camera_grid_height(void const *data, uint32_t x, uint32_t y) {
    return GetWar3MapVertexHeight(GetWar3MapVertex(data, x, y));
}

static void R_FreeMapLayers(maplayer_t * *layers) {
    while (*layers) {
        maplayer_t *layer = *layers;
        *layers = layer->next;
        if (layer->buffer) R_ReleaseVertexArrayObject((buffer_t *)layer->buffer);
        ri.MemFree(layer);
    }
}

static void R_FreeMapSegments(void) {
    while (g_mapSegments) {
        mapsegment_t *segment = g_mapSegments;
        g_mapSegments = segment->next;
        R_FreeMapLayers(&segment->layers);
        ri.MemFree(segment);
    }
}

static void R_FreeWar3Map(war3map_t *map) {
    if (!map) return;
    SAFE_DELETE(map->grounds, ri.MemFree);
    SAFE_DELETE(map->cliffs, ri.MemFree);
    SAFE_DELETE(map->vertices, ri.MemFree);
    ri.MemFree(map);
}

void _W3M_ClearMap(void) {
    texture_t *shadow = tr.texture[TEX_TERRAIN_SHADOW];

    R_FreeMapSegments();
    R_ResetGroundTextures();
    R_ResetCliffCache();
    R_ResetBlightCache();
    R_FreeCameraHeightMap(&w3_camera_height);
    R_ShutdownFogOfWar();
    SAFE_DELETE(tr.minimap, R_ReleaseTexture);
    tr.texture[TEX_TERRAIN_SHADOW] = NULL;
    if (shadow) R_ReleaseTexture(shadow);
    if (tr.world) {
        R_FreeWar3Map((war3map_t *)tr.world);
        tr.world = NULL;
    }
}

static void R_FileReadShadowMap(handle_t hMpq, war3map_t *pWorld) {
    handle_t file;
    if (!SFileOpenFileEx(hMpq, "war3map.shd", SFILE_OPEN_FROM_MPQ, &file)) {
        return;
    }
    int const w = (pWorld->width - 1) * 4;
    int const h = (pWorld->height - 1) * 4;
    string_t shadows = ri.MemAlloc(w * h);
    if (!SFileReadFile(file, shadows, w * h, NULL, NULL)) {
        ri.MemFree(shadows);
        SFileCloseFile(file);
        return;
    }
    texture_t *pShadowmap = R_AllocateTexture(w, h);
    color32_t *pixels = ri.MemAlloc(w * h * sizeof(struct color32));
    FOR_LOOP(i, w * h) {
        uint8_t shadow = (uint8_t)shadows[i];
        pixels[i].r = 0;
        pixels[i].g = 0;
        pixels[i].b = 0;
        pixels[i].a = shadow / 2;
    }
    R_LoadTextureMipLevel(pShadowmap, &(texMip_t){ pixels, w, h, 0, PIXEL_RGBA });
    SFileCloseFile(file);
    ri.MemFree(shadows);
    ri.MemFree(pixels);

    tr.texture[TEX_TERRAIN_SHADOW] = pShadowmap;
}

typedef struct {
    uint64_t water_ms;
    uint64_t cliffs_ms;
    uint64_t ground_ms;
    uint32_t cliff_layers;
} mapSegmentBuildStats_t;

static mapsegment_t *R_BuildMapSegment(war3map_t const *map, uint32_t sx, uint32_t sy,
                                       mapSegmentBuildStats_t *stats) {
    mapsegment_t *mapSegment = ri.MemAlloc(sizeof(mapsegment_t));
    uint32_t started = SDL_GetTicks();
    maplayer_t *mapLayer = R_BuildMapSegmentWater(map, sx, sy);
    stats->water_ms += SDL_GetTicks() - started;
    ADD_TO_LIST(mapLayer, mapSegment->layers);
    started = SDL_GetTicks();
    FOR_LOOP(cliff, map->num_cliffs) {
        if ((mapLayer = R_BuildMapSegmentCliffs(map, sx, sy, cliff))) {
            ADD_TO_LIST(mapLayer, mapSegment->layers);
            stats->cliff_layers++;
        }
    }
    stats->cliffs_ms += SDL_GetTicks() - started;
    /* Ground is built per segment so a view can cull it; a whole-map buffer submits
     * every tile in the map each frame and collapses frame rate on large maps. */
    started = SDL_GetTicks();
    for (uint32_t layer = map->num_grounds; layer > 0; layer--) {
        if ((mapLayer = R_BuildMapSegmentLayer(map, sx, sy, layer - 1))) {
            ADD_TO_LIST(mapLayer, mapSegment->layers);
        }
    }
    stats->ground_ms += SDL_GetTicks() - started;
    mapSegment->bbox.min = MAKE(vec3_t, FLT_MAX, FLT_MAX, FLT_MAX);
    mapSegment->bbox.max = MAKE(vec3_t, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    return mapSegment;
}

static vec3_t R_GetMapVertexPoint(war3map_t const *map, uint32_t x, uint32_t y) {
    war3mapVertex_t const *mapVertex = GetWar3MapVertex(map, x, y);
    return (vec3_t) {
        .x = map->center.x + x * TILE_SIZE,
        .y = map->center.y + y * TILE_SIZE,
        .z = GetWar3MapVertexHeight(mapVertex),
    };
}

static void R_LoadMapSegments(war3map_t const *map) {
    uint32_t const segments_x = (map->width - 1) / SEGMENT_SIZE;
    uint32_t const segments_y = (map->height - 1) / SEGMENT_SIZE;
    uint32_t const total = segments_x * segments_y;
    uint32_t completed = 0;
    uint32_t last_report = SDL_GetTicks();
    mapSegmentBuildStats_t stats = {0};
    float min_height = FLT_MAX, max_height = -FLT_MAX;
    FOR_LOOP(fx, (map->width - 1) / SEGMENT_SIZE) {
        FOR_LOOP(fy, (map->height - 1) / SEGMENT_SIZE) {
            uint32_t const started = SDL_GetTicks();
            mapsegment_t *segment = R_BuildMapSegment(map, fx, fy, &stats);
            ADD_TO_LIST(segment, g_mapSegments);
            FOR_LOOP(sx, SEGMENT_SIZE+1) {
                FOR_LOOP(sy, SEGMENT_SIZE+1) {
                    float x = fx * SEGMENT_SIZE + sx;
                    float y = fy * SEGMENT_SIZE + sy;
                    vec3_t v = R_GetMapVertexPoint(map, x, y);
                    min_height = MIN(min_height, v.z);
                    max_height = MAX(max_height, v.z);
                    segment->bbox.min.x = MIN(segment->bbox.min.x, v.x);
                    segment->bbox.min.y = MIN(segment->bbox.min.y, v.y);
                    segment->bbox.min.z = MIN(segment->bbox.min.z, v.z);
                    segment->bbox.max.x = MAX(segment->bbox.max.x, v.x);
                    segment->bbox.max.y = MAX(segment->bbox.max.y, v.y);
                    segment->bbox.max.z = MAX(segment->bbox.max.z, v.z);
                }
            }
            completed++;
            uint32_t const now = SDL_GetTicks();
            if (now - started >= 250 || now - last_report >= 1000) {
                fprintf(stderr,
                        "WC3_MAP_LOAD phase=terrain_segment item=%u/%u segment=(%u,%u) item_ms=%u elapsed_ms=%u\n",
                        (unsigned)completed, (unsigned)total, (unsigned)fx, (unsigned)fy,
                        (unsigned)(now - started),
                        w3_map_load_started ? now - w3_map_load_started : 0);
                last_report = now;
            }
        }
    }
    fprintf(stderr,
            "WC3_MAP_LOAD phase=terrain_segment_build segments=%u water_ms=%llu cliffs_ms=%llu ground_ms=%llu cliff_layers=%u\n",
            (unsigned)completed, (unsigned long long)stats.water_ms,
            (unsigned long long)stats.cliffs_ms, (unsigned long long)stats.ground_ms,
            (unsigned)stats.cliff_layers);
    fprintf(stderr, "WC3_MAP_LOAD phase=terrain_height_bounds min_z=%.3f max_z=%.3f\n",
            (double)min_height, (double)max_height);
}

void R_AllocateFogOfWar(war3map_t *map) {
    R_InitFogOfWar((map->width - 1) * 4, (map->height - 1) * 4);
}

static void R_LoadMapMinimap(handle_t hMpq, cstring_t mapFilename) {
    static cstring_t const candidates[] = {
        "war3mapMap.blp",
        "war3mapMap.tga",
        NULL,
    };

    SAFE_DELETE(tr.minimap, R_ReleaseTexture);

    FOR_LOOP(i, sizeof(candidates) / sizeof(candidates[0])) {
        handle_t file;
        PATHSTR path;

        if (!candidates[i]) {
            break;
        }
        if (!SFileOpenFileEx(hMpq, candidates[i], SFILE_OPEN_FROM_MPQ, &file)) {
            continue;
        }
        SFileCloseFile(file);
        snprintf(path, sizeof(path), "%s\\%s", mapFilename, candidates[i]);
        tr.minimap = R_LoadTexture(path);
        return;
    }
}

war3map_t *FileReadWar3Map(handle_t archive) {
    war3map_t *map = ri.MemAlloc(sizeof(war3map_t));
    handle_t file;
    SFileOpenFileEx(archive, "war3map.w3e", SFILE_OPEN_FROM_MPQ, &file);
    SFileReadFile(file, &map->header, 4, NULL, NULL);
    SFileReadFile(file, &map->version, 4, NULL, NULL);
    SFileReadFile(file, &map->tileset, 1, NULL, NULL);
    SFileReadFile(file, &map->custom, 4, NULL, NULL);
    SFileReadArray(file, map, grounds, 4, ri.MemAlloc);
    SFileReadArray(file, map, cliffs, 4, ri.MemAlloc);
    SFileReadFile(file, &map->width, 4, NULL, NULL);
    SFileReadFile(file, &map->height, 4, NULL, NULL);
    SFileReadFile(file, &map->center, 8, NULL, NULL);
    uint32_t const num_vertices = map->width * map->height;
    uint32_t const vertex_size = WC3_MapVertexEncodedSize(map->version);
    int const vertexblocksize = sizeof(war3mapVertex_t) * num_vertices;
    map->vertices = ri.MemAlloc(vertexblocksize);
    R_AllocateFogOfWar(map);
    uint8_t vertex_chunk[8 * 8192];
    for (uint32_t first = 0; first < num_vertices;) {
        uint32_t const count = MIN(num_vertices - first, 8192u);
        if (!SFileReadFile(file, vertex_chunk, count * vertex_size, NULL, NULL)) {
            fprintf(stderr, "WC3: truncated war3map.w3e vertex data at vertex %u (version %u, stride %u)\n",
                    (unsigned)first, (unsigned)map->version, (unsigned)vertex_size);
            break;
        }
        FOR_LOOP(i, count)
            WC3_DecodeMapVertex(vertex_chunk + i * vertex_size, map->version,
                                (war3mapVertex_t *)map->vertices + first + i);
        first += count;
    }
    SFileCloseFile(file);
    FOR_LOOP(y, map->height) {
//        printf("%04x  ", y);
        FOR_LOOP(x, map->width) {
            war3mapVertex_t *vert = (war3mapVertex_t *)GetWar3MapVertex(map, x, y);
            if (!vert->ramp)
                continue;
            vert->cliffVariation = 0; // used also to mark mid-ramp
            war3mapVertex_t const *l = GetWar3MapVertex(map, x-1, y);
            war3mapVertex_t const *r = GetWar3MapVertex(map, x+1, y);
            war3mapVertex_t const *t = GetWar3MapVertex(map, x, y-1);
            war3mapVertex_t const *b = GetWar3MapVertex(map, x, y+1);
            if (l && r && l->ramp && r->ramp && l->level != r->level) {
                vert->cliffVariation = 1;
            } else if (t && b && t->ramp && b->ramp && t->level != b->level) {
                vert->cliffVariation = 1;
            }
//            printf("%x", GetWar3MapVertex(map, x, y)->level);
        }
//        printf("\n");
    }
    return map;
}

void _W3M_RegisterMap(char const *mapFilename) {
    handle_t hMpq;
    uint8_t *mapData;
    int mapSize;
    war3map_t *map;
    uint32_t started;

    w3_map_load_started = SDL_GetTicks();
    fprintf(stderr, "WC3_MAP_LOAD phase=begin map=\"%s\"\n", mapFilename ? mapFilename : "");

    /* A map registration replaces the whole WC3 world.  Free GPU buffers,
     * map-owned models, fog targets and source terrain before creating the
     * next level so repeated campaign transitions do not accumulate state. */
    _W3M_ClearMap();

    /* Load .w3m file (which is itself an MPQ archive) */
    started = R_W3MapStageBegin("archive_read");
    mapSize = ri.FS_ReadFile(mapFilename, (void **)&mapData);
    R_W3MapStageEnd("archive_read", started);
    if (mapSize < 0 || !mapData) {
        ri.error("R_RegisterMap: failed to open map %s\n", mapFilename);
        return;
    }
    
    /* Open the .w3m as a nested MPQ archive to read internal files */
    if (!SFileOpenArchiveFromMemory(mapData, (uint32_t)mapSize, 0, &hMpq)) {
        ri.FS_FreeFile(mapData);
        ri.error("R_RegisterMap: failed to open map archive %s\n", mapFilename);
        return;
    }
    started = R_W3MapStageBegin("archive_parse");
    map = FileReadWar3Map(hMpq);
    R_W3MapStageEnd("archive_parse", started);
    if (!map) {
        SFileCloseArchive(hMpq);
        ri.FS_FreeFile(mapData);
        ri.error("R_RegisterMap: failed to parse map %s\n", mapFilename);
        return;
    }
    fprintf(stderr, "WC3_MAP_LOAD phase=archive_parse dimensions=%ux%u grounds=%u\n",
            (unsigned)map->width, (unsigned)map->height, (unsigned)map->num_grounds);
    started = R_W3MapStageBegin("shadow_map");
    R_FileReadShadowMap(hMpq, map);
    R_W3MapStageEnd("shadow_map", started);
    started = R_W3MapStageBegin("minimap");
    R_LoadMapMinimap(hMpq, mapFilename);
    R_W3MapStageEnd("minimap", started);
    SFileCloseArchive(hMpq);
    ri.FS_FreeFile(mapData);
    tr.world = map;
    started = R_W3MapStageBegin("camera_height_map_and_blight");
    R_LoadBlightTexture(map->tileset);
    R_BuildCameraHeightMap(&(cameraHeightBuild_t){ .map = &w3_camera_height, .data = map,
        .width = map->width, .height_count = map->height, .radius = WC3_CAMERA_HEIGHT_RADIUS,
        .samples = BZ_BROAD_HEIGHT_SAMPLES, .origin = map->center, .cell_size = TILE_SIZE,
        .get_height = r_w3_camera_grid_height });
    R_W3MapStageEnd("camera_height_map_and_blight", started);

    started = R_W3MapStageBegin("terrain_segments");
    R_LoadMapSegments(map);
    R_W3MapStageEnd("terrain_segments", started);
    started = R_W3MapStageBegin("cliff_finish");
    R_FinishCliffs();
    R_W3MapStageEnd("cliff_finish", started);
    R_W3MapStageEnd("map_register", w3_map_load_started);
}

float R_W3CameraHeightAtPoint(float x, float y) { return R_SampleCameraHeightMap(&w3_camera_height, x, y); }

/* Sample the exact world terrain source used to build the client camera height map. */
float R_W3TerrainHeightAtPoint(float x, float y) {
    float gx, gy, tx, ty, h0, h1;
    uint32_t x0, y0, x1, y1;

    if (!tr.world || !tr.world->vertices || !tr.world->width || !tr.world->height) return 0.0f;
    gx = (x - tr.world->center.x) / TILE_SIZE;
    gy = (y - tr.world->center.y) / TILE_SIZE;
    gx = MAX(0.0f, MIN((float)tr.world->width - 1.0f, gx));
    gy = MAX(0.0f, MIN((float)tr.world->height - 1.0f, gy));
    x0 = (uint32_t)floorf(gx); y0 = (uint32_t)floorf(gy);
    x1 = MIN(tr.world->width - 1, x0 + 1); y1 = MIN(tr.world->height - 1, y0 + 1);
    tx = gx - x0; ty = gy - y0;
    h0 = LerpNumber(GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x0, y0)),
                    GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x1, y0)), tx);
    h1 = LerpNumber(GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x0, y1)),
                    GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x1, y1)), tx);
    return LerpNumber(h0, h1, ty);
}

void _W3M_DrawTerrainShadows(void) {
    if (!tr.world || !tr.texture[TEX_TERRAIN_SHADOW] || (tr.viewDef.rdflags & RDF_NOWORLDMODEL)) {
        return;
    }

    vec2_t size = GetWar3MapSize(tr.world);
    vec2_t mins = tr.world->center;
    vec2_t maxs = {
        .x = tr.world->center.x + size.x,
        .y = tr.world->center.y + size.y,
    };
    color32_t shadowColor = {0, 0, 0, 255};

    R_RenderRectSplat(&mins, &maxs, tr.texture[TEX_TERRAIN_SHADOW], R_SPLAT_SHADER(&tr.shader_shadowSplat), shadowColor);
}

/* Copy the server-authored scene fog into the terrain shader before each pass. */
static void _W3M_SetSceneFog(void) {
    tr.shader_default.state.fogEnable = tr.viewDef.fogEnable;
    tr.shader_default.state.fogColor = tr.viewDef.fogColor;
    tr.shader_default.state.fogParams = (vec2_t){ tr.viewDef.fogStart, tr.viewDef.fogEnd };
}

void _W3M_DrawWorld(void) {
    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL)
        return;

    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    _W3M_SetSceneFog();

    {
        modelLighting_t lighting;
        R_SetDefaultLighting(&tr.shader_default,
                             R_LightingFromEnviron(&tr.viewDef.terrainLight, &lighting)
                                 ? &lighting : NULL);
    }

    /* Ground travels with its segment so the view frustum culls tiles off-screen;
     * a whole-map buffer submits the entire board every frame. */
    FOR_EACH_LIST(mapsegment_t, segment, g_mapSegments) {
        R_DrawTerrainSegment(segment, (1 << MAPLAYERTYPE_GROUND) | (1 << MAPLAYERTYPE_CLIFF));
    }

    R_UpdateBlightLayer();
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_DrawBlightLayer();
}

void _W3M_DrawAlphaSurfaces(void) {
    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL)
        return;

    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    _W3M_SetSceneFog();

    FOR_EACH_LIST(mapsegment_t, segment, g_mapSegments) {
        R_DrawTerrainSegment(segment, (1 << MAPLAYERTYPE_WATER));
    }
    R_Call(glDepthMask, GL_TRUE);
}
