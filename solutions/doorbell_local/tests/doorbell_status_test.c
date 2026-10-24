#include "doorbell_status.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    doorbell_status_snapshot_t status;
    doorbell_status_init(&status);
    assert(status.presence == DOORBELL_PRESENCE_UNKNOWN);
    assert(status.call == DOORBELL_CALL_IDLE);
    assert(status.lock == DOORBELL_LOCK_UNKNOWN);
    assert(status.network == DOORBELL_NETWORK_DISCONNECTED);
    assert(status.pedestrian_model == VISION_MODEL_INITIALIZING);
    assert(status.face_model == VISION_MODEL_INITIALIZING);

    vision_snapshot_t vision = {0};
    vision.update.confirmed_present = true;
    vision.update.confirmed_person_count = 1;
    vision.pedestrian_model = VISION_MODEL_READY;
    vision.face_model = VISION_MODEL_UNAVAILABLE;
    doorbell_status_apply_vision(&status, &vision);
    assert(status.presence == DOORBELL_PRESENCE_CONFIRMED);
    assert(status.confirmed_person_count == 1);
    assert(status.pedestrian_model == VISION_MODEL_READY);
    assert(status.face_model == VISION_MODEL_UNAVAILABLE);

    puts("PASS: display-independent doorbell status seam");
    return 0;
}
