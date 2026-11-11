#ifndef AH_DEVICE_H
#define AH_DEVICE_H

#include <stdint.h>

#include "ah_protocol_v2.h"
#include "ah_scs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AH_DEV_SERVO_COUNT AH_SCS_SERVO_COUNT

typedef struct {
    uint32_t pregrasp_ms;
    uint32_t closing_timeout_ms;
    uint32_t release_ms;
    uint32_t target_timeout_ms;
    uint32_t link_timeout_ms;
    uint16_t load_threshold_default;
    int16_t open_target_default;
} ah_device_config_t;

typedef struct {
    ah_device_config_t cfg;
    uint8_t phase;
    uint8_t stop_reason;
    uint8_t estop;
    uint8_t link_seen;
    uint8_t torque_enabled;
    uint8_t watchdog_state;
    uint16_t fault_bits;
    int16_t target[AH_DEV_SERVO_COUNT];
    int16_t close_target[AH_DEV_SERVO_COUNT];
    uint16_t speed[AH_DEV_SERVO_COUNT];
    uint16_t torque_limit[AH_DEV_SERVO_COUNT];
    uint16_t load_threshold[AH_DEV_SERVO_COUNT];
    uint32_t phase_start_ms;
    uint32_t last_exec_ms;
    uint32_t last_link_ms;
    ah_v2_actual_t actual;
    uint16_t counters[4];
} ah_device_t;

void ah_device_config_default(ah_device_config_t *cfg);
void ah_device_init(ah_device_t *dev, const ah_device_config_t *cfg);
void ah_device_note_link(ah_device_t *dev, uint32_t now_ms);
int ah_device_apply_execution(ah_device_t *dev, const ah_v2_execution_t *exec, uint32_t now_ms);
void ah_device_step(ah_device_t *dev, const ah_scs_t *scs, uint32_t now_ms);
void ah_device_stop(ah_device_t *dev, uint8_t reason, uint32_t now_ms);
void ah_device_set_estop(ah_device_t *dev, uint8_t active, uint32_t now_ms);
int ah_device_reset(ah_device_t *dev);

void ah_device_get_targets(const ah_device_t *dev, int16_t out[AH_DEV_SERVO_COUNT]);
void ah_device_get_profile(const ah_device_t *dev, uint16_t speed[AH_DEV_SERVO_COUNT],
                           uint16_t torque_limit[AH_DEV_SERVO_COUNT]);
uint8_t ah_device_torque_enabled(const ah_device_t *dev);
void ah_device_get_actual(const ah_device_t *dev, ah_v2_actual_t *out);
void ah_device_get_health(const ah_device_t *dev, ah_v2_health_t *out);

#ifdef __cplusplus
}
#endif

#endif /* AH_DEVICE_H */
