#include "ah_protocol_v2.h"

#include <stdio.h>
#include <string.h>

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t from_hex(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = 0;
    int hi;
    int lo;
    while (*hex != '\0') {
        hi = hexval(*hex++);
        lo = hexval(*hex++);
        if (hi < 0 || lo < 0 || n >= cap) {
            return 0;
        }
        out[n++] = (uint8_t)((hi << 4) | lo);
    }
    return n;
}

static int expect_hex(const uint8_t *got, size_t got_len, const char *hex)
{
    uint8_t want[AH_V2_MAX_FRAME];
    size_t want_len = from_hex(hex, want, sizeof(want));
    if (want_len == 0 || got_len != want_len) {
        fprintf(stderr, "length mismatch got=%u want=%u\n", (unsigned)got_len, (unsigned)want_len);
        return -1;
    }
    if (memcmp(got, want, want_len) != 0) {
        size_t i;
        fprintf(stderr, "bytes mismatch\n");
        for (i = 0; i < want_len; ++i) {
            if (got[i] != want[i]) {
                fprintf(stderr, "  [%u] got=%02X want=%02X\n", (unsigned)i, got[i], want[i]);
                break;
            }
        }
        return -1;
    }
    return 0;
}

int main(void)
{
    uint8_t buf[AH_V2_MAX_FRAME];
    size_t n;
    ah_v2_hello_t hello = { AH_V2_VERSION, AH_V2_NODE_PI, 0x12345678u, 1u, 0u };
    ah_v2_execution_t exec;
    ah_v2_actual_t actual;
    ah_v2_health_t health = { AH_V2_PHASE_HOLD, AH_V2_STOP_NONE, 0u, 0u, 0u, { 1u, 2u, 3u, 4u } };
    ah_v2_operation_t op = { 7u, AH_V2_OP_RESET, 0u, { 0u }, 0u };
    ah_v2_info_t info = { AH_V2_VERSION, 0u, 1u, AH_V2_SERVO_COUNT, 0u, 0u };
    ah_v2_frame_t frames[4];
    ah_v2_parser_t parser;
    size_t i;
    size_t count;

    /* hello golden */
    n = ah_v2_build_hello(1u, &hello, buf, sizeof(buf));
    if (n == 0 || expect_hex(buf, n, "aa55020101000a02017856341201000000a8f0") != 0) {
        return 1;
    }

    /* execution golden */
    exec.mode = AH_V2_MODE_GRASP;
    for (i = 0; i < AH_V2_SERVO_COUNT; ++i) {
        exec.target[i] = (int16_t)i;
        exec.close[i] = (int16_t)(2000 + (int)i);
        exec.load_threshold[i] = 384u;
        exec.speed[i] = 100u;
        exec.torque_limit[i] = 500u;
    }
    exec.target_timeout_ms = 500u;
    n = ah_v2_build_execution(2u, &exec, buf, sizeof(buf));
    if (n == 0 || expect_hex(buf, n,
        "aa5502040200530200000100020003000400050006000700d007d107d207d307d407d507d607d7078001800180018001800180018001800164006400640064006400640064006400f401f401f401f401f401f401f401f401f401b01c") != 0) {
        return 1;
    }

    /* actual golden */
    for (i = 0; i < AH_V2_SERVO_COUNT; ++i) {
        actual.raw[i] = (int16_t)i;
        actual.load[i] = -1;
        actual.temp_c[i] = 30;
        actual.error_bits[i] = 0u;
    }
    actual.online_mask = 0xFFu;
    actual.sample_time_ms = 1000u;
    actual.age_ms = 5u;
    n = ah_v2_build_actual(3u, &actual, buf, sizeof(buf));
    if (n == 0 || expect_hex(buf, n,
        "aa55020503003700000100020003000400050006000700ffffffffffffffffffffffffffffffff1e1e1e1e1e1e1e1effe8030000050000000000000000009751") != 0) {
        return 1;
    }

    /* health + operation + info golden */
    n = ah_v2_build_health(4u, &health, buf, sizeof(buf));
    if (n == 0 || expect_hex(buf, n, "aa55020604000e030000000000010002000300040014bb") != 0) {
        return 1;
    }
    n = ah_v2_build_operation(5u, &op, buf, sizeof(buf));
    if (n == 0 || expect_hex(buf, n, "aa550207050006070000000100df45") != 0) {
        return 1;
    }
    n = ah_v2_build_info(6u, &info, buf, sizeof(buf));
    if (n == 0 || expect_hex(buf, n, "aa55020806000a02000108000000000000d36f") != 0) {
        return 1;
    }

    /* roundtrip parse via parser */
    ah_v2_parser_init(&parser);
    n = ah_v2_build_hello(10u, &hello, buf, sizeof(buf));
    count = ah_v2_parser_feed(&parser, buf, n / 2u, frames, 4u);
    if (count != 0u) return 1;
    count = ah_v2_parser_feed(&parser, &buf[n / 2u], n - n / 2u, frames, 4u);
    if (count != 1u || frames[0].type != AH_V2_MSG_HELLO) return 1;
    if (ah_v2_parse_hello(frames[0].payload, frames[0].len, &hello) != 0) return 1;
    if (hello.session_id != 0x12345678u || hello.profile_rev != 1u) return 1;

    /* version mismatch is dropped */
    ah_v2_parser_init(&parser);
    n = ah_v2_build_hello(11u, &hello, buf, sizeof(buf));
    buf[2] = 1u;
    count = ah_v2_parser_feed(&parser, buf, n, frames, 4u);
    if (count != 0u) return 1;

    /* bad CRC is dropped */
    ah_v2_parser_init(&parser);
    n = ah_v2_build_hello(12u, &hello, buf, sizeof(buf));
    buf[n - 1u] ^= 0x01u;
    count = ah_v2_parser_feed(&parser, buf, n, frames, 4u);
    if (count != 0u) return 1;

    printf("AH_V2_TEST_OK\n");
    return 0;
}
