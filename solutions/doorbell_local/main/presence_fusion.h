#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "radar_ld2450.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRESENCE_STATE_CLEAR = 0,
    PRESENCE_STATE_ENTERING,
    PRESENCE_STATE_PRESENT,
    PRESENCE_STATE_EXITING,
} presence_state_t;

typedef enum {
    PRESENCE_EVENT_NONE = 0,
    PRESENCE_EVENT_ENTER_CONFIRMED,
    PRESENCE_EVENT_LEAVE_CONFIRMED,
} presence_event_t;

typedef enum {
    PRESENCE_DIRECTION_UNKNOWN = 0,
    PRESENCE_DIRECTION_APPROACHING,
    PRESENCE_DIRECTION_STATIONARY,
    PRESENCE_DIRECTION_LEAVING,
} presence_direction_t;

typedef struct {
    int16_t x_min_mm;
    int16_t x_max_mm;
    int16_t y_min_mm;
    int16_t y_max_mm;
    uint32_t enter_confirm_ms;
    uint32_t leave_confirm_ms;
    uint32_t cooldown_ms;
    int16_t direction_deadband_cm_s;
} presence_fusion_config_t;

typedef struct {
    presence_fusion_config_t config;
    presence_state_t state;
    uint32_t state_since_ms;
    bool cooldown_active;
    uint32_t cooldown_started_ms;
} presence_fusion_t;

typedef struct {
    presence_state_t state;
    presence_event_t event;
    bool state_changed;
    bool ring_requested;
    size_t in_zone_count;
    bool has_representative_target;
    size_t representative_slot;
    ld2450_target_t representative_target;
    presence_direction_t direction;
} presence_fusion_result_t;

presence_fusion_config_t presence_fusion_default_config(void);
bool presence_fusion_init(presence_fusion_t *fusion,
                          const presence_fusion_config_t *config);
bool presence_fusion_target_in_zone(const presence_fusion_config_t *config,
                                    const ld2450_target_t *target);
presence_direction_t presence_fusion_classify_direction(
    const presence_fusion_config_t *config, int16_t speed_cm_s);
presence_fusion_result_t presence_fusion_process(presence_fusion_t *fusion,
                                                 const ld2450_frame_t *frame,
                                                 uint32_t now_ms);
const char *presence_state_name(presence_state_t state);
const char *presence_event_name(presence_event_t event);
const char *presence_direction_name(presence_direction_t direction);

#ifdef __cplusplus
}
#endif
