#include "ah_app.h"
#include "ah_hal.h"

#include <string.h>

static void app_send(ah_app_t *app, uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t frame[AH_V2_MAX_FRAME];
    size_t n;
    app->tx_seq = (uint16_t)((app->tx_seq + 1u) & 0xFFFFu);
    n = ah_v2_encode_frame(type, app->tx_seq, payload, len, frame, sizeof(frame));
    if (n > 0u) {
        (void)ah_hal_uart_write(frame, n);
    }
}

static void send_accept(ah_app_t *app, uint8_t status)
{
    ah_v2_accept_t a;
    uint8_t payload[AH_V2_ACCEPT_SIZE];
    a.proto = AH_V2_VERSION;
    a.status = status;
    a.session_id = app->session_id;
    a.fw_version = 0x0001u;
    a.servo_count = AH_V2_SERVO_COUNT;
    a.caps = 0u;
    payload[0] = a.proto;
    payload[1] = a.status;
    payload[2] = (uint8_t)(a.session_id & 0xFFu);
    payload[3] = (uint8_t)((a.session_id >> 8) & 0xFFu);
    payload[4] = (uint8_t)((a.session_id >> 16) & 0xFFu);
    payload[5] = (uint8_t)((a.session_id >> 24) & 0xFFu);
    payload[6] = (uint8_t)(a.fw_version & 0xFFu);
    payload[7] = (uint8_t)((a.fw_version >> 8) & 0xFFu);
    payload[8] = a.servo_count;
    payload[9] = (uint8_t)(a.caps & 0xFFu);
    payload[10] = (uint8_t)((a.caps >> 8) & 0xFFu);
    app_send(app, AH_V2_MSG_ACCEPT, payload, AH_V2_ACCEPT_SIZE);
}

static void send_actual(ah_app_t *app)
{
    ah_v2_actual_t actual;
    uint8_t payload[AH_V2_ACTUAL_SIZE];
    size_t i;
    ah_device_get_actual(&app->dev, &actual);
    for (i = 0; i < AH_V2_SERVO_COUNT; ++i) {
        payload[i * 2u] = (uint8_t)(actual.raw[i] & 0xFF);
        payload[i * 2u + 1u] = (uint8_t)(((uint16_t)actual.raw[i] >> 8) & 0xFF);
        payload[16 + i * 2u] = (uint8_t)(actual.load[i] & 0xFF);
        payload[16 + i * 2u + 1u] = (uint8_t)(((uint16_t)actual.load[i] >> 8) & 0xFF);
        payload[32 + i] = (uint8_t)actual.temp_c[i];
        payload[47 + i] = actual.error_bits[i];
    }
    payload[40] = actual.online_mask;
    payload[41] = (uint8_t)(actual.sample_time_ms & 0xFFu);
    payload[42] = (uint8_t)((actual.sample_time_ms >> 8) & 0xFFu);
    payload[43] = (uint8_t)((actual.sample_time_ms >> 16) & 0xFFu);
    payload[44] = (uint8_t)((actual.sample_time_ms >> 24) & 0xFFu);
    payload[45] = (uint8_t)(actual.age_ms & 0xFFu);
    payload[46] = (uint8_t)((actual.age_ms >> 8) & 0xFFu);
    app_send(app, AH_V2_MSG_ACTUAL, payload, AH_V2_ACTUAL_SIZE);
}

static void send_health(ah_app_t *app)
{
    ah_v2_health_t h;
    uint8_t payload[AH_V2_HEALTH_SIZE];
    size_t i;
    ah_device_get_health(&app->dev, &h);
    payload[0] = h.phase;
    payload[1] = h.stop_reason;
    payload[2] = (uint8_t)(h.fault_bits & 0xFFu);
    payload[3] = (uint8_t)((h.fault_bits >> 8) & 0xFFu);
    payload[4] = h.watchdog_state;
    payload[5] = h.flags;
    for (i = 0; i < 4u; ++i) {
        payload[6 + i * 2u] = (uint8_t)(h.counters[i] & 0xFFu);
        payload[6 + i * 2u + 1u] = (uint8_t)((h.counters[i] >> 8) & 0xFFu);
    }
    app_send(app, AH_V2_MSG_HEALTH, payload, AH_V2_HEALTH_SIZE);
}

static void handle_frame(ah_app_t *app, const ah_v2_frame_t *frame)
{
    if (frame->type == AH_V2_MSG_HELLO) {
        ah_v2_hello_t hello;
        if (ah_v2_parse_hello(frame->payload, frame->len, &hello) != 0) {
            send_accept(app, AH_V2_ACCEPT_PROFILE_MISMATCH);
            return;
        }
        if (hello.proto != AH_V2_VERSION) {
            send_accept(app, AH_V2_ACCEPT_VERSION_MISMATCH);
            return;
        }
        if (hello.profile_rev != app->profile_rev) {
            send_accept(app, AH_V2_ACCEPT_PROFILE_MISMATCH);
            return;
        }
        app->session_id = hello.session_id;
        app->session_ok = 1u;
        ah_device_note_link(&app->dev, app->now_ms);
        send_accept(app, AH_V2_ACCEPT_OK);
    } else if (frame->type == AH_V2_MSG_HEARTBEAT) {
        ah_v2_heartbeat_t hb;
        if (ah_v2_parse_heartbeat(frame->payload, frame->len, &hb) == 0 &&
            app->session_ok != 0u && hb.session_id == app->session_id) {
            ah_device_note_link(&app->dev, app->now_ms);
        }
    } else if (frame->type == AH_V2_MSG_EXECUTION) {
        ah_v2_execution_t exec;
        if (app->session_ok == 0u) {
            return;
        }
        if (ah_v2_parse_execution(frame->payload, frame->len, &exec) == 0) {
            ah_device_note_link(&app->dev, app->now_ms);
            (void)ah_device_apply_execution(&app->dev, &exec, app->now_ms);
        }
    } else if (frame->type == AH_V2_MSG_OPERATION) {
        ah_v2_operation_t op;
        if (app->session_ok == 0u) {
            return;
        }
        if (ah_v2_parse_operation(frame->payload, frame->len, &op) == 0) {
            uint8_t payload[AH_V2_OPERATION_HDR_SIZE + 1u];
            uint8_t status = 1u;
            if (op.op_type == AH_V2_OP_RESET && ah_device_reset(&app->dev) == 0) {
                status = 0u;
            }
            payload[0] = (uint8_t)(op.op_id & 0xFFu);
            payload[1] = (uint8_t)((op.op_id >> 8) & 0xFFu);
            payload[2] = (uint8_t)((op.op_id >> 16) & 0xFFu);
            payload[3] = (uint8_t)((op.op_id >> 24) & 0xFFu);
            payload[4] = op.op_type;
            payload[5] = status;
            app_send(app, AH_V2_MSG_OPERATION, payload, AH_V2_OPERATION_HDR_SIZE);
        }
    }
}

void ah_app_init(ah_app_t *app, const ah_device_config_t *cfg)
{
    if (app == NULL) {
        return;
    }
    memset(app, 0, sizeof(*app));
    ah_scs_init(&app->scs);
    ah_device_init(&app->dev, cfg);
    ah_v2_parser_init(&app->parser);
    app->profile_rev = 1u;
    app->control_mode = 1u;
}

void ah_app_tick(ah_app_t *app, uint32_t now_ms)
{
    uint8_t rx[AH_V2_MAX_FRAME];
    size_t n;
    ah_v2_frame_t frames[8];
    size_t count;
    size_t i;
    int16_t targets[AH_SCS_SERVO_COUNT];
    if (app == NULL) {
        return;
    }
    app->now_ms = now_ms;

    n = ah_hal_uart_read(rx, sizeof(rx));
    if (n > 0u) {
        count = ah_v2_parser_feed(&app->parser, rx, n, frames, 8u);
        for (i = 0; i < count; ++i) {
            handle_frame(app, &frames[i]);
        }
    }

    if ((now_ms - app->last_scs_ms) >= AH_SCS_SLOT_MS) {
        app->last_scs_ms = now_ms;
        ah_device_get_targets(&app->dev, targets);
        ah_scs_set_targets(&app->scs, targets);
        ah_scs_tick(&app->scs, now_ms);
    }
    ah_device_step(&app->dev, &app->scs, now_ms);

    if ((now_ms - app->last_telemetry_ms) >= 20u) {
        app->last_telemetry_ms = now_ms;
        send_actual(app);
        send_health(app);
    }
    if (app->session_ok != 0u && (now_ms - app->last_heartbeat_ms) >= 100u) {
        ah_v2_heartbeat_t hb;
        uint8_t payload[AH_V2_HEARTBEAT_SIZE];
        app->last_heartbeat_ms = now_ms;
        hb.session_id = app->session_id;
        hb.uptime_ms = now_ms;
        payload[0] = (uint8_t)(hb.session_id & 0xFFu);
        payload[1] = (uint8_t)((hb.session_id >> 8) & 0xFFu);
        payload[2] = (uint8_t)((hb.session_id >> 16) & 0xFFu);
        payload[3] = (uint8_t)((hb.session_id >> 24) & 0xFFu);
        payload[4] = (uint8_t)(hb.uptime_ms & 0xFFu);
        payload[5] = (uint8_t)((hb.uptime_ms >> 8) & 0xFFu);
        payload[6] = (uint8_t)((hb.uptime_ms >> 16) & 0xFFu);
        payload[7] = (uint8_t)((hb.uptime_ms >> 24) & 0xFFu);
        app_send(app, AH_V2_MSG_HEARTBEAT, payload, AH_V2_HEARTBEAT_SIZE);
    }
}
