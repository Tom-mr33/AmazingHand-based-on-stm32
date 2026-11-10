#include "ah_scs.h"
#include "ah_hal.h"

#include <string.h>

#define SCS_BROADCAST_ID 0xFEu
#define SCS_HEADER0 0xFFu
#define SCS_HEADER1 0xFFu
#define SCS_INS_READ 0x02u
#define SCS_INS_SYNC_WRITE 0x83u
#define SCS_ADDR_GOAL_POSITION 0x2Au
#define SCS_ADDR_FEEDBACK 0x3Au
#define SCS_FEEDBACK_LEN 8u

static uint8_t scs_checksum(uint8_t servo_id, uint8_t length, uint8_t instruction,
                            const uint8_t *params, size_t params_len)
{
    uint16_t sum = (uint16_t)servo_id + (uint16_t)length + (uint16_t)instruction;
    size_t i;
    for (i = 0; i < params_len; ++i) {
        sum += params[i];
    }
    return (uint8_t)(~sum & 0xFFu);
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

size_t ah_scs_build_sync_write(const int16_t target[AH_SCS_SERVO_COUNT],
                               uint8_t *out, size_t cap)
{
    uint8_t body[26];
    size_t i;
    uint8_t length;
    if (target == NULL || out == NULL || cap < AH_SCS_SYNC_FRAME_SIZE) {
        return 0;
    }
    body[0] = SCS_ADDR_GOAL_POSITION;
    body[1] = 2u;
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        body[2 + i * 3u] = (uint8_t)(i + 1u);
        wr_u16(&body[3 + i * 3u], (uint16_t)target[i]);
    }
    length = (uint8_t)(sizeof(body) + 2u);
    out[0] = SCS_HEADER0;
    out[1] = SCS_HEADER1;
    out[2] = SCS_BROADCAST_ID;
    out[3] = length;
    out[4] = SCS_INS_SYNC_WRITE;
    memcpy(&out[5], body, sizeof(body));
    out[5 + sizeof(body)] = scs_checksum(SCS_BROADCAST_ID, length, SCS_INS_SYNC_WRITE, body, sizeof(body));
    return AH_SCS_SYNC_FRAME_SIZE;
}

size_t ah_scs_build_read(uint8_t servo_id, uint8_t *out, size_t cap)
{
    uint8_t body[2];
    if (out == NULL || cap < AH_SCS_READ_FRAME_SIZE || servo_id == 0u || servo_id == SCS_BROADCAST_ID) {
        return 0;
    }
    body[0] = SCS_ADDR_FEEDBACK;
    body[1] = SCS_FEEDBACK_LEN;
    out[0] = SCS_HEADER0;
    out[1] = SCS_HEADER1;
    out[2] = servo_id;
    out[3] = 4u;
    out[4] = SCS_INS_READ;
    out[5] = body[0];
    out[6] = body[1];
    out[7] = scs_checksum(servo_id, 4u, SCS_INS_READ, body, sizeof(body));
    return AH_SCS_READ_FRAME_SIZE;
}

int ah_scs_parse_status(const uint8_t *frame, size_t len, ah_scs_feedback_t *fb)
{
    uint8_t length;
    uint8_t cs;
    if (frame == NULL || fb == NULL || len < AH_SCS_STATUS_SIZE) {
        return -1;
    }
    if (frame[0] != SCS_HEADER0 || frame[1] != SCS_HEADER1) {
        return -1;
    }
    length = frame[3];
    if (length != 10u || len < AH_SCS_STATUS_SIZE) {
        return -1;
    }
    cs = scs_checksum(frame[2], length, frame[4], &frame[5], 8u);
    if (cs != frame[13]) {
        return -1;
    }
    fb->error = frame[4];
    fb->raw = (int16_t)rd_u16(&frame[5]);
    fb->load = (int16_t)rd_u16(&frame[9]);
    fb->temp_c = (int8_t)frame[12];
    return 0;
}

void ah_scs_init(ah_scs_t *scs)
{
    if (scs != NULL) {
        memset(scs, 0, sizeof(*scs));
    }
}

void ah_scs_set_targets(ah_scs_t *scs, const int16_t target[AH_SCS_SERVO_COUNT])
{
    if (scs != NULL && target != NULL) {
        memcpy(scs->target, target, sizeof(scs->target));
    }
}

static void update_age(ah_scs_t *scs, uint32_t now_ms)
{
    size_t i;
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        ah_scs_feedback_t *fb = &scs->fb[i];
        uint32_t age;
        if (fb->valid == 0u) {
            fb->online = 0u;
            fb->age_ms = 0xFFFFu;
            continue;
        }
        age = now_ms - fb->last_good_ms;
        fb->age_ms = (age > 0xFFFFu) ? 0xFFFFu : (uint16_t)age;
        fb->online = (age <= AH_SCS_OFFLINE_MS) ? 1u : 0u;
    }
}

static void read_one(ah_scs_t *scs, uint8_t servo_id)
{
    uint8_t req[AH_SCS_READ_FRAME_SIZE];
    uint8_t resp[AH_SCS_STATUS_SIZE];
    ah_scs_feedback_t fb;
    size_t n;
    if (ah_scs_build_read(servo_id, req, sizeof(req)) == 0u) {
        return;
    }
    if (!ah_hal_bus_write(req, sizeof(req))) {
        scs->timeout_count++;
        return;
    }
    n = ah_hal_bus_read(resp, sizeof(resp), 2000u);
    if (n != AH_SCS_STATUS_SIZE || resp[2] != servo_id) {
        scs->timeout_count++;
        scs->fb[servo_id - 1u].timeout_count++;
        scs->fb[servo_id - 1u].valid = 0u;
        scs->fb[servo_id - 1u].online = 0u;
        return;
    }
    if (ah_scs_parse_status(resp, n, &fb) != 0) {
        scs->crc_error_count++;
        scs->fb[servo_id - 1u].valid = 0u;
        return;
    }
    scs->read_count++;
    fb.valid = 1u;
    fb.online = 1u;
    fb.last_good_ms = scs->now_ms;
    fb.age_ms = 0u;
    fb.timeout_count = scs->fb[servo_id - 1u].timeout_count;
    scs->fb[servo_id - 1u] = fb;
}

void ah_scs_tick(ah_scs_t *scs, uint32_t now_ms)
{
    uint8_t sync[AH_SCS_SYNC_FRAME_SIZE];
    uint8_t slot;
    size_t i;
    size_t first;
    if (scs == NULL) {
        return;
    }
    scs->now_ms = now_ms;
    update_age(scs, now_ms);

    slot = scs->slot;
    if (slot == 0u) {
        if (ah_scs_build_sync_write(scs->target, sync, sizeof(sync)) == 0u ||
            !ah_hal_bus_write(sync, sizeof(sync))) {
            scs->timeout_count++;
        } else {
            scs->write_count++;
        }
    } else {
        first = (size_t)(slot - 1u) * AH_SCS_READS_PER_SLOT;
        for (i = 0; i < AH_SCS_READS_PER_SLOT; ++i) {
            read_one(scs, (uint8_t)(first + i + 1u));
        }
    }
    scs->slot = (uint8_t)((slot + 1u) % 3u);
}
