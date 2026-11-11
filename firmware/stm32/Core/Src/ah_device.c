#include "ah_device.h"

#include <string.h>

#define AH_SCS_ERR_OVERLOAD (1u << 1)
#define AH_SCS_ERR_OVERHEAT (1u << 4)
#define AH_DEV_TEMP_LIMIT_C 70

static int16_t abs_i16(int16_t v)
{
    int32_t x = v;
    if (x < 0) {
        x = -x;
    }
    return (int16_t)((x > 32767) ? 32767 : x);
}

void ah_device_config_default(ah_device_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->pregrasp_ms = 300u;
    cfg->closing_timeout_ms = 3000u;
    cfg->release_ms = 500u;
    cfg->target_timeout_ms = 1000u;
    cfg->link_timeout_ms = 500u;
    cfg->load_threshold_default = 384u;
    cfg->open_target_default = 2048;
}

void ah_device_init(ah_device_t *dev, const ah_device_config_t *cfg)
{
    size_t i;
    if (dev == NULL) {
        return;
    }
    memset(dev, 0, sizeof(*dev));
    if (cfg != NULL) {
        dev->cfg = *cfg;
    } else {
        ah_device_config_default(&dev->cfg);
    }
    dev->phase = AH_V2_PHASE_IDLE;
    dev->stop_reason = AH_V2_STOP_NONE;
    for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
        dev->target[i] = dev->cfg.open_target_default;
        dev->close_target[i] = dev->cfg.open_target_default;
        dev->speed[i] = 0u;
        dev->torque_limit[i] = 0u;
        dev->load_threshold[i] = dev->cfg.load_threshold_default;
    }
}

void ah_device_note_link(ah_device_t *dev, uint32_t now_ms)
{
    if (dev != NULL) {
        dev->link_seen = 1u;
        dev->last_link_ms = now_ms;
    }
}

static void fill_actual(ah_device_t *dev, const ah_scs_t *scs, uint32_t now_ms)
{
    size_t i;
    uint8_t mask = 0u;
    if (dev == NULL || scs == NULL) {
        return;
    }
    for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
        const ah_scs_feedback_t *fb = &scs->fb[i];
        dev->actual.raw[i] = fb->raw;
        dev->actual.load[i] = fb->load;
        dev->actual.temp_c[i] = fb->temp_c;
        dev->actual.error_bits[i] = fb->error;
        if (fb->online != 0u) {
            mask = (uint8_t)(mask | (1u << i));
        }
    }
    dev->actual.online_mask = mask;
    dev->actual.age_ms = 0u;
    for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
        if (scs->fb[i].age_ms > dev->actual.age_ms) {
            dev->actual.age_ms = scs->fb[i].age_ms;
        }
    }
    dev->actual.sample_time_ms = now_ms - dev->actual.age_ms;
}

void ah_device_stop(ah_device_t *dev, uint8_t reason, uint32_t now_ms)
{
    if (dev == NULL) {
        return;
    }
    dev->phase = AH_V2_PHASE_FAULT;
    dev->stop_reason = reason;
    dev->torque_enabled = 0u;
    dev->last_exec_ms = 0u;
    (void)now_ms;
    if (reason == AH_V2_STOP_LINK_TIMEOUT || reason == AH_V2_STOP_TARGET_TIMEOUT) {
        dev->fault_bits = (uint16_t)(dev->fault_bits | AH_V2_FAULT_WATCHDOG);
        dev->watchdog_state = (reason == AH_V2_STOP_LINK_TIMEOUT) ? 2u : 1u;
    } else if (reason == AH_V2_STOP_ESTOP) {
        dev->fault_bits = (uint16_t)(dev->fault_bits | AH_V2_FAULT_ESTOP);
        dev->watchdog_state = 3u;
    } else if (reason == AH_V2_STOP_FAULT) {
        dev->fault_bits = (uint16_t)(dev->fault_bits | AH_V2_FAULT_PROTOCOL);
        dev->watchdog_state = 4u;
    } else if (reason == AH_V2_STOP_USER) {
        dev->watchdog_state = 0u;
    }
    dev->counters[3] = (uint16_t)(dev->counters[3] + 1u);
}

int ah_device_apply_execution(ah_device_t *dev, const ah_v2_execution_t *exec, uint32_t now_ms)
{
    size_t i;
    if (dev == NULL || exec == NULL || dev->estop != 0u || dev->phase == AH_V2_PHASE_FAULT) {
        return -1;
    }
    if (exec->mode == AH_V2_MODE_STOP) {
        ah_device_stop(dev, AH_V2_STOP_USER, now_ms);
        return 0;
    }
    if (exec->mode != AH_V2_MODE_OPEN && exec->mode != AH_V2_MODE_GRASP &&
        exec->mode != AH_V2_MODE_HOLD && exec->mode != AH_V2_MODE_RELEASE) {
        return -1;
    }
    for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
        if (exec->target[i] < 0 || exec->target[i] > 4095 ||
            exec->close[i] < 0 || exec->close[i] > 4095) {
            return -1;
        }
    }
    dev->fault_bits = 0u;
    dev->stop_reason = AH_V2_STOP_NONE;
    dev->watchdog_state = 0u;
    dev->torque_enabled = 1u;
    dev->last_exec_ms = now_ms;
    dev->phase_start_ms = now_ms;
    for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
        dev->target[i] = exec->target[i];
        dev->close_target[i] = exec->close[i];
        dev->speed[i] = exec->speed[i];
        dev->torque_limit[i] = exec->torque_limit[i];
        dev->load_threshold[i] = (exec->load_threshold[i] == 0u)
                                     ? dev->cfg.load_threshold_default
                                     : exec->load_threshold[i];
    }
    if (exec->mode == AH_V2_MODE_GRASP) {
        dev->phase = AH_V2_PHASE_PREGRASP;
    } else if (exec->mode == AH_V2_MODE_HOLD) {
        dev->phase = AH_V2_PHASE_HOLD;
    } else {
        dev->phase = AH_V2_PHASE_RELEASING;
    }
    dev->counters[0] = (uint16_t)(dev->counters[0] + 1u);
    return 0;
}

static int any_contact(const ah_device_t *dev)
{
    size_t i;
    for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
        if ((uint16_t)abs_i16(dev->actual.load[i]) >= dev->load_threshold[i] &&
            (dev->actual.online_mask & (1u << i)) != 0u) {
            return 1;
        }
    }
    return 0;
}

void ah_device_step(ah_device_t *dev, const ah_scs_t *scs, uint32_t now_ms)
{
    size_t i;
    uint32_t elapsed;
    if (dev == NULL || scs == NULL) {
        return;
    }
    fill_actual(dev, scs, now_ms);

    if (dev->phase == AH_V2_PHASE_FAULT) {
        return;
    }
    if (dev->phase == AH_V2_PHASE_IDLE) {
        return;
    }

    if (dev->link_seen != 0u && (now_ms - dev->last_link_ms) > dev->cfg.link_timeout_ms) {
        ah_device_stop(dev, AH_V2_STOP_LINK_TIMEOUT, now_ms);
        return;
    }

    for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
        if (dev->actual.online_mask & (1u << i)) {
            if (dev->actual.temp_c[i] > AH_DEV_TEMP_LIMIT_C ||
                (dev->actual.error_bits[i] & AH_SCS_ERR_OVERHEAT) != 0u) {
                dev->fault_bits = (uint16_t)(dev->fault_bits | AH_V2_FAULT_OVERTEMP);
                ah_device_stop(dev, AH_V2_STOP_FAULT, now_ms);
                return;
            }
            if ((dev->actual.error_bits[i] & AH_SCS_ERR_OVERLOAD) != 0u) {
                dev->fault_bits = (uint16_t)(dev->fault_bits | AH_V2_FAULT_OVERLOAD);
                ah_device_stop(dev, AH_V2_STOP_FAULT, now_ms);
                return;
            }
        } else {
            dev->fault_bits = (uint16_t)(dev->fault_bits | AH_V2_FAULT_OFFLINE);
            ah_device_stop(dev, AH_V2_STOP_FAULT, now_ms);
            return;
        }
    }

    if (dev->last_exec_ms != 0u &&
        (now_ms - dev->last_exec_ms) > dev->cfg.target_timeout_ms) {
        ah_device_stop(dev, AH_V2_STOP_TARGET_TIMEOUT, now_ms);
        return;
    }

    elapsed = now_ms - dev->phase_start_ms;
    if (dev->phase == AH_V2_PHASE_PREGRASP) {
        if (elapsed >= dev->cfg.pregrasp_ms) {
            for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
                dev->target[i] = dev->close_target[i];
            }
            dev->phase = AH_V2_PHASE_CLOSING;
            dev->phase_start_ms = now_ms;
        }
    } else if (dev->phase == AH_V2_PHASE_CLOSING) {
        if (any_contact(dev)) {
            for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
                dev->target[i] = dev->actual.raw[i];
            }
            dev->phase = AH_V2_PHASE_HOLD;
            dev->phase_start_ms = now_ms;
            dev->counters[1] = (uint16_t)(dev->counters[1] + 1u);
        } else if (elapsed >= dev->cfg.closing_timeout_ms) {
            ah_device_stop(dev, AH_V2_STOP_TARGET_TIMEOUT, now_ms);
        }
    } else if (dev->phase == AH_V2_PHASE_RELEASING) {
        if (elapsed >= dev->cfg.release_ms) {
            dev->phase = AH_V2_PHASE_IDLE;
            dev->torque_enabled = 0u;
            dev->last_exec_ms = 0u;
        }
    }
}

void ah_device_set_estop(ah_device_t *dev, uint8_t active, uint32_t now_ms)
{
    if (dev == NULL) {
        return;
    }
    if (active != 0u) {
        dev->estop = 1u;
        ah_device_stop(dev, AH_V2_STOP_ESTOP, now_ms);
    } else {
        dev->estop = 0u;
    }
}

int ah_device_reset(ah_device_t *dev)
{
    size_t i;
    if (dev == NULL || dev->estop != 0u) {
        return -1;
    }
    dev->phase = AH_V2_PHASE_IDLE;
    dev->stop_reason = AH_V2_STOP_NONE;
    dev->fault_bits = 0u;
    dev->watchdog_state = 0u;
    dev->torque_enabled = 0u;
    dev->last_exec_ms = 0u;
    dev->actual.age_ms = 0u;
    for (i = 0; i < AH_DEV_SERVO_COUNT; ++i) {
        dev->target[i] = dev->cfg.open_target_default;
        dev->close_target[i] = dev->cfg.open_target_default;
        dev->speed[i] = 0u;
        dev->torque_limit[i] = 0u;
        dev->load_threshold[i] = dev->cfg.load_threshold_default;
    }
    return 0;
}

void ah_device_get_targets(const ah_device_t *dev, int16_t out[AH_DEV_SERVO_COUNT])
{
    if (dev != NULL && out != NULL) {
        memcpy(out, dev->target, sizeof(dev->target));
    }
}

void ah_device_get_profile(const ah_device_t *dev, uint16_t speed[AH_DEV_SERVO_COUNT],
                           uint16_t torque_limit[AH_DEV_SERVO_COUNT])
{
    if (dev != NULL && speed != NULL && torque_limit != NULL) {
        memcpy(speed, dev->speed, sizeof(dev->speed));
        memcpy(torque_limit, dev->torque_limit, sizeof(dev->torque_limit));
    }
}

uint8_t ah_device_torque_enabled(const ah_device_t *dev)
{
    return (dev != NULL) ? dev->torque_enabled : 0u;
}

void ah_device_get_actual(const ah_device_t *dev, ah_v2_actual_t *out)
{
    if (dev != NULL && out != NULL) {
        *out = dev->actual;
    }
}

void ah_device_get_health(const ah_device_t *dev, ah_v2_health_t *out)
{
    size_t i;
    if (dev == NULL || out == NULL) {
        return;
    }
    out->phase = dev->phase;
    out->stop_reason = dev->stop_reason;
    out->fault_bits = dev->fault_bits;
    out->watchdog_state = dev->watchdog_state;
    out->flags = 0u;
    if (dev->torque_enabled != 0u) {
        out->flags = (uint8_t)(out->flags | AH_V2_HEALTH_FLAG_TORQUE);
    } else if (dev->phase == AH_V2_PHASE_IDLE || dev->phase == AH_V2_PHASE_FAULT) {
        out->flags = (uint8_t)(out->flags | AH_V2_HEALTH_FLAG_STOPPED);
    }
    for (i = 0; i < 4u; ++i) {
        out->counters[i] = dev->counters[i];
    }
}
