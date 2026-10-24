#include "door_lock_state.h"

door_lock_state_t door_lock_state_transition(door_lock_state_t state,
                                             door_lock_event_t event)
{
    if (event == DOOR_LOCK_EVENT_FAILURE) {
        return DOOR_LOCK_STATE_FAULT;
    }

    switch (state) {
        case DOOR_LOCK_STATE_LOCKED:
            if (event == DOOR_LOCK_EVENT_OPEN_REQUEST) {
                return DOOR_LOCK_STATE_OPENING;
            }
            break;
        case DOOR_LOCK_STATE_OPENING:
            if (event == DOOR_LOCK_EVENT_MOTION_COMPLETE) {
                return DOOR_LOCK_STATE_UNLOCKED;
            }
            break;
        case DOOR_LOCK_STATE_UNLOCKED:
            if (event == DOOR_LOCK_EVENT_HOLD_EXPIRED) {
                return DOOR_LOCK_STATE_CLOSING;
            }
            break;
        case DOOR_LOCK_STATE_CLOSING:
            if (event == DOOR_LOCK_EVENT_MOTION_COMPLETE) {
                return DOOR_LOCK_STATE_LOCKED;
            }
            break;
        case DOOR_LOCK_STATE_FAULT:
        default:
            break;
    }
    return state;
}

const char *door_lock_state_name(door_lock_state_t state)
{
    switch (state) {
        case DOOR_LOCK_STATE_LOCKED:
            return "LOCKED";
        case DOOR_LOCK_STATE_OPENING:
            return "OPENING";
        case DOOR_LOCK_STATE_UNLOCKED:
            return "UNLOCKED";
        case DOOR_LOCK_STATE_CLOSING:
            return "CLOSING";
        case DOOR_LOCK_STATE_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}
