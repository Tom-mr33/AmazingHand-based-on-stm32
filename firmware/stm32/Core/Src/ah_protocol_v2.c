#include "ah_protocol_v2.h"

#include <string.h>

_Static_assert(AH_V2_HELLO_SIZE == 10u, "hello size");
_Static_assert(AH_V2_ACCEPT_SIZE == 11u, "accept size");
_Static_assert(AH_V2_HEARTBEAT_SIZE == 8u, "heartbeat size");
_Static_assert(AH_V2_EXECUTION_SIZE == 83u, "execution size");
_Static_assert(AH_V2_ACTUAL_SIZE == 55u, "actual size");
_Static_assert(AH_V2_HEALTH_SIZE == 14u, "health size");
_Static_assert(AH_V2_INFO_SIZE == 10u, "info size");
_Static_assert(AH_V2_MAX_FRAME == 105u, "max frame");

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int16_t rd_i16(const uint8_t *p)
{
    return (int16_t)rd_u16(p);
}

static void wr_i16(uint8_t *p, int16_t v)
{
    wr_u16(p, (uint16_t)v);
}

uint16_t ah_v2_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    int bit;
    for (i = 0; i < len; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

size_t ah_v2_encode_frame(uint8_t type, uint16_t seq, const uint8_t *payload,
                          uint8_t len, uint8_t *out, size_t out_cap)
{
    size_t total;
    uint16_t crc;
    if (len > AH_V2_MAX_PAYLOAD || out == NULL || (len > 0u && payload == NULL)) {
        return 0;
    }
    total = (size_t)len + 9u;
    if (out_cap < total) {
        return 0;
    }
    out[0] = 0xAAu;
    out[1] = 0x55u;
    out[2] = AH_V2_VERSION;
    out[3] = type;
    wr_u16(&out[4], seq);
    out[6] = len;
    if (len > 0u) {
        memcpy(&out[7], payload, len);
    }
    crc = ah_v2_crc16(&out[2], (size_t)len + 5u);
    wr_u16(&out[7 + len], crc);
    return total;
}

void ah_v2_parser_init(ah_v2_parser_t *parser)
{
    if (parser != NULL) {
        parser->len = 0;
    }
}

size_t ah_v2_parser_feed(ah_v2_parser_t *parser, const uint8_t *data, size_t len,
                         ah_v2_frame_t *out, size_t max_frames)
{
    size_t count = 0;
    if (parser == NULL || data == NULL || out == NULL || max_frames == 0u) {
        return 0;
    }
    if (parser->len + len > sizeof(parser->buf)) {
        parser->len = 0;
        if (len > sizeof(parser->buf)) {
            data += len - sizeof(parser->buf);
            len = sizeof(parser->buf);
        }
    }
    memcpy(&parser->buf[parser->len], data, len);
    parser->len += len;

    while (count < max_frames) {
        size_t start = 0;
        size_t total;
        uint8_t flen;
        uint16_t got;
        while (start + 1u < parser->len &&
               !(parser->buf[start] == 0xAAu && parser->buf[start + 1u] == 0x55u)) {
            ++start;
        }
        if (start + 1u >= parser->len) {
            if (parser->len > 1u) {
                parser->buf[0] = parser->buf[parser->len - 1u];
                parser->len = 1;
            }
            break;
        }
        if (start > 0u) {
            memmove(parser->buf, &parser->buf[start], parser->len - start);
            parser->len -= start;
        }
        if (parser->len < 9u) {
            break;
        }
        if (parser->buf[2] != AH_V2_VERSION) {
            memmove(parser->buf, &parser->buf[2], parser->len - 2u);
            parser->len -= 2u;
            continue;
        }
        flen = parser->buf[6];
        if (flen > AH_V2_MAX_PAYLOAD) {
            memmove(parser->buf, &parser->buf[2], parser->len - 2u);
            parser->len -= 2u;
            continue;
        }
        total = (size_t)flen + 9u;
        if (parser->len < total) {
            break;
        }
        got = (uint16_t)((uint16_t)parser->buf[7 + flen] |
                         ((uint16_t)parser->buf[8 + flen] << 8));
        if (got != ah_v2_crc16(&parser->buf[2], (size_t)flen + 5u)) {
            memmove(parser->buf, &parser->buf[2], parser->len - 2u);
            parser->len -= 2u;
            continue;
        }
        out[count].type = parser->buf[3];
        out[count].seq = rd_u16(&parser->buf[4]);
        out[count].len = flen;
        if (flen > 0u) {
            memcpy(out[count].payload, &parser->buf[7], flen);
        }
        ++count;
        memmove(parser->buf, &parser->buf[total], parser->len - total);
        parser->len -= total;
    }
    return count;
}

static size_t build(uint8_t type, uint16_t seq, const uint8_t *payload,
                    uint8_t len, uint8_t *out, size_t cap)
{
    return ah_v2_encode_frame(type, seq, payload, len, out, cap);
}

size_t ah_v2_build_hello(uint16_t seq, const ah_v2_hello_t *v, uint8_t *out, size_t cap)
{
    uint8_t p[AH_V2_HELLO_SIZE];
    if (v == NULL) {
        return 0;
    }
    p[0] = v->proto;
    p[1] = v->node_type;
    wr_u32(&p[2], v->session_id);
    wr_u16(&p[6], v->profile_rev);
    wr_u16(&p[8], v->caps);
    return build(AH_V2_MSG_HELLO, seq, p, AH_V2_HELLO_SIZE, out, cap);
}

size_t ah_v2_build_accept(uint16_t seq, const ah_v2_accept_t *v, uint8_t *out, size_t cap)
{
    uint8_t p[AH_V2_ACCEPT_SIZE];
    if (v == NULL) {
        return 0;
    }
    p[0] = v->proto;
    p[1] = v->status;
    wr_u32(&p[2], v->session_id);
    wr_u16(&p[6], v->fw_version);
    p[8] = v->servo_count;
    wr_u16(&p[9], v->caps);
    return build(AH_V2_MSG_ACCEPT, seq, p, AH_V2_ACCEPT_SIZE, out, cap);
}

size_t ah_v2_build_heartbeat(uint16_t seq, const ah_v2_heartbeat_t *v, uint8_t *out, size_t cap)
{
    uint8_t p[AH_V2_HEARTBEAT_SIZE];
    if (v == NULL) {
        return 0;
    }
    wr_u32(&p[0], v->session_id);
    wr_u32(&p[4], v->uptime_ms);
    return build(AH_V2_MSG_HEARTBEAT, seq, p, AH_V2_HEARTBEAT_SIZE, out, cap);
}

size_t ah_v2_build_execution(uint16_t seq, const ah_v2_execution_t *v, uint8_t *out, size_t cap)
{
    uint8_t p[AH_V2_EXECUTION_SIZE];
    size_t i;
    if (v == NULL) {
        return 0;
    }
    p[0] = v->mode;
    for (i = 0; i < AH_V2_SERVO_COUNT; ++i) {
        wr_i16(&p[1 + i * 2u], v->target[i]);
        wr_i16(&p[17 + i * 2u], v->close[i]);
        wr_u16(&p[33 + i * 2u], v->load_threshold[i]);
        wr_u16(&p[49 + i * 2u], v->speed[i]);
        wr_u16(&p[65 + i * 2u], v->torque_limit[i]);
    }
    wr_u16(&p[81], v->target_timeout_ms);
    return build(AH_V2_MSG_EXECUTION, seq, p, AH_V2_EXECUTION_SIZE, out, cap);
}

size_t ah_v2_build_actual(uint16_t seq, const ah_v2_actual_t *v, uint8_t *out, size_t cap)
{
    uint8_t p[AH_V2_ACTUAL_SIZE];
    size_t i;
    if (v == NULL) {
        return 0;
    }
    for (i = 0; i < AH_V2_SERVO_COUNT; ++i) {
        wr_i16(&p[i * 2u], v->raw[i]);
        wr_i16(&p[16 + i * 2u], v->load[i]);
        p[32 + i] = (uint8_t)v->temp_c[i];
        p[47 + i] = v->error_bits[i];
    }
    p[40] = v->online_mask;
    wr_u32(&p[41], v->sample_time_ms);
    wr_u16(&p[45], v->age_ms);
    return build(AH_V2_MSG_ACTUAL, seq, p, AH_V2_ACTUAL_SIZE, out, cap);
}

size_t ah_v2_build_health(uint16_t seq, const ah_v2_health_t *v, uint8_t *out, size_t cap)
{
    uint8_t p[AH_V2_HEALTH_SIZE];
    size_t i;
    if (v == NULL) {
        return 0;
    }
    p[0] = v->phase;
    p[1] = v->stop_reason;
    wr_u16(&p[2], v->fault_bits);
    p[4] = v->watchdog_state;
    p[5] = v->flags;
    for (i = 0; i < 4u; ++i) {
        wr_u16(&p[6 + i * 2u], v->counters[i]);
    }
    return build(AH_V2_MSG_HEALTH, seq, p, AH_V2_HEALTH_SIZE, out, cap);
}

size_t ah_v2_build_operation(uint16_t seq, const ah_v2_operation_t *v, uint8_t *out, size_t cap)
{
    uint8_t p[AH_V2_MAX_PAYLOAD];
    if (v == NULL || v->data_len > AH_V2_OPERATION_MAX_DATA) {
        return 0;
    }
    wr_u32(&p[0], v->op_id);
    p[4] = v->op_type;
    p[5] = v->status;
    if (v->data_len > 0u) {
        memcpy(&p[AH_V2_OPERATION_HDR_SIZE], v->data, v->data_len);
    }
    return build(AH_V2_MSG_OPERATION, seq, p,
                 (uint8_t)(AH_V2_OPERATION_HDR_SIZE + v->data_len), out, cap);
}

size_t ah_v2_build_info(uint16_t seq, const ah_v2_info_t *v, uint8_t *out, size_t cap)
{
    uint8_t p[AH_V2_INFO_SIZE];
    if (v == NULL) {
        return 0;
    }
    p[0] = v->proto;
    p[1] = v->fw_major;
    p[2] = v->fw_minor;
    p[3] = v->servo_count;
    wr_u16(&p[4], v->caps);
    wr_u32(&p[6], v->build_id);
    return build(AH_V2_MSG_INFO, seq, p, AH_V2_INFO_SIZE, out, cap);
}

int ah_v2_parse_hello(const uint8_t *payload, size_t len, ah_v2_hello_t *out)
{
    if (payload == NULL || out == NULL || len != AH_V2_HELLO_SIZE) {
        return -1;
    }
    out->proto = payload[0];
    out->node_type = payload[1];
    out->session_id = rd_u32(&payload[2]);
    out->profile_rev = rd_u16(&payload[6]);
    out->caps = rd_u16(&payload[8]);
    return 0;
}

int ah_v2_parse_accept(const uint8_t *payload, size_t len, ah_v2_accept_t *out)
{
    if (payload == NULL || out == NULL || len != AH_V2_ACCEPT_SIZE) {
        return -1;
    }
    out->proto = payload[0];
    out->status = payload[1];
    out->session_id = rd_u32(&payload[2]);
    out->fw_version = rd_u16(&payload[6]);
    out->servo_count = payload[8];
    out->caps = rd_u16(&payload[9]);
    return 0;
}

int ah_v2_parse_heartbeat(const uint8_t *payload, size_t len, ah_v2_heartbeat_t *out)
{
    if (payload == NULL || out == NULL || len != AH_V2_HEARTBEAT_SIZE) {
        return -1;
    }
    out->session_id = rd_u32(&payload[0]);
    out->uptime_ms = rd_u32(&payload[4]);
    return 0;
}

int ah_v2_parse_execution(const uint8_t *payload, size_t len, ah_v2_execution_t *out)
{
    size_t i;
    if (payload == NULL || out == NULL || len != AH_V2_EXECUTION_SIZE) {
        return -1;
    }
    out->mode = payload[0];
    for (i = 0; i < AH_V2_SERVO_COUNT; ++i) {
        out->target[i] = rd_i16(&payload[1 + i * 2u]);
        out->close[i] = rd_i16(&payload[17 + i * 2u]);
        out->load_threshold[i] = rd_u16(&payload[33 + i * 2u]);
        out->speed[i] = rd_u16(&payload[49 + i * 2u]);
        out->torque_limit[i] = rd_u16(&payload[65 + i * 2u]);
    }
    out->target_timeout_ms = rd_u16(&payload[81]);
    return 0;
}

int ah_v2_parse_actual(const uint8_t *payload, size_t len, ah_v2_actual_t *out)
{
    size_t i;
    if (payload == NULL || out == NULL || len != AH_V2_ACTUAL_SIZE) {
        return -1;
    }
    for (i = 0; i < AH_V2_SERVO_COUNT; ++i) {
        out->raw[i] = rd_i16(&payload[i * 2u]);
        out->load[i] = rd_i16(&payload[16 + i * 2u]);
        out->temp_c[i] = (int8_t)payload[32 + i];
        out->error_bits[i] = payload[47 + i];
    }
    out->online_mask = payload[40];
    out->sample_time_ms = rd_u32(&payload[41]);
    out->age_ms = rd_u16(&payload[45]);
    return 0;
}

int ah_v2_parse_health(const uint8_t *payload, size_t len, ah_v2_health_t *out)
{
    size_t i;
    if (payload == NULL || out == NULL || len != AH_V2_HEALTH_SIZE) {
        return -1;
    }
    out->phase = payload[0];
    out->stop_reason = payload[1];
    out->fault_bits = rd_u16(&payload[2]);
    out->watchdog_state = payload[4];
    out->flags = payload[5];
    for (i = 0; i < 4u; ++i) {
        out->counters[i] = rd_u16(&payload[6 + i * 2u]);
    }
    return 0;
}

int ah_v2_parse_operation(const uint8_t *payload, size_t len, ah_v2_operation_t *out)
{
    size_t n;
    if (payload == NULL || out == NULL || len < AH_V2_OPERATION_HDR_SIZE) {
        return -1;
    }
    out->op_id = rd_u32(&payload[0]);
    out->op_type = payload[4];
    out->status = payload[5];
    n = len - AH_V2_OPERATION_HDR_SIZE;
    if (n > AH_V2_OPERATION_MAX_DATA) {
        return -1;
    }
    if (n > 0u) {
        memcpy(out->data, &payload[AH_V2_OPERATION_HDR_SIZE], n);
    }
    out->data_len = (uint8_t)n;
    return 0;
}

int ah_v2_parse_info(const uint8_t *payload, size_t len, ah_v2_info_t *out)
{
    if (payload == NULL || out == NULL || len != AH_V2_INFO_SIZE) {
        return -1;
    }
    out->proto = payload[0];
    out->fw_major = payload[1];
    out->fw_minor = payload[2];
    out->servo_count = payload[3];
    out->caps = rd_u16(&payload[4]);
    out->build_id = rd_u32(&payload[6]);
    return 0;
}
