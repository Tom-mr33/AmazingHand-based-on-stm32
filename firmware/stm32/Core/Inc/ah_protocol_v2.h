#ifndef AH_PROTOCOL_V2_H
#define AH_PROTOCOL_V2_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AH_V2_VERSION 2u
#define AH_V2_MAX_PAYLOAD 96u
#define AH_V2_MAX_FRAME (AH_V2_MAX_PAYLOAD + 9u)
#define AH_V2_PARSER_BUF 256u
#define AH_V2_SERVO_COUNT 8u

/* message types */
#define AH_V2_MSG_HELLO 0x01u
#define AH_V2_MSG_ACCEPT 0x02u
#define AH_V2_MSG_HEARTBEAT 0x03u
#define AH_V2_MSG_EXECUTION 0x04u
#define AH_V2_MSG_ACTUAL 0x05u
#define AH_V2_MSG_HEALTH 0x06u
#define AH_V2_MSG_OPERATION 0x07u
#define AH_V2_MSG_INFO 0x08u

/* node types */
#define AH_V2_NODE_PI 1u
#define AH_V2_NODE_STM32 2u

/* execution modes */
#define AH_V2_MODE_OPEN 1u
#define AH_V2_MODE_GRASP 2u
#define AH_V2_MODE_HOLD 3u
#define AH_V2_MODE_RELEASE 4u
#define AH_V2_MODE_STOP 5u

/* device phases */
#define AH_V2_PHASE_IDLE 0u
#define AH_V2_PHASE_PREGRASP 1u
#define AH_V2_PHASE_CLOSING 2u
#define AH_V2_PHASE_HOLD 3u
#define AH_V2_PHASE_RELEASING 4u
#define AH_V2_PHASE_FAULT 5u

/* stop reasons */
#define AH_V2_STOP_NONE 0u
#define AH_V2_STOP_USER 1u
#define AH_V2_STOP_LINK_TIMEOUT 2u
#define AH_V2_STOP_TARGET_TIMEOUT 3u
#define AH_V2_STOP_ESTOP 4u
#define AH_V2_STOP_FAULT 5u
#define AH_V2_STOP_SESSION_RESET 6u

/* fault bits */
#define AH_V2_FAULT_WATCHDOG (1u << 0)
#define AH_V2_FAULT_ESTOP (1u << 1)
#define AH_V2_FAULT_OFFLINE (1u << 2)
#define AH_V2_FAULT_SEQ (1u << 3)
#define AH_V2_FAULT_OVERLOAD (1u << 4)
#define AH_V2_FAULT_OVERTEMP (1u << 5)
#define AH_V2_FAULT_CRC (1u << 6)
#define AH_V2_FAULT_PROTOCOL (1u << 7)

/* health flags */
#define AH_V2_HEALTH_FLAG_TORQUE (1u << 0)
#define AH_V2_HEALTH_FLAG_STOPPED (1u << 1)
#define AH_V2_HEALTH_FLAG_SESSION_OK (1u << 2)

/* operation types */
#define AH_V2_OP_RESET 1u
#define AH_V2_OP_CALIBRATE 2u
#define AH_V2_OP_APPLY_CONFIG 3u

/* accept status */
#define AH_V2_ACCEPT_OK 0u
#define AH_V2_ACCEPT_VERSION_MISMATCH 1u
#define AH_V2_ACCEPT_PROFILE_MISMATCH 2u
#define AH_V2_ACCEPT_BUSY 3u

/* payload sizes */
#define AH_V2_HELLO_SIZE 10u
#define AH_V2_ACCEPT_SIZE 11u
#define AH_V2_HEARTBEAT_SIZE 8u
#define AH_V2_EXECUTION_SIZE 83u
#define AH_V2_ACTUAL_SIZE 55u
#define AH_V2_HEALTH_SIZE 14u
#define AH_V2_OPERATION_HDR_SIZE 6u
#define AH_V2_INFO_SIZE 10u
#define AH_V2_OPERATION_MAX_DATA (AH_V2_MAX_PAYLOAD - AH_V2_OPERATION_HDR_SIZE)

typedef struct {
    uint8_t type;
    uint16_t seq;
    uint8_t len;
    uint8_t payload[AH_V2_MAX_PAYLOAD];
} ah_v2_frame_t;

typedef struct {
    uint8_t buf[AH_V2_PARSER_BUF];
    size_t len;
} ah_v2_parser_t;

typedef struct {
    uint8_t proto;
    uint8_t node_type;
    uint32_t session_id;
    uint16_t profile_rev;
    uint16_t caps;
} ah_v2_hello_t;

typedef struct {
    uint8_t proto;
    uint8_t status;
    uint32_t session_id;
    uint16_t fw_version;
    uint8_t servo_count;
    uint16_t caps;
} ah_v2_accept_t;

typedef struct {
    uint32_t session_id;
    uint32_t uptime_ms;
} ah_v2_heartbeat_t;

typedef struct {
    uint8_t mode;
    int16_t target[8];
    int16_t close[8];
    uint16_t load_threshold[8];
    uint16_t speed[8];
    uint16_t torque_limit[8];
    uint16_t target_timeout_ms;
} ah_v2_execution_t;

typedef struct {
    int16_t raw[8];
    int16_t load[8];
    int8_t temp_c[8];
    uint8_t online_mask;
    uint32_t sample_time_ms;
    uint16_t age_ms;
    uint8_t error_bits[8];
} ah_v2_actual_t;

typedef struct {
    uint8_t phase;
    uint8_t stop_reason;
    uint16_t fault_bits;
    uint8_t watchdog_state;
    uint8_t flags;
    uint16_t counters[4];
} ah_v2_health_t;

typedef struct {
    uint32_t op_id;
    uint8_t op_type;
    uint8_t status;
    uint8_t data[AH_V2_OPERATION_MAX_DATA];
    uint8_t data_len;
} ah_v2_operation_t;

typedef struct {
    uint8_t proto;
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t servo_count;
    uint16_t caps;
    uint32_t build_id;
} ah_v2_info_t;

uint16_t ah_v2_crc16(const uint8_t *data, size_t len);
size_t ah_v2_encode_frame(uint8_t type, uint16_t seq, const uint8_t *payload,
                          uint8_t len, uint8_t *out, size_t out_cap);
void ah_v2_parser_init(ah_v2_parser_t *parser);
size_t ah_v2_parser_feed(ah_v2_parser_t *parser, const uint8_t *data, size_t len,
                         ah_v2_frame_t *out, size_t max_frames);

size_t ah_v2_build_hello(uint16_t seq, const ah_v2_hello_t *v, uint8_t *out, size_t cap);
size_t ah_v2_build_accept(uint16_t seq, const ah_v2_accept_t *v, uint8_t *out, size_t cap);
size_t ah_v2_build_heartbeat(uint16_t seq, const ah_v2_heartbeat_t *v, uint8_t *out, size_t cap);
size_t ah_v2_build_execution(uint16_t seq, const ah_v2_execution_t *v, uint8_t *out, size_t cap);
size_t ah_v2_build_actual(uint16_t seq, const ah_v2_actual_t *v, uint8_t *out, size_t cap);
size_t ah_v2_build_health(uint16_t seq, const ah_v2_health_t *v, uint8_t *out, size_t cap);
size_t ah_v2_build_operation(uint16_t seq, const ah_v2_operation_t *v, uint8_t *out, size_t cap);
size_t ah_v2_build_info(uint16_t seq, const ah_v2_info_t *v, uint8_t *out, size_t cap);

int ah_v2_parse_hello(const uint8_t *payload, size_t len, ah_v2_hello_t *out);
int ah_v2_parse_accept(const uint8_t *payload, size_t len, ah_v2_accept_t *out);
int ah_v2_parse_heartbeat(const uint8_t *payload, size_t len, ah_v2_heartbeat_t *out);
int ah_v2_parse_execution(const uint8_t *payload, size_t len, ah_v2_execution_t *out);
int ah_v2_parse_actual(const uint8_t *payload, size_t len, ah_v2_actual_t *out);
int ah_v2_parse_health(const uint8_t *payload, size_t len, ah_v2_health_t *out);
int ah_v2_parse_operation(const uint8_t *payload, size_t len, ah_v2_operation_t *out);
int ah_v2_parse_info(const uint8_t *payload, size_t len, ah_v2_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* AH_PROTOCOL_V2_H */
