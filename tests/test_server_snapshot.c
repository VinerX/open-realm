#include "test.h"
#include "server/server.h"
#include <stdlib.h>

struct server sv;
struct server_static svs;
struct game_export *ge;
static entityState_t states[16];
void SV_EmitPacketEntities(clientFrame_t const *from, clientFrame_t const *to, sizeBuf_t *msg);

int Cvar_Integer(cstring_t name, int fallback) {
    (void)name;
    return fallback;
}

void MemFree(handle_t mem) { free(mem); }

TEST(server_snapshot, snapshot_delta_handles_full_entity_number_range) {
    uint32_t const numbers[] = { 9998, 9999, 10000, 32768, MAX_GAME_ENTITIES - 1 };
    uint8_t data[4096];
    sizeBuf_t msg = { .data = data, .maxsize = sizeof(data) };
    clientFrame_t frame = { .num_entities = sizeof(numbers) / sizeof(*numbers) };

    memset(&sv, 0, sizeof(sv));
    svs.client_entities = states;
    svs.num_client_entities = sizeof(states) / sizeof(*states);
    FOR_LOOP(i, frame.num_entities) {
        svs.client_entities[i] = (entityState_t){ .number = numbers[i], .model = 1 };
    }

    SV_EmitPacketEntities(NULL, &frame, &msg);
    T_ASSERT(!msg.overflowed);
    T_EQ(MSG_ReadByte(&msg), svc_packetentities);
    FOR_LOOP(i, frame.num_entities) {
        entityState_t entity = { 0 };
        uint32_t bits, number = MSG_ReadEntityBits(&msg, &bits);
        T_EQ(number, numbers[i]);
        T_ASSERT(!(bits & (1u << U_REMOVE)));
        MSG_ReadDeltaEntity(&msg, &entity, number, bits);
        T_EQ(entity.model, 1);
    }
    uint32_t bits;
    T_EQ(MSG_ReadEntityBits(&msg, &bits), 0); T_EQ(bits, 0);
    T_EQ(msg.readcount, msg.cursize);

    msg.cursize = msg.readcount = 0;
    SV_EmitPacketEntities(&frame, &frame, &msg);
    T_ASSERT(!msg.overflowed);
    T_EQ(MSG_ReadByte(&msg), svc_packetentities);
    T_EQ(MSG_ReadEntityBits(&msg, &bits), 0); T_EQ(bits, 0);
    T_EQ(msg.readcount, msg.cursize);

    clientFrame_t empty = { 0 };
    msg.cursize = msg.readcount = 0;
    SV_EmitPacketEntities(&frame, &empty, &msg);
    T_ASSERT(!msg.overflowed);
    T_EQ(MSG_ReadByte(&msg), svc_packetentities);
    FOR_LOOP(i, frame.num_entities) {
        T_EQ(MSG_ReadEntityBits(&msg, &bits), numbers[i]);
        T_EQ(bits, 1u << U_REMOVE);
    }
    T_EQ(MSG_ReadEntityBits(&msg, &bits), 0); T_EQ(bits, 0);
    T_EQ(msg.readcount, msg.cursize);
}

TEST(server_snapshot, snapshot_delta_merges_high_number_additions_changes_and_removals) {
    uint8_t data[4096];
    sizeBuf_t msg = { .data = data, .maxsize = sizeof(data) };
    clientFrame_t before = { .num_entities = 3 };
    clientFrame_t after = { .first_entity = 3, .num_entities = 3 };
    uint32_t bits, number;
    entityState_t entity;

    memset(&sv, 0, sizeof(sv));
    svs.client_entities = states;
    svs.num_client_entities = sizeof(states) / sizeof(*states);
    states[0] = (entityState_t){ .number = 9999, .model = 1 };
    states[1] = (entityState_t){ .number = 32768, .model = 1 };
    states[2] = (entityState_t){ .number = MAX_GAME_ENTITIES - 1, .model = 1 };
    states[3] = (entityState_t){ .number = 10000, .model = 2 };
    states[4] = (entityState_t){ .number = 32768, .model = 2 };
    states[5] = states[2];

    SV_EmitPacketEntities(&before, &after, &msg);
    T_ASSERT(!msg.overflowed);
    T_EQ(MSG_ReadByte(&msg), svc_packetentities);
    T_EQ(MSG_ReadEntityBits(&msg, &bits), 9999);
    T_EQ(bits, 1u << U_REMOVE);
    number = MSG_ReadEntityBits(&msg, &bits);
    T_EQ(number, 10000); T_ASSERT(!(bits & (1u << U_REMOVE)));
    entity = (entityState_t){ 0 };
    MSG_ReadDeltaEntity(&msg, &entity, number, bits);
    T_EQ(entity.model, 2);
    number = MSG_ReadEntityBits(&msg, &bits);
    T_EQ(number, 32768); T_ASSERT(!(bits & (1u << U_REMOVE)));
    entity = states[1];
    MSG_ReadDeltaEntity(&msg, &entity, number, bits);
    T_EQ(entity.model, 2);
    T_EQ(MSG_ReadEntityBits(&msg, &bits), 0); T_EQ(bits, 0);
    T_EQ(msg.readcount, msg.cursize);
}
