#include "presence_fusion.h"

#include <string.h>

presence_fusion_config_t presence_fusion_default_config(void)
{
    presence_fusion_config_t config = {
        .x_min_mm = -1500, .x_max_mm = 1500,
        .y_min_mm = 300, .y_max_mm = 3000,
        .enter_confirm_ms = 1000U, .leave_confirm_ms = 2000U,
        .cooldown_ms = 10000U, .direction_deadband_cm_s = 10,
    };
    return config;
}

bool presence_fusion_init(presence_fusion_t *fusion,
                          const presence_fusion_config_t *config)
{
    if (fusion == NULL || config == NULL) {
        return false;
    }
    presence_fusion_config_t selected = *config;
    if (selected.x_min_mm > selected.x_max_mm ||
        selected.y_min_mm > selected.y_max_mm || selected.enter_confirm_ms == 0U ||
        selected.leave_confirm_ms == 0U || selected.direction_deadband_cm_s < 0) {
        return false;
    }
    memset(fusion, 0, sizeof(*fusion));
    fusion->config = selected;
    fusion->state = PRESENCE_STATE_CLEAR;
    return true;
}

bool presence_fusion_target_in_zone(const presence_fusion_config_t *config,
                                    const ld2450_target_t *target)
{
    return config != NULL && target != NULL && target->valid &&
           target->x_mm >= config->x_min_mm && target->x_mm <= config->x_max_mm &&
           target->y_mm >= config->y_min_mm && target->y_mm <= config->y_max_mm;
}

presence_direction_t presence_fusion_classify_direction(
    const presence_fusion_config_t *config, int16_t speed_cm_s)
{
    if (config == NULL || config->direction_deadband_cm_s < 0) {
        return PRESENCE_DIRECTION_UNKNOWN;
    }
    if (speed_cm_s < -config->direction_deadband_cm_s) {
        return PRESENCE_DIRECTION_APPROACHING;
    }
    if (speed_cm_s > config->direction_deadband_cm_s) {
        return PRESENCE_DIRECTION_LEAVING;
    }
    return PRESENCE_DIRECTION_STATIONARY;
}

static presence_fusion_result_t empty_result(void)
{
    presence_fusion_result_t result;
    memset(&result, 0, sizeof(result));
    result.state = PRESENCE_STATE_CLEAR;
    result.event = PRESENCE_EVENT_NONE;
    result.direction = PRESENCE_DIRECTION_UNKNOWN;
    result.representative_slot = SIZE_MAX;
    return result;
}

presence_fusion_result_t presence_fusion_process(presence_fusion_t *fusion,
                                                 const ld2450_frame_t *frame,
                                                 uint32_t now_ms)
{
    presence_fusion_result_t result = empty_result();
    if (fusion == NULL) {
        return result;
    }
    result.state = fusion->state;
    if (frame == NULL) {
        return result;
    }
    if (fusion->cooldown_active &&
        (uint32_t)(now_ms - fusion->cooldown_started_ms) >= fusion->config.cooldown_ms) {
        fusion->cooldown_active = false;
    }

    size_t representative = LD2450_MAX_TARGETS;
    for (size_t i = 0; i < LD2450_MAX_TARGETS; ++i) {
        if (!presence_fusion_target_in_zone(&fusion->config, &frame->targets[i])) {
            continue;
        }
        ++result.in_zone_count;
        if (representative == LD2450_MAX_TARGETS ||
            frame->targets[i].y_mm < frame->targets[representative].y_mm) {
            representative = i;
        }
    }
    const bool occupied = result.in_zone_count != 0U;
    if (occupied) {
        result.has_representative_target = true;
        result.representative_slot = representative;
        result.representative_target = frame->targets[representative];
        result.direction = presence_fusion_classify_direction(
            &fusion->config, result.representative_target.speed_cm_s);
    }

    switch (fusion->state) {
    case PRESENCE_STATE_CLEAR:
        if (occupied) {
            fusion->state = PRESENCE_STATE_ENTERING;
            fusion->state_since_ms = now_ms;
            result.state_changed = true;
        }
        break;
    case PRESENCE_STATE_ENTERING:
        if (!occupied) {
            fusion->state = PRESENCE_STATE_CLEAR;
            result.state_changed = true;
        } else if ((uint32_t)(now_ms - fusion->state_since_ms) >= fusion->config.enter_confirm_ms) {
            fusion->state = PRESENCE_STATE_PRESENT;
            result.event = PRESENCE_EVENT_ENTER_CONFIRMED;
            result.state_changed = true;
            result.ring_requested = !fusion->cooldown_active;
        }
        break;
    case PRESENCE_STATE_PRESENT:
        if (!occupied) {
            fusion->state = PRESENCE_STATE_EXITING;
            fusion->state_since_ms = now_ms;
            result.state_changed = true;
        }
        break;
    case PRESENCE_STATE_EXITING:
        if (occupied) {
            fusion->state = PRESENCE_STATE_PRESENT;
            result.state_changed = true;
        } else if ((uint32_t)(now_ms - fusion->state_since_ms) >= fusion->config.leave_confirm_ms) {
            fusion->state = PRESENCE_STATE_CLEAR;
            fusion->cooldown_active = true;
            fusion->cooldown_started_ms = now_ms;
            result.event = PRESENCE_EVENT_LEAVE_CONFIRMED;
            result.state_changed = true;
        }
        break;
    default:
        fusion->state = PRESENCE_STATE_CLEAR;
        result.state_changed = true;
        break;
    }
    result.state = fusion->state;
    return result;
}

const char *presence_state_name(presence_state_t state)
{
    switch (state) {
    case PRESENCE_STATE_CLEAR: return "CLEAR";
    case PRESENCE_STATE_ENTERING: return "ENTERING";
    case PRESENCE_STATE_PRESENT: return "PRESENT";
    case PRESENCE_STATE_EXITING: return "EXITING";
    default: return "UNKNOWN";
    }
}

const char *presence_event_name(presence_event_t event)
{
    switch (event) {
    case PRESENCE_EVENT_NONE: return "NONE";
    case PRESENCE_EVENT_ENTER_CONFIRMED: return "ENTER_CONFIRMED";
    case PRESENCE_EVENT_LEAVE_CONFIRMED: return "LEAVE_CONFIRMED";
    default: return "UNKNOWN";
    }
}

const char *presence_direction_name(presence_direction_t direction)
{
    switch (direction) {
    case PRESENCE_DIRECTION_UNKNOWN: return "UNKNOWN";
    case PRESENCE_DIRECTION_APPROACHING: return "APPROACHING";
    case PRESENCE_DIRECTION_STATIONARY: return "STATIONARY";
    case PRESENCE_DIRECTION_LEAVING: return "LEAVING";
    default: return "UNKNOWN";
    }
}
