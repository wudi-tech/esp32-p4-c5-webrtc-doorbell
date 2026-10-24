#pragma once

#include "vision_event.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOORBELL_PRESENCE_UNKNOWN = 0,
    DOORBELL_PRESENCE_CLEAR,
    DOORBELL_PRESENCE_CANDIDATE,
    DOORBELL_PRESENCE_CONFIRMED,
} doorbell_presence_state_t;

typedef enum {
    DOORBELL_CALL_IDLE = 0,
    DOORBELL_CALL_RINGING,
    DOORBELL_CALL_ACTIVE,
} doorbell_call_state_t;

typedef enum {
    DOORBELL_LOCK_UNKNOWN = 0,
    DOORBELL_LOCK_LOCKED,
    DOORBELL_LOCK_OPENING,
    DOORBELL_LOCK_OPEN,
} doorbell_lock_status_t;

typedef enum {
    DOORBELL_NETWORK_DISCONNECTED = 0,
    DOORBELL_NETWORK_CONNECTING,
    DOORBELL_NETWORK_CONNECTED,
} doorbell_network_state_t;

typedef struct {
    doorbell_presence_state_t presence;
    uint8_t confirmed_person_count;
    doorbell_call_state_t call;
    doorbell_lock_status_t lock;
    doorbell_network_state_t network;
    vision_model_state_t pedestrian_model;
    vision_model_state_t face_model;
} doorbell_status_snapshot_t;

void doorbell_status_init(doorbell_status_snapshot_t *status);
void doorbell_status_apply_vision(doorbell_status_snapshot_t *status,
                                  const vision_snapshot_t *vision);

#ifdef __cplusplus
}
#endif
