#ifndef g_camera_h
#define g_camera_h

/* g_camera.h — shared camera state helpers.
 *
 * The JASS and Lua camera natives must drive the same client camera state with
 * the same authored-angle and FOV conventions. These helpers live here so both
 * api_camera.h and lua_api.c include one implementation instead of duplicating
 * the conversion math.
 *
 * Requires game/g_local.h and jass/jass.h (CAMERAFIELD) to be included first. */

#include <math.h>

extern player_t *currentplayer;

#define WC3_CAMERA_ASPECT 1.66f /* Warcraft camera horizontal/vertical FOV conversion aspect */

static inline gameClient_t *G_CurrentCameraClient(cstring_t func) {
    (void)func;
    if (!currentplayer) {
        return NULL;
    }
    return G_GetPlayerClientByNumber(PLAYER_NUM(currentplayer));
}

static inline float G_CameraHorizontalToVerticalFov(float horizontal) {
    float const hfov_rad = horizontal * (float)M_PI / 180.0f;
    return 2.0f * atanf(tanf(hfov_rad / 2.0f) / WC3_CAMERA_ASPECT) * 180.0f / (float)M_PI;
}

static inline float G_CameraVerticalToHorizontalFov(float vertical) {
    float const vfov_rad = vertical * (float)M_PI / 180.0f;
    return 2.0f * atanf(tanf(vfov_rad / 2.0f) * WC3_CAMERA_ASPECT) * 180.0f / (float)M_PI;
}

static inline float G_CameraDegreesToRadians(float value) { return value * (float)M_PI / 180.0f; }

/* Sample all interpolated camera fields so a new transition can rebase from the in-flight state. */
static inline camerasetup_t G_CameraStateAtTime(gameClient_t *gc, uint32_t now) {
    camerasetup_t current;
    uint32_t duration;
    float k;

    if (!gc) {
        return (camerasetup_t){ 0 };
    }
    duration = gc->camera.end_time - gc->camera.start_time;
    if (!duration || now >= gc->camera.end_time) {
        return gc->camera.state;
    }
    if (now <= gc->camera.start_time) {
        return gc->camera.old_state;
    }

    k = (now - gc->camera.start_time) / (float)duration;
    current.position = Vector2_lerp(&gc->camera.old_state.position, &gc->camera.state.position, k);
    current.viewangles = (vec3_t){
        CL_GameLerpDegrees(gc->camera.old_state.viewangles.x, gc->camera.state.viewangles.x, k),
        CL_GameLerpDegrees(gc->camera.old_state.viewangles.y, gc->camera.state.viewangles.y, k),
        CL_GameLerpDegrees(gc->camera.old_state.viewangles.z, gc->camera.state.viewangles.z, k),
    };
    current.target_distance = LerpNumber(gc->camera.old_state.target_distance, gc->camera.state.target_distance, k);
    current.fov = LerpNumber(gc->camera.old_state.fov, gc->camera.state.fov, k);
    current.z_offset = LerpNumber(gc->camera.old_state.z_offset, gc->camera.state.z_offset, k);
    current.near_z = LerpNumber(gc->camera.old_state.near_z, gc->camera.state.near_z, k);
    current.far_z = LerpNumber(gc->camera.old_state.far_z, gc->camera.state.far_z, k);
    return current;
}

/* Reconstruct the rendered orbit eye from the same target, orientation, and distance sent to the client. */
static inline vec3_t G_CameraEyePositionFromState(vec3_t const *target, vec3_t const *angles, float distance) {
    quaternion_t quat;
    mat4_t view, inverse;
    vec3_t eye, origin = Vector3_unm(target);

    quat = Quaternion_fromEuler(angles, ROTATE_ZYX);
    Matrix4_identity(&view);
    Matrix4_translate(&view, &(vec3_t){ 0, 0, -distance });
    Matrix4_rotateQuat(&view, &quat);
    Matrix4_translate(&view, &origin);
    Matrix4_inverse(&view, &inverse);
    eye = (vec3_t){ inverse.v[12], inverse.v[13], inverse.v[14] };
    return eye;
}
static inline vec3_t G_CameraEyePosition(player_t const *playerstate) {
    return G_CameraEyePositionFromState(&playerstate->vieworigin, &playerstate->viewangles, playerstate->distance);
}

/* Mirror low authored AoA values above the target while preserving their world-facing orbit. */
static inline float G_CameraAuthoredToPitch(float value) { return value < 90 ? 90 + value : -90 - value; }
static inline float G_CameraPitchToAuthored(float value) {
    return value >= 90 ? value - 90 : value >= 0 ? 90 - value : -90 - value;
}
static inline float G_CameraAuthoredToYaw(float value, float pitch) { return pitch < 0 ? 450 - value : 270 - value; }
static inline float G_CameraYawToAuthored(float value, float pitch) { return pitch < 0 ? 450 - value : 270 - value; }
/* Convert the client Euler yaw back to Warcraft's authored rotation field. */
static inline float G_CameraRotation(player_t const *p) { return G_CameraYawToAuthored(p->viewangles.z, p->viewangles.x); }

/* Read one sampled camera field in the authored units consumed by camera setters. */
static inline float G_GetCameraStateField(camerasetup_t const *camera, CAMERAFIELD field) {
    if (!camera) {
        return 0.0f;
    }
    switch (field) {
        case CAMERA_FIELD_TARGET_DISTANCE: return camera->target_distance;
        case CAMERA_FIELD_FARZ: return camera->far_z;
        case CAMERA_FIELD_NEARZ: return camera->near_z;
        case CAMERA_FIELD_ANGLE_OF_ATTACK: return G_CameraPitchToAuthored(camera->viewangles.x);
        case CAMERA_FIELD_FIELD_OF_VIEW: return G_CameraVerticalToHorizontalFov(camera->fov);
        case CAMERA_FIELD_ROLL: return camera->viewangles.y;
        case CAMERA_FIELD_ROTATION: return G_CameraYawToAuthored(camera->viewangles.z, camera->viewangles.x);
        case CAMERA_FIELD_ZOFFSET: return camera->z_offset;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            return 0.0f;
    }
    return 0.0f;
}

/* Write one authored camera field while preserving the setup's shared angle convention. */
static inline bool G_SetCameraStateField(camerasetup_t *camera, CAMERAFIELD field, float value) {
    if (!camera) {
        return false;
    }
    switch (field) {
        case CAMERA_FIELD_TARGET_DISTANCE: camera->target_distance = value; break;
        case CAMERA_FIELD_FARZ: camera->far_z = value; break;
        case CAMERA_FIELD_NEARZ: camera->near_z = value; break;
        case CAMERA_FIELD_ANGLE_OF_ATTACK: {
            float rotation = G_CameraYawToAuthored(camera->viewangles.z, camera->viewangles.x);
            camera->viewangles.x = G_CameraAuthoredToPitch(value);
            camera->viewangles.z = G_CameraAuthoredToYaw(rotation, camera->viewangles.x);
            break;
        }
        case CAMERA_FIELD_FIELD_OF_VIEW: camera->fov = G_CameraHorizontalToVerticalFov(value); break;
        case CAMERA_FIELD_ROLL: camera->viewangles.y = value; break;
        case CAMERA_FIELD_ROTATION: camera->viewangles.z = G_CameraAuthoredToYaw(value, camera->viewangles.x); break;
        case CAMERA_FIELD_ZOFFSET: camera->z_offset = value; break;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            /* TODO: Implement local fields once client-local camera state has an owner. */
            fprintf(stderr, "WC3: unsupported camera field %d\n", field);
            return false;
        default:
            fprintf(stderr, "WC3: unsupported camera field %d\n", field);
            return false;
    }
    return true;
}

/* Start a scalar field transition from the current in-flight setup for the current player. */
static inline void G_SetCameraFieldForCurrentPlayer(CAMERAFIELD field, float value, float duration) {
    gameClient_t *gc = G_CurrentCameraClient("G_SetCameraFieldForCurrentPlayer");
    camerasetup_t current, target;
    uint32_t now;

    if (!gc) {
        return;
    }
    if (G_SkipCutscene()) {
        duration = 0.0f;
    }
    now = G_Time();
    current = G_CameraStateAtTime(gc, now);
    target = current;
    if (!G_SetCameraStateField(&target, field, value)) {
        return;
    }
    gc->camera.old_state = current;
    gc->camera.state = target;
    gc->camera.start_time = now;
    gc->camera.end_time = now + (uint32_t)(MAX(0.0f, duration) * 1000.0f);
}

/* Recover the authored target offset from the terrain-composed runtime target height. */
static inline float G_CameraZOffset(player_t const *p) {
    gameClient_t *gc = G_CurrentCameraClient("G_CameraZOffset");
    return gc ? p->vieworigin.z - gc->camera.target_height
              : p->vieworigin.z - CM_GetHeightAtPoint(p->vieworigin.x, p->vieworigin.y) - CM_GetCameraHeightOffset();
}

/* Pan the current player's camera target, optionally overriding the Z offset. */
static inline void G_SetCameraPositionForCurrentPlayer(cstring_t func, float x, float y,
                                                bool set_z, float z_offset, float duration) {
    gameClient_t *gc = G_CurrentCameraClient(func);
    vec2_t position = { x, y };

    if (!gc) {
        return;
    }
    if (G_SkipCutscene()) {
        duration = 0;
    }
    position = G_ClampCameraPosition(gc, &position);
    G_ClearCameraTarget(gc, func);
    gc->camera.old_state = gc->camera.state;
    gc->camera.target_height = gc->ps.vieworigin.z;
    gc->camera.state.position = position;
    if (set_z) {
        gc->camera.state.z_offset = z_offset;
    }
    gc->camera.start_time = G_Time();
    gc->camera.end_time = gc->camera.start_time + duration * 1000;
}

/* CameraSetup Apply variants share one state transition. The plain Apply
 * still has no retained per-field duration, but WithZ must override the setup's
 * authored Z offset exactly like Warsmash's setTargetZOffset path. */
static inline void G_ApplyCameraSetup(camerasetup_t *setup, bool apply_position,
                               bool override_z, float z_offset, float duration_ms) {
    gameClient_t *gc = G_CurrentCameraClient("CameraSetupApply");
    if (!gc || !setup) {
        return;
    }
    if (G_SkipCutscene()) {
        duration_ms = 0;
    }
    G_ClearCameraTarget(gc, "CameraSetupApply");
    gc->camera.old_state = gc->camera.state;
    if (apply_position && (setup->position.x != gc->camera.old_state.position.x ||
                           setup->position.y != gc->camera.old_state.position.y)) {
        gc->camera.target_height = CM_GetHeightAtPoint(setup->position.x, setup->position.y);
    }
    gc->camera.state = *setup;
    if (!apply_position) {
        gc->camera.state.position = gc->camera.old_state.position;
    }
    /* Retail applies the setup's authored Z offset for ordinary CameraSetupApply*;
     * only the WithZ variants replace it with their explicit argument. */
    if (override_z) {
        gc->camera.state.z_offset = z_offset;
    }
    gc->camera.state.position = G_ClampCameraPosition(gc, &gc->camera.state.position);
    gc->camera.start_time = G_Time();
    gc->camera.end_time = gc->camera.start_time + duration_ms;
}

#endif
