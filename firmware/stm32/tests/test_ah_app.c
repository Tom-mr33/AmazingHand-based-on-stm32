#include "ah_app.h"
#include "ah_hal.h"
#include "ah_protocol_v2.h"
#include "fake_hal.h"

#include <stdio.h>
#include <string.h>

static size_t feed_uart(const uint8_t *frame, size_t n)
{
    fake_hal_uart_push(frame, n);
    return n;
}

static size_t collect(ah_v2_frame_t *frames, size_t max)
{
    uint8_t tx[4096];
    size_t n = fake_hal_uart_tx_read(tx, sizeof(tx));
    ah_v2_parser_t parser;
    ah_v2_parser_init(&parser);
    if (n == 0u) {
        return 0u;
    }
    return ah_v2_parser_feed(&parser, tx, n, frames, max);
}

int main(void)
{
    ah_app_t app;
    ah_device_config_t cfg;
    uint8_t frame[AH_V2_MAX_FRAME];
    ah_v2_frame_t frames[32];
    size_t n;
    size_t i;
    size_t count;
    size_t actual_count = 0u;
    size_t health_count = 0u;
    int saw_accept = 0;
    int saw_operation = 0;

    fake_hal_reset();
    for (i = 0; i < AH_SCS_SERVO_COUNT; ++i) {
        fake_hal_set_servo((uint8_t)(i + 1u), 1800, 400, 30, 0, 1);
    }
    ah_device_config_default(&cfg);
    cfg.pregrasp_ms = 10u;
    cfg.closing_timeout_ms = 100u;
    cfg.release_ms = 10u;
    cfg.target_timeout_ms = 500u;
    cfg.link_timeout_ms = 500u;
    ah_app_init(&app, &cfg);

    /* HELLO -> ACCEPT */
    {
        ah_v2_hello_t hello = { AH_V2_VERSION, AH_V2_NODE_PI, 0x11223344u, 1u, 0u };
        n = ah_v2_build_hello(1u, &hello, frame, sizeof(frame));
        feed_uart(frame, n);
    }
    ah_app_tick(&app, 0u);
    count = collect(frames, 32u);
    for (i = 0; i < count; ++i) {
        if (frames[i].type == AH_V2_MSG_ACCEPT) {
            ah_v2_accept_t a;
            if (ah_v2_parse_accept(frames[i].payload, frames[i].len, &a) == 0 &&
                a.status == AH_V2_ACCEPT_OK && a.session_id == 0x11223344u) {
                saw_accept = 1;
            }
        }
    }
    if (!saw_accept) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* SCS feedback slots */
    ah_app_tick(&app, 2u);
    ah_app_tick(&app, 4u);
    ah_app_tick(&app, 6u);

    /* EXECUTION -> device PREGRASP */
    {
        ah_v2_execution_t ex;
        ex.mode = AH_V2_MODE_GRASP;
        for (i = 0; i < AH_V2_SERVO_COUNT; ++i) {
            ex.target[i] = 1000;
            ex.close[i] = 2000;
            ex.load_threshold[i] = 300u;
            ex.speed[i] = 100u;
            ex.torque_limit[i] = 500u;
        }
        ex.target_timeout_ms = 500u;
        n = ah_v2_build_execution(2u, &ex, frame, sizeof(frame));
        feed_uart(frame, n);
    }
    ah_app_tick(&app, 8u);
    if (app.dev.phase != AH_V2_PHASE_PREGRASP) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    for (i = 10u; i <= 24u; i += 2u) {
        ah_app_tick(&app, (uint32_t)i);
    }
    if (app.dev.phase != AH_V2_PHASE_HOLD) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    count = collect(frames, 32u);
    for (i = 0; i < count; ++i) {
        if (frames[i].type == AH_V2_MSG_ACTUAL) actual_count++;
        if (frames[i].type == AH_V2_MSG_HEALTH) health_count++;
    }
    if (actual_count == 0u || health_count == 0u) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* RESET operation -> IDLE */
    {
        ah_v2_operation_t op;
        op.op_id = 7u;
        op.op_type = AH_V2_OP_RESET;
        op.status = 0u;
        op.data_len = 0u;
        n = ah_v2_build_operation(3u, &op, frame, sizeof(frame));
        feed_uart(frame, n);
    }
    ah_app_tick(&app, 26u);
    if (app.dev.phase != AH_V2_PHASE_IDLE) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }
    count = collect(frames, 32u);
    for (i = 0; i < count; ++i) {
        if (frames[i].type == AH_V2_MSG_OPERATION) {
            ah_v2_operation_t op;
            if (ah_v2_parse_operation(frames[i].payload, frames[i].len, &op) == 0 &&
                op.op_id == 7u && op.status == 0u) {
                saw_operation = 1;
            }
        }
    }
    if (!saw_operation) { fprintf(stderr, "fail line %d\n", __LINE__); return 1; }

    /* new session must stop the old device before accepting new execution */
    {
        ah_v2_hello_t hello = { AH_V2_VERSION, AH_V2_NODE_PI, 0x55667788u, 1u, 0u };
        n = ah_v2_build_hello(4u, &hello, frame, sizeof(frame));
        feed_uart(frame, n);
    }
    ah_app_tick(&app, 28u);
    if (app.dev.stop_reason != AH_V2_STOP_SESSION_RESET) {
        fprintf(stderr, "fail line %d\n", __LINE__); return 1;
    }

    printf("AH_APP_TEST_OK\n");
    return 0;
}
