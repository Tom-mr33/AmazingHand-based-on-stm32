#ifndef AH_SCS_H
#define AH_SCS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AH_SCS_SERVO_COUNT 8u
#define AH_SCS_SLOT_MS 2u
#define AH_SCS_READS_PER_SLOT 4u
#define AH_SCS_OFFLINE_MS 100u
#define AH_SCS_SYNC_FRAME_SIZE 32u
#define AH_SCS_SYNC_TORQUE_SIZE 24u
#define AH_SCS_SYNC_PROFILE_SIZE 48u
#define AH_SCS_READ_FRAME_SIZE 8u
#define AH_SCS_STATUS_SIZE 14u

typedef struct {
    int16_t raw;
    int16_t load;
    int8_t temp_c;
    uint8_t error;
    uint8_t valid;
    uint8_t online;
    uint32_t last_good_ms;
    uint16_t age_ms;
    uint16_t timeout_count;
} ah_scs_feedback_t;

typedef struct {
    ah_scs_feedback_t fb[AH_SCS_SERVO_COUNT];
    int16_t target[AH_SCS_SERVO_COUNT];
    uint16_t speed[AH_SCS_SERVO_COUNT];
    uint16_t torque_limit[AH_SCS_SERVO_COUNT];
    uint32_t now_ms;
    uint8_t slot;
    uint8_t torque_enabled;
    uint8_t torque_dirty;
    uint8_t profile_dirty;
    uint32_t write_count;
    uint32_t read_count;
    uint32_t timeout_count;
    uint32_t crc_error_count;
} ah_scs_t;

void ah_scs_init(ah_scs_t *scs);
void ah_scs_set_targets(ah_scs_t *scs, const int16_t target[AH_SCS_SERVO_COUNT]);
void ah_scs_set_profile(ah_scs_t *scs, const uint16_t speed[AH_SCS_SERVO_COUNT],
                        const uint16_t torque_limit[AH_SCS_SERVO_COUNT]);
void ah_scs_set_torque(ah_scs_t *scs, uint8_t enabled);
void ah_scs_tick(ah_scs_t *scs, uint32_t now_ms);

size_t ah_scs_build_sync_write(const int16_t target[AH_SCS_SERVO_COUNT],
                               uint8_t *out, size_t cap);
size_t ah_scs_build_sync_torque(uint8_t enabled, uint8_t *out, size_t cap);
size_t ah_scs_build_sync_profile(const uint16_t speed[AH_SCS_SERVO_COUNT],
                                 const uint16_t torque_limit[AH_SCS_SERVO_COUNT],
                                 uint8_t *out, size_t cap);
size_t ah_scs_build_read(uint8_t servo_id, uint8_t *out, size_t cap);
int ah_scs_parse_status(const uint8_t *frame, size_t len, ah_scs_feedback_t *fb);

#ifdef __cplusplus
}
#endif

#endif /* AH_SCS_H */
