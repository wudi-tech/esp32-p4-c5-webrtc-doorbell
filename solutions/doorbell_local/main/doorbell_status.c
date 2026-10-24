#include "doorbell_status.h"

#include <stddef.h>
#include <string.h>

void doorbell_status_init(doorbell_status_snapshot_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->presence = DOORBELL_PRESENCE_UNKNOWN;
    status->call = DOORBELL_CALL_IDLE;
    status->lock = DOORBELL_LOCK_UNKNOWN;
    status->network = DOORBELL_NETWORK_DISCONNECTED;
    status->pedestrian_model = VISION_MODEL_INITIALIZING;
    status->face_model = VISION_MODEL_INITIALIZING;
}

void doorbell_status_apply_vision(doorbell_status_snapshot_t *status,
                                  const vision_snapshot_t *vision)
{
    if (status == NULL || vision == NULL) {
        return;
    }
    status->presence = vision->update.confirmed_present
                           ? DOORBELL_PRESENCE_CONFIRMED
                           : (vision->update.target_count > 0U
                                  ? DOORBELL_PRESENCE_CANDIDATE
                                  : DOORBELL_PRESENCE_CLEAR);
    status->confirmed_person_count =
        (uint8_t)vision->update.confirmed_person_count;
    status->pedestrian_model = vision->pedestrian_model;
    status->face_model = vision->face_model;
}
