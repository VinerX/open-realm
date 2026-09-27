#define API_PLAYERSTATE(NAME) \
jassContext_t const *NAME##Context = jass_getcontext(j); \
player_t *NAME = NAME##Context && NAME##Context->unit ? G_GetPlayerByNumber(NAME##Context->unit->s.player) : currentplayer;

#include "game/g_camera.h"

#ifdef WC3_DEBUG_CAMERA_TRACE
/* Emit opt-in camera samples for retail/OpenRealm comparisons without changing map JASS. */
void G_CameraTraceSnapshotForClient(gameClient_t *gc, cstring_t label) {
    player_t const *p;
    camerasetup_t *s;
    vec3_t const *ang;
    float dist, fov, roll, zoff, farz;
    float terrain, sample_height, realized_base, composed_z;
    float k = 0.0f;
    vec3_t eye, realized_target;
    static uint32_t sample;
    cstring_t enabled = gi.CvarString("camera_trace", "0");

    if (!enabled || !*enabled || !strcmp(enabled, "0")) return;
    if (!gc) return;
    p = &gc->ps;
    s = &gc->camera.state;
    terrain = CM_GetHeightAtPoint(p->vieworigin.x, p->vieworigin.y);
    sample_height = terrain;
    if (gc->camera.end_time > G_Time() && G_Time() != gc->camera.start_time) {
        k = (G_Time() - gc->camera.start_time) /
            (float)(gc->camera.end_time - gc->camera.start_time);
        /* Retail keeps the realized eye on the old setup while logical fields expose the new setup. */
        realized_target = p->vieworigin;
        eye = G_CameraEyePositionFromState(&realized_target, &gc->camera.old_state.viewangles,
                                           gc->camera.old_state.target_distance);
        ang = &p->viewangles;
        dist = p->distance;
        fov = p->fov;
        roll = p->viewangles.y;
        /* Report the logical interpolated offset; deriving it from vieworigin
         * uses the stale target height while a transition is in progress. */
        zoff = LerpNumber(gc->camera.old_state.z_offset, gc->camera.state.z_offset, k);
        farz = p->zfar;
    } else {
        if (gc->camera.end_time > gc->camera.start_time && G_Time() < gc->camera.end_time)
            s = &gc->camera.old_state;
        ang = &s->viewangles;
        dist = s->target_distance;
        fov = s->fov;
        roll = s->viewangles.y;
        zoff = s->z_offset;
        farz = s->far_z;
        realized_target = p->vieworigin;
        eye = G_CameraEyePositionFromState(&realized_target, &p->viewangles, p->distance);
    }
    realized_base = p->vieworigin.z - zoff;
    composed_z = gc->camera.target_height + zoff;
    /* Retail exposes newly applied setup fields immediately, while its eye and
     * target getters continue to report the realized camera until the next
     * client update. Keep both halves of that contract in the trace. */
    fprintf(stderr,
            "CAMTRACE n=%u t=%.3f label=%s tx=%.3f ty=%.3f tz=%.3f ex=%.3f ey=%.3f ez=%.3f dist=%.3f aoa=%.3f rot=%.3f fov=%.3f roll=%.3f zoff=%.3f farz=%.3f terrain=%.3f sampleheight=%.3f targetbase=%.3f realizedbase=%.3f composedz=%.3f setupx=%.3f setupy=%.3f k=%.3f\n",
            (unsigned)++sample, G_Time() / 1000.0f, label ? label : "camera-event",
            p->vieworigin.x, p->vieworigin.y, p->vieworigin.z, eye.x, eye.y, eye.z,
            dist, G_CameraDegreesToRadians(G_CameraPitchToAuthored(ang->x)),
            G_CameraDegreesToRadians(G_CameraYawToAuthored(ang->z, ang->x)),
            G_CameraDegreesToRadians(G_CameraVerticalToHorizontalFov(fov)),
            G_CameraDegreesToRadians(roll), zoff, farz, terrain, sample_height, gc->camera.target_height,
            realized_base, composed_z, gc->camera.state.position.x, gc->camera.state.position.y, k);
}

void G_CameraTraceSnapshot(cstring_t label) {
    G_CameraTraceSnapshotForClient(G_CurrentCameraClient("G_CameraTraceSnapshot"), label);
}
#endif

uint32_t SetCameraTargetController(jass_t *j) {
    edict_t *whichUnit = jass_checkhandle(j, 1, "unit");
    float xoffset = jass_checknumber(j, 2);
    float yoffset = jass_checknumber(j, 3);
    bool inheritOrientation = jass_checkboolean(j, 4);
    gameClient_t *gc = G_CurrentCameraClient("SetCameraTargetController");
    if (!gc) {
        return 0;
    }
    gc->camera.target_controller = whichUnit;
    gc->camera.target_offset = (vec2_t){ xoffset, yoffset };
    gc->camera.target_inherit_orientation = inheritOrientation;
    if (whichUnit) {
        vec2_t position = { whichUnit->s.origin2.x + xoffset, whichUnit->s.origin2.y + yoffset };
        gc->camera.old_state = gc->camera.state;
        gc->camera.state.position = G_ClampCameraPosition(gc, &position);
        if (inheritOrientation) {
            gc->camera.old_state.viewangles.z = 90.0f - (float)RAD2DEG(whichUnit->s.angle);
            gc->camera.state.viewangles.z = 90.0f - (float)RAD2DEG(whichUnit->s.angle);
        }
        gc->camera.start_time = G_Time();
        gc->camera.end_time = gc->camera.start_time;
    } else {
        gc->camera.target_offset = (vec2_t){ 0, 0 };
        gc->camera.target_inherit_orientation = false;
    }
    return 0;
}
uint32_t SetCameraOrientController(jass_t *j) {
    //edict_t *whichUnit = jass_checkhandle(j, 1, "unit");
    //float xoffset = jass_checknumber(j, 2);
    //float yoffset = jass_checknumber(j, 3);
    return 0;
}
uint32_t SetCameraPosition(jass_t *j) {
    float x = jass_checknumber(j, 1);
    float y = jass_checknumber(j, 2);
    G_SetCameraPositionForCurrentPlayer("SetCameraPosition", x, y, false, 0.0f, 0);
    return 0;
}
uint32_t SetCameraQuickPosition(jass_t *j) {
    float x = jass_checknumber(j, 1);
    float y = jass_checknumber(j, 2);
    gameClient_t *gc = G_CurrentCameraClient("SetCameraQuickPosition");
    if (!gc) {
        return 0;
    }
    /* Warcraft's quick position is the spacebar recall point. It must not
     * mutate the current camera target when the script assigns it. */
    gc->camera.quick_position = MAKE(vec2_t, x, y);
    gc->camera.quick_position_set = true;
    return 0;
}
uint32_t SetCameraBounds(jass_t *j) {
    float bounds[8];

    FOR_LOOP(i, 8) {
        bounds[i] = jass_checknumber(j, i + 1);
    }
    G_SetCameraBounds(bounds);
    return 0;
}
/* Freeze the current timed camera transition at its sampled state. */
uint32_t StopCamera(jass_t *j) {
    gameClient_t *gc = G_CurrentCameraClient("StopCamera");
    uint32_t now;

    (void)j;
    if (!gc) {
        return 0;
    }
    now = G_Time();
    gc->camera.state = G_CameraStateAtTime(gc, now);
    gc->camera.old_state = gc->camera.state;
    gc->camera.start_time = gc->camera.end_time = now;
    return 0;
}
uint32_t ResetToGameCamera(jass_t *j) {
    float duration = jass_checknumber(j, 1);
    gameClient_t *gc = G_CurrentCameraClient("ResetToGameCamera");
    if (!gc) {
        return 0;
    }
    if (G_SkipCutscene()) {
        duration = 0;
    }
    G_ClearCameraTarget(gc, "ResetToGameCamera");
    gc->camera.old_state = gc->camera.state;
    {
        gameCamera_t cam;
        CL_GameDefaultCamera(&cam);
        gc->camera.state.viewangles = (vec3_t){ cam.pitch, 0, cam.yaw };
        gc->camera.state.fov = cam.fov;
        gc->camera.state.target_distance = cam.distance;
        gc->camera.state.z_offset = 0.0f;
        gc->camera.state.near_z = cam.znear;
        gc->camera.state.far_z = cam.zfar;
    }
    gc->camera.start_time = G_Time();
    gc->camera.end_time = gc->camera.start_time + (duration * 1000);
    return 0;
}
uint32_t PanCameraTo(jass_t *j) {
    float x = jass_checknumber(j, 1);
    float y = jass_checknumber(j, 2);
    G_SetCameraPositionForCurrentPlayer("PanCameraTo", x, y, false, 0.0f, 0);
    return 0;
}
uint32_t PanCameraToTimed(jass_t *j) {
    float x = jass_checknumber(j, 1);
    float y = jass_checknumber(j, 2);
    float duration = jass_checknumber(j, 3);
    G_SetCameraPositionForCurrentPlayer("PanCameraToTimed", x, y, false, 0.0f, duration);
    return 0;
}
uint32_t PanCameraToWithZ(jass_t *j) {
    float x = jass_checknumber(j, 1);
    float y = jass_checknumber(j, 2);
    float zOffsetDest = jass_checknumber(j, 3);
    G_SetCameraPositionForCurrentPlayer("PanCameraToWithZ", x, y, true, zOffsetDest, 0);
    return 0;
}
uint32_t PanCameraToTimedWithZ(jass_t *j) {
    float x = jass_checknumber(j, 1);
    float y = jass_checknumber(j, 2);
    float zOffsetDest = jass_checknumber(j, 3);
    float duration = jass_checknumber(j, 4);
    G_SetCameraPositionForCurrentPlayer("PanCameraToTimedWithZ", x, y, true, zOffsetDest, duration);
    return 0;
}
uint32_t SetCinematicCamera(jass_t *j) {
    //cstring_t cameraModelFile = jass_checkstring(j, 1);
    return 0;
}
uint32_t SetCameraField(jass_t *j) {
    CAMERAFIELD *whichField = jass_checkhandle(j, 1, "camerafield");
    float value = jass_checknumber(j, 2);
    float duration = jass_checknumber(j, 3);

    if (whichField) {
        G_SetCameraFieldForCurrentPlayer(*whichField, value, duration);
    }
    return 0;
}
uint32_t AdjustCameraField(jass_t *j) {
    CAMERAFIELD *whichField = jass_checkhandle(j, 1, "camerafield");
    float offset = jass_checknumber(j, 2);
    float duration = jass_checknumber(j, 3);
    gameClient_t *gc = G_CurrentCameraClient("AdjustCameraField");

    if (gc && whichField) {
        camerasetup_t current = G_CameraStateAtTime(gc, G_Time());
        G_SetCameraFieldForCurrentPlayer(*whichField, G_GetCameraStateField(&current, *whichField) + offset, duration);
    }
    return 0;
}
uint32_t CreateCameraSetup(jass_t *j) {
    API_ALLOC(camerasetup_t, camerasetup);
    {
        gameCamera_t cam;
        CL_GameDefaultCamera(&cam);
        camerasetup->viewangles = (vec3_t){ cam.pitch, 0, cam.yaw };
        camerasetup->fov = cam.fov;
        camerasetup->target_distance = cam.distance;
        camerasetup->near_z = cam.znear;
        camerasetup->far_z = cam.zfar;
    }
    return 1;
}
uint32_t CameraSetupSetField(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    CAMERAFIELD *whichField = jass_checkhandle(j, 2, "camerafield");
    float value = jass_checknumber(j, 3);
    switch (*whichField) {
        case CAMERA_FIELD_TARGET_DISTANCE: whichSetup->target_distance = value; break;
        case CAMERA_FIELD_FARZ: whichSetup->far_z = value; break;
        case CAMERA_FIELD_NEARZ: whichSetup->near_z = value; break;
        case CAMERA_FIELD_ANGLE_OF_ATTACK: {
            float rotation = G_CameraYawToAuthored(whichSetup->viewangles.z, whichSetup->viewangles.x);
            whichSetup->viewangles.x = G_CameraAuthoredToPitch(value);
            whichSetup->viewangles.z = G_CameraAuthoredToYaw(rotation, whichSetup->viewangles.x);
            break;
        }
        case CAMERA_FIELD_FIELD_OF_VIEW: whichSetup->fov = G_CameraHorizontalToVerticalFov(value); break;
        case CAMERA_FIELD_ROLL: whichSetup->viewangles.y = value; break;
        case CAMERA_FIELD_ROTATION: whichSetup->viewangles.z = G_CameraAuthoredToYaw(value, whichSetup->viewangles.x); break;
        case CAMERA_FIELD_ZOFFSET: whichSetup->z_offset = value; break;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            break;
    }
//    float duration = jass_checknumber(j, 4);
    return 0;
}
uint32_t CameraSetupGetField(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    uint32_t *whichField = jass_checkhandle(j, 2, "camerafield");
    float value = 0;
    switch (*whichField) {
        case CAMERA_FIELD_TARGET_DISTANCE: value = whichSetup->target_distance; break;
        case CAMERA_FIELD_FARZ: value = whichSetup->far_z; break;
        case CAMERA_FIELD_NEARZ: value = whichSetup->near_z; break;
        case CAMERA_FIELD_ANGLE_OF_ATTACK: value = G_CameraPitchToAuthored(whichSetup->viewangles.x); break;
        case CAMERA_FIELD_FIELD_OF_VIEW: value = G_CameraVerticalToHorizontalFov(whichSetup->fov); break;
        case CAMERA_FIELD_ROLL: value = whichSetup->viewangles.y; break;
        case CAMERA_FIELD_ROTATION: value = G_CameraYawToAuthored(whichSetup->viewangles.z, whichSetup->viewangles.x); break;
        case CAMERA_FIELD_ZOFFSET: value = whichSetup->z_offset; break;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            break;
    }
    return jass_pushnumber(j, value);
}
uint32_t CameraSetupSetDestPosition(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
//    float duration = jass_checknumber(j, 4);
    whichSetup->position.x = x;
    whichSetup->position.y = y;
    return 0;
}
uint32_t CameraSetupGetDestPositionLoc(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    return jass_pushlighthandle(j, &whichSetup->position, "location");
}
uint32_t CameraSetupGetDestPositionX(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    return jass_pushnumber(j, whichSetup->position.x);
}
uint32_t CameraSetupGetDestPositionY(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    return jass_pushnumber(j, whichSetup->position.y);
}
uint32_t CameraSetupApply(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    bool doPan = jass_checkboolean(j, 2);
    (void)jass_checkboolean(j, 3); /* panTimed: untimed camera rates are not retained yet */
    G_ApplyCameraSetup(whichSetup, doPan, false, 0.0f, 0);
#ifdef WC3_DEBUG_CAMERA_TRACE
    G_CameraTraceSnapshot("CameraSetupApply");
#endif
    return 0;
}
uint32_t CameraSetupApplyWithZ(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    float zDestOffset = jass_checknumber(j, 2);
    G_ApplyCameraSetup(whichSetup, true, true, zDestOffset, 0);
#ifdef WC3_DEBUG_CAMERA_TRACE
    G_CameraTraceSnapshot("CameraSetupApplyWithZ");
#endif
    return 0;
}
uint32_t CameraSetupApplyForceDuration(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    bool doPan = jass_checkboolean(j, 2);
    float forceDuration = jass_checknumber(j, 3);
    G_ApplyCameraSetup(whichSetup, doPan, false, 0.0f, forceDuration * 1000);
#ifdef WC3_DEBUG_CAMERA_TRACE
    G_CameraTraceSnapshot("CameraSetupApplyForceDuration");
#endif
    return 0;
}
uint32_t CameraSetupApplyForceDurationWithZ(jass_t *j) {
    camerasetup_t *whichSetup = jass_checkhandle(j, 1, "camerasetup");
    float zDestOffset = jass_checknumber(j, 2);
    float forceDuration = jass_checknumber(j, 3);
    G_ApplyCameraSetup(whichSetup, true, true, zDestOffset, forceDuration * 1000);
#ifdef WC3_DEBUG_CAMERA_TRACE
    G_CameraTraceSnapshot("CameraSetupApplyForceDurationWithZ");
#endif
    return 0;
}
uint32_t CameraSetTargetNoise(jass_t *j) {
    //float mag = jass_checknumber(j, 1);
    //float velocity = jass_checknumber(j, 2);
    return 0;
}
uint32_t CameraSetSourceNoise(jass_t *j) {
    //float mag = jass_checknumber(j, 1);
    //float velocity = jass_checknumber(j, 2);
    return 0;
}
uint32_t CameraSetSmoothingFactor(jass_t *j) {
    //float factor = jass_checknumber(j, 1);
    return 0;
}
uint32_t GetCameraMargin(jass_t *j) {
    int32_t whichMargin = jass_checkinteger(j, 1);
    float margin;
    if (G_GetCameraMargin(whichMargin, &margin)) jass_pushnumber(j, margin);
    else jass_pushnull(j);
    return 1;
}
uint32_t GetCameraBoundMinX(jass_t *j) {
    return jass_pushnumber(j, level.camera_bounds.min.x);
}
uint32_t GetCameraBoundMinY(jass_t *j) {
    return jass_pushnumber(j, level.camera_bounds.min.y);
}
uint32_t GetCameraBoundMaxX(jass_t *j) {
    return jass_pushnumber(j, level.camera_bounds.max.x);
}
uint32_t GetCameraBoundMaxY(jass_t *j) {
    return jass_pushnumber(j, level.camera_bounds.max.y);
}
uint32_t GetCameraField(jass_t *j) {
    handle_t whichField = jass_checkhandle(j, 1, "camerafield");
    API_PLAYERSTATE(playerstate);
    float value = 0;
    if (playerstate && whichField) switch (*(CAMERAFIELD *)whichField) {
        case CAMERA_FIELD_TARGET_DISTANCE: value = playerstate->distance; break;
        case CAMERA_FIELD_FARZ: value = playerstate->zfar; break;
        case CAMERA_FIELD_NEARZ: value = playerstate->znear; break;
        /* Warcraft's field getters expose angles and FOV in radians; runtime state stores degrees. */
        case CAMERA_FIELD_ANGLE_OF_ATTACK:
            value = G_CameraDegreesToRadians(G_CameraPitchToAuthored(playerstate->viewangles.x)); break;
        case CAMERA_FIELD_FIELD_OF_VIEW:
            value = G_CameraDegreesToRadians(G_CameraVerticalToHorizontalFov(playerstate->fov)); break;
        case CAMERA_FIELD_ROLL: value = G_CameraDegreesToRadians(playerstate->viewangles.y); break;
        case CAMERA_FIELD_ROTATION: value = G_CameraDegreesToRadians(G_CameraRotation(playerstate)); break;
        case CAMERA_FIELD_ZOFFSET: value = G_CameraZOffset(playerstate); break;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            break;
    }
    return jass_pushnumber(j, value);
}
uint32_t GetCameraTargetPositionX(jass_t *j) {
    API_PLAYERSTATE(playerstate);
    return jass_pushnumber(j, playerstate ? playerstate->vieworigin.x : 0);
}
uint32_t GetCameraTargetPositionY(jass_t *j) {
    API_PLAYERSTATE(playerstate);
    return jass_pushnumber(j, playerstate ? playerstate->vieworigin.y : 0);
}
uint32_t GetCameraTargetPositionZ(jass_t *j) {
    API_PLAYERSTATE(playerstate);
    return jass_pushnumber(j, playerstate ? playerstate->vieworigin.z : 0);
}

uint32_t GetCameraTargetPositionLoc(jass_t *j) {
    API_ALLOC(vec2_t, location);
    API_PLAYERSTATE(playerstate);
    if (playerstate) {
        *location = (vec2_t){ playerstate->vieworigin.x, playerstate->vieworigin.y };
    }
    return 1;
}
uint32_t GetCameraEyePositionX(jass_t *j) {
    API_PLAYERSTATE(playerstate);
    vec3_t eye = playerstate ? G_CameraEyePosition(playerstate) : (vec3_t){ 0 };
    return jass_pushnumber(j, eye.x);
}
uint32_t GetCameraEyePositionY(jass_t *j) {
    API_PLAYERSTATE(playerstate);
    vec3_t eye = playerstate ? G_CameraEyePosition(playerstate) : (vec3_t){ 0 };
    return jass_pushnumber(j, eye.y);
}
uint32_t GetCameraEyePositionZ(jass_t *j) {
    API_PLAYERSTATE(playerstate);
    vec3_t eye = playerstate ? G_CameraEyePosition(playerstate) : (vec3_t){ 0 };
    return jass_pushnumber(j, eye.z);
}
uint32_t GetCameraEyePositionLoc(jass_t *j) {
    API_ALLOC(vec2_t, location);
    API_PLAYERSTATE(playerstate);
    if (playerstate) {
        vec3_t eye = G_CameraEyePosition(playerstate);
        *location = (vec2_t){ eye.x, eye.y };
    }
    return 1;
}
