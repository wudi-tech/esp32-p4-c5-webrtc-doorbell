#ifndef DOORBELL_RING_POLICY_H
#define DOORBELL_RING_POLICY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOORBELL_RING_ACCEPTED = 0,
    DOORBELL_RING_NOT_READY,
    DOORBELL_RING_BUSY,
    DOORBELL_RING_SEND_FAILED,
} doorbell_ring_request_result_t;

doorbell_ring_request_result_t doorbell_ring_policy_evaluate(bool webrtc_ready,
                                                              bool doorbell_idle);
const char *doorbell_ring_result_name(doorbell_ring_request_result_t result);

#ifdef __cplusplus
}
#endif

#endif
