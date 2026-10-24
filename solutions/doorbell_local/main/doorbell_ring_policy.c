#include "doorbell_ring_policy.h"

doorbell_ring_request_result_t doorbell_ring_policy_evaluate(bool webrtc_ready,
                                                              bool doorbell_idle)
{
    if (!webrtc_ready) {
        return DOORBELL_RING_NOT_READY;
    }
    if (!doorbell_idle) {
        return DOORBELL_RING_BUSY;
    }
    return DOORBELL_RING_ACCEPTED;
}

const char *doorbell_ring_result_name(doorbell_ring_request_result_t result)
{
    switch (result) {
    case DOORBELL_RING_ACCEPTED:
        return "ACCEPTED";
    case DOORBELL_RING_NOT_READY:
        return "NOT_READY";
    case DOORBELL_RING_BUSY:
        return "BUSY";
    case DOORBELL_RING_SEND_FAILED:
        return "SEND_FAILED";
    default:
        return "UNKNOWN";
    }
}
