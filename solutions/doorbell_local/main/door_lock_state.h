#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOOR_LOCK_STATE_LOCKED = 0,
    DOOR_LOCK_STATE_OPENING,
    DOOR_LOCK_STATE_UNLOCKED,
    DOOR_LOCK_STATE_CLOSING,
    DOOR_LOCK_STATE_FAULT,
} door_lock_state_t;

typedef enum {
    DOOR_LOCK_EVENT_OPEN_REQUEST = 0,
    DOOR_LOCK_EVENT_MOTION_COMPLETE,
    DOOR_LOCK_EVENT_HOLD_EXPIRED,
    DOOR_LOCK_EVENT_FAILURE,
} door_lock_event_t;

door_lock_state_t door_lock_state_transition(door_lock_state_t state,
                                             door_lock_event_t event);
const char *door_lock_state_name(door_lock_state_t state);

#ifdef __cplusplus
}
#endif
