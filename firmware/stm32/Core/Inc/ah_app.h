#ifndef AH_APP_H
#define AH_APP_H

#include <stdint.h>

#include "ah_device.h"
#include "ah_protocol_v2.h"
#include "ah_scs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AH_APP_TX_BUF 128u

typedef struct {
    ah_scs_t scs;
    ah_device_t dev;
    ah_v2_parser_t parser;
    uint32_t now_ms;
    uint32_t last_scs_ms;
    uint32_t last_telemetry_ms;
    uint32_t last_heartbeat_ms;
    uint16_t tx_seq;
    uint32_t session_id;
    uint16_t profile_rev;
    uint8_t session_ok;
    uint8_t control_mode;
} ah_app_t;

void ah_app_init(ah_app_t *app, const ah_device_config_t *cfg);
void ah_app_tick(ah_app_t *app, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* AH_APP_H */
