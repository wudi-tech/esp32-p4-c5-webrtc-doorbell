#include "doorbell_ring_policy.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require_true(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void test_policy_decisions(void)
{
    require_true(doorbell_ring_policy_evaluate(false, false) ==
                     DOORBELL_RING_NOT_READY,
                 "not-ready takes precedence over busy");
    require_true(doorbell_ring_policy_evaluate(false, true) ==
                     DOORBELL_RING_NOT_READY,
                 "not-ready rejects even when idle");
    require_true(doorbell_ring_policy_evaluate(true, false) ==
                     DOORBELL_RING_BUSY,
                 "ready but busy is rejected");
    require_true(doorbell_ring_policy_evaluate(true, true) ==
                     DOORBELL_RING_ACCEPTED,
                 "ready and idle is accepted");
}

static void test_result_names(void)
{
    require_true(strcmp(doorbell_ring_result_name(DOORBELL_RING_ACCEPTED),
                        "ACCEPTED") == 0,
                 "accepted result name");
    require_true(strcmp(doorbell_ring_result_name(DOORBELL_RING_NOT_READY),
                        "NOT_READY") == 0,
                 "not-ready result name");
    require_true(strcmp(doorbell_ring_result_name(DOORBELL_RING_BUSY),
                        "BUSY") == 0,
                 "busy result name");
    require_true(strcmp(doorbell_ring_result_name(DOORBELL_RING_SEND_FAILED),
                        "SEND_FAILED") == 0,
                 "send-failed result name");
    require_true(strcmp(doorbell_ring_result_name(
                            (doorbell_ring_request_result_t)99),
                        "UNKNOWN") == 0,
                 "invalid result name");
    require_true(strcmp(doorbell_ring_result_name(
                            (doorbell_ring_request_result_t)-1),
                        "UNKNOWN") == 0,
                 "negative invalid result name");
}

int main(void)
{
    test_policy_decisions();
    test_result_names();
    puts("PASS: doorbell ring policy");
    return EXIT_SUCCESS;
}
