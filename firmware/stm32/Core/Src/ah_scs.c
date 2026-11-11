#include "ah_scs.h"
#include "ah_hal.h"

#include <string.h>

#define SCS_BROADCAST_ID 0xFEu
#define SCS_HEADER0 0xFFu
#define SCS_HEADER1 0xFFu
#define SCS_INS_READ 0x02u
#define SCS_INS_SYNC_WRITE 0x83u
#define SCS_ADDR_TORQUE_SWITCH 0x28u
#define SCS_ADDR_GOAL_POSITION 0x2Au
#define SCS_ADDR_RUNNING_SPEED 0x2Eu
#define SCS_ADDR_TORQUE_LIMIT 0x30u
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

static size_t build_sync_frame(uint8_t addr, uint8_t data_len, const uint8_t *values,
                               uint8_t *out, size_t cap)
{
    uint8_t body[42];
    size_t body_len = 2u + (size_t)AH_SCS_SERVO_COUNT * (1u + (size_t)data_len);
    size_t i;
    uint8_t length;
    if (values == NULL || out == NULL || data_len == 0u || data_len > 4u || cap < body_len + 6u) {
        return 0;
    }
    body[0] = addr;
    body[1] = data_len;
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        size_t off = 2u + i * (1u + (size_t)data_len);
        size_t j;
        body[off] = (uint8_t)(i + 1u);
        for (j = 0; j < data_len; ++j) {
            body[off + 1u + j] = values[i * data_len + j];
        }
    }
    length = (uint8_t)(body_len + 2u);
    out[0] = SCS_HEADER0;
    out[1] = SCS_HEADER1;
    out[2] = SCS_BROADCAST_ID;
    out[3] = length;
    out[4] = SCS_INS_SYNC_WRITE;
    memcpy(&out[5], body, body_len);
    out[5 + body_len] = scs_checksum(SCS_BROADCAST_ID, length, SCS_INS_SYNC_WRITE, body, body_len);
    return body_len + 6u;
}

size_t ah_scs_build_sync_write(const int16_t target[AH_SCS_SERVO_COUNT],
                               uint8_t *out, size_t cap)
{
    uint8_t values[AH_SCS_SERVO_COUNT * 2u];
    size_t i;
    if (target == NULL) {
        return 0;
    }
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        wr_u16(&values[i * 2u], (uint16_t)target[i]);
    }
    return build_sync_frame(SCS_ADDR_GOAL_POSITION, 2u, values, out, cap);
}

size_t ah_scs_build_sync_torque(uint8_t enabled, uint8_t *out, size_t cap)
{
    uint8_t values[AH_SCS_SERVO_COUNT];
    size_t i;
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        values[i] = enabled ? 1u : 2u;
    }
    return build_sync_frame(SCS_ADDR_TORQUE_SWITCH, 1u, values, out, cap);
}

size_t ah_scs_build_sync_profile(const uint16_t speed[AH_SCS_SERVO_COUNT],
                                 const uint16_t torque_limit[AH_SCS_SERVO_COUNT],
                                 uint8_t *out, size_t cap)
{
    uint8_t values[AH_SCS_SERVO_COUNT * 4u];
    size_t i;
    if (speed == NULL || torque_limit == NULL) {
        return 0;
    }
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        wr_u16(&values[i * 4u], speed[i]);
        wr_u16(&values[i * 4u + 2u], torque_limit[i]);
    }
    return build_sync_frame(SCS_ADDR_RUNNING_SPEED, 4u, values, out, cap);
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
        scs->torque_enabled = 0u;
        scs->torque_dirty = 1u;
        scs->profile_dirty = 1u;
    }
}

void ah_scs_set_targets(ah_scs_t *scs, const int16_t target[AH_SCS_SERVO_COUNT])
{
    if (scs != NULL && target != NULL) {
        memcpy(scs->target, target, sizeof(scs->target));
    }
}

void ah_scs_set_profile(ah_scs_t *scs, const uint16_t speed[AH_SCS_SERVO_COUNT],
                        const uint16_t torque_limit[AH_SCS_SERVO_COUNT])
{
    if (scs == NULL || speed == NULL || torque_limit == NULL) {
        return;
    }
    if (memcmp(scs->speed, speed, sizeof(scs->speed)) != 0 ||
        memcmp(scs->torque_limit, torque_limit, sizeof(scs->torque_limit)) != 0) {
        memcpy(scs->speed, speed, sizeof(scs->speed));
        memcpy(scs->torque_limit, torque_limit, sizeof(scs->torque_limit));
        scs->profile_dirty = 1u;
    }
}

void ah_scs_set_torque(ah_scs_t *scs, uint8_t enabled)
{
    if (scs == NULL) {
        return;
    }
    enabled = (enabled != 0u) ? 1u : 0u;
    if (scs->torque_enabled != enabled) {
        scs->torque_enabled = enabled;
        scs->torque_dirty = 1u;
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

static int send_sync(ah_scs_t *scs, const uint8_t *frame, size_t len)
{
    if (!ah_hal_bus_write(frame, len)) {
        scs->timeout_count++;
        return -1;
    }
    scs->write_count++;
    return 0;
}

void ah_scs_tick(ah_scs_t *scs, uint32_t now_ms)
{
    uint8_t sync[AH_SCS_SYNC_PROFILE_SIZE];
    uint8_t slot;
    size_t i;
    size_t first;
    size_t n;
    if (scs == NULL) {
        return;
    }
    scs->now_ms = now_ms;
    update_age(scs, now_ms);

    slot = scs->slot;
    if (slot == 0u) {
        if (scs->torque_dirty != 0u && scs->torque_enabled == 0u) {
            n = ah_scs_build_sync_torque(0u, sync, sizeof(sync));
            if (n > 0u && send_sync(scs, sync, n) == 0) {
                scs->torque_dirty = 0u;
            }
        }
        n = ah_scs_build_sync_write(scs->target, sync, sizeof(sync));
        if (n > 0u) {
            (void)send_sync(scs, sync, n);
        }
        if (scs->profile_dirty != 0u) {
            n = ah_scs_build_sync_profile(scs->speed, scs->torque_limit, sync, sizeof(sync));
            if (n > 0u && send_sync(scs, sync, n) == 0) {
                scs->profile_dirty = 0u;
            }
        }
        if (scs->torque_dirty != 0u && scs->torque_enabled != 0u) {
            n = ah_scs_build_sync_torque(1u, sync, sizeof(sync));
            if (n > 0u && send_sync(scs, sync, n) == 0) {
                scs->torque_dirty = 0u;
            }
        }
    } else {
        first = (size_t)(slot - 1u) * AH_SCS_READS_PER_SLOT;
        for (i = 0; i < AH_SCS_READS_PER_SLOT; ++i) {
            read_one(scs, (uint8_t)(first + i + 1u));
        }
    }
    scs->slot = (uint8_t)((slot + 1u) % 3u);
}
