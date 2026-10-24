#include "presence_fusion.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static ld2450_frame_t frame_with_target(size_t slot, int16_t x_mm,
                                        int16_t y_mm, int16_t speed_cm_s)
{
    ld2450_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    require_true(slot < LD2450_MAX_TARGETS, "target slot is in range");
    frame.targets[slot].valid = true;
    frame.targets[slot].x_mm = x_mm;
    frame.targets[slot].y_mm = y_mm;
    frame.targets[slot].speed_cm_s = speed_cm_s;
    frame.target_count = 1;
    return frame;
}

static ld2450_frame_t frame_with_two_targets(void)
{
    ld2450_frame_t frame = frame_with_target(0, 0, 1000, 0);
    frame.targets[2].valid = true;
    frame.targets[2].x_mm = 10;
    frame.targets[2].y_mm = 1100;
    frame.targets[2].speed_cm_s = 0;
    frame.target_count = 2;
    return frame;
}

static presence_fusion_result_t confirm_entry(presence_fusion_t *fusion,
                                               const ld2450_frame_t *frame,
                                               uint32_t start_ms)
{
    (void)presence_fusion_process(fusion, frame, start_ms);
    return presence_fusion_process(fusion, frame, start_ms + 1000U);
}

static presence_fusion_result_t confirm_leave(presence_fusion_t *fusion,
                                              uint32_t start_ms)
{
    ld2450_frame_t empty;
    memset(&empty, 0, sizeof(empty));
    (void)presence_fusion_process(fusion, &empty, start_ms);
    presence_fusion_result_t before_confirmation =
        presence_fusion_process(fusion, &empty, start_ms + 1999U);
    require_true(before_confirmation.state == PRESENCE_STATE_EXITING &&
                     before_confirmation.event == PRESENCE_EVENT_NONE &&
                     !before_confirmation.state_changed &&
                     !before_confirmation.ring_requested,
                 "leave is not confirmed before 2000 ms");
    return presence_fusion_process(fusion, &empty, start_ms + 2000U);
}

static void test_defaults_and_boundaries(void)
{
    presence_fusion_config_t config = presence_fusion_default_config();
    require_true(config.x_min_mm == -1500 && config.x_max_mm == 1500,
                 "default x bounds");
    require_true(config.y_min_mm == 300 && config.y_max_mm == 3000,
                 "default y bounds");
    require_true(config.enter_confirm_ms == 1000U && config.leave_confirm_ms == 2000U,
                 "default entry and leave times");
    require_true(config.cooldown_ms == 10000U &&
                     config.direction_deadband_cm_s == 10,
                 "default cooldown and direction deadband");

    ld2450_frame_t lower = frame_with_target(0, -1500, 300, 0);
    ld2450_frame_t upper = frame_with_target(0, 1500, 3000, 0);
    ld2450_frame_t outside_x_low = frame_with_target(0, -1501, 1000, 0);
    ld2450_frame_t outside_x_high = frame_with_target(0, 1501, 1000, 0);
    ld2450_frame_t outside_y_low = frame_with_target(0, 0, 299, 0);
    ld2450_frame_t outside_y_high = frame_with_target(0, 0, 3001, 0);
    require_true(presence_fusion_target_in_zone(&config, &lower.targets[0]),
                 "zone includes lower boundaries");
    require_true(presence_fusion_target_in_zone(&config, &upper.targets[0]),
                 "zone includes upper boundaries");
    require_true(!presence_fusion_target_in_zone(&config, &outside_x_low.targets[0]) &&
                     !presence_fusion_target_in_zone(&config, &outside_x_high.targets[0]),
                 "zone excludes x below and above bounds");
    require_true(!presence_fusion_target_in_zone(&config, &outside_y_low.targets[0]) &&
                     !presence_fusion_target_in_zone(&config, &outside_y_high.targets[0]),
                 "zone excludes y below and above bounds");
}

static void test_direction_classification(void)
{
    presence_fusion_config_t config = presence_fusion_default_config();
    require_true(presence_fusion_classify_direction(&config, -11) ==
                     PRESENCE_DIRECTION_APPROACHING,
                 "speed below deadband is approaching");
    require_true(presence_fusion_classify_direction(&config, -10) ==
                     PRESENCE_DIRECTION_STATIONARY &&
                     presence_fusion_classify_direction(&config, 10) ==
                         PRESENCE_DIRECTION_STATIONARY,
                 "deadband endpoints are stationary");
    require_true(presence_fusion_classify_direction(&config, 11) ==
                     PRESENCE_DIRECTION_LEAVING,
                 "speed above deadband is leaving");
}

static void test_entry_presence_and_representative_target(void)
{
    presence_fusion_t fusion;
    presence_fusion_config_t config = presence_fusion_default_config();
    presence_fusion_init(&fusion, &config);
    ld2450_frame_t frame = frame_with_target(1, 250, 1200, -25);

    presence_fusion_result_t result = presence_fusion_process(&fusion, &frame,
                                                                100);
    require_true(result.state == PRESENCE_STATE_ENTERING && result.state_changed,
                 "first in-zone sample enters ENTERING");
    result = presence_fusion_process(&fusion, &frame, 1099);
    require_true(result.event == PRESENCE_EVENT_NONE &&
                     result.state == PRESENCE_STATE_ENTERING &&
                     !result.state_changed,
                 "entry is not confirmed before 1000 ms");
    result = presence_fusion_process(&fusion, &frame, 1100);
    require_true(result.event == PRESENCE_EVENT_ENTER_CONFIRMED &&
                     result.state == PRESENCE_STATE_PRESENT &&
                     result.ring_requested && result.state_changed,
                 "entry confirms at exactly 1000 ms");
    require_true(result.representative_slot == 1 && result.representative_target.x_mm == 250 &&
                     result.direction == PRESENCE_DIRECTION_APPROACHING,
                 "representative target and direction are reported");
    result = presence_fusion_process(&fusion, &frame, 1500);
    require_true(result.event == PRESENCE_EVENT_NONE && result.state == PRESENCE_STATE_PRESENT &&
                     !result.ring_requested && !result.state_changed,
                 "continued presence initially has no second event or ring");
    result = presence_fusion_process(&fusion, &frame, 20000);
    require_true(result.event == PRESENCE_EVENT_NONE && result.state == PRESENCE_STATE_PRESENT &&
                     !result.ring_requested && !result.state_changed,
                 "continued presence beyond cooldown has no second event or ring");
}

static void test_interruption_dropout_and_leave(void)
{
    presence_fusion_t fusion;
    presence_fusion_config_t config = presence_fusion_default_config();
    presence_fusion_init(&fusion, &config);
    ld2450_frame_t frame = frame_with_target(0, 0, 1000, 0);
    (void)presence_fusion_process(&fusion, &frame, 100);
    presence_fusion_result_t result = presence_fusion_process(&fusion, &(ld2450_frame_t){0}, 500);
    require_true(result.state == PRESENCE_STATE_CLEAR && result.state_changed,
                 "interrupted entering resets timer");
    result = presence_fusion_process(&fusion, &frame, 1000);
    require_true(result.state == PRESENCE_STATE_ENTERING && result.state_changed,
                 "re-entry starts a new entering timer");
    result = presence_fusion_process(&fusion, &frame, 1999);
    require_true(result.state == PRESENCE_STATE_ENTERING &&
                     result.event == PRESENCE_EVENT_NONE && !result.state_changed,
                 "re-entry is not confirmed on the discarded timer");
    result = presence_fusion_process(&fusion, &frame, 2000);
    require_true(result.state == PRESENCE_STATE_PRESENT &&
                     result.event == PRESENCE_EVENT_ENTER_CONFIRMED &&
                     result.state_changed,
                 "re-entry confirms after its new 1000 ms timer");
    result = presence_fusion_process(&fusion, &(ld2450_frame_t){0}, 2200);
    require_true(result.state == PRESENCE_STATE_EXITING && result.state_changed,
                 "dropout starts exiting");
    result = presence_fusion_process(&fusion, &frame, 3000);
    require_true(result.state == PRESENCE_STATE_PRESENT && result.event == PRESENCE_EVENT_NONE &&
                     result.state_changed && !result.ring_requested,
                 "short dropout returns to present");
    result = confirm_leave(&fusion, 4000);
    require_true(result.event == PRESENCE_EVENT_LEAVE_CONFIRMED &&
                     result.state == PRESENCE_STATE_CLEAR && result.state_changed,
                 "leave confirms at exactly 2000 ms");
}

static void test_cooldown_slot_and_wraparound(void)
{
    presence_fusion_t fusion;
    presence_fusion_config_t config = presence_fusion_default_config();
    presence_fusion_init(&fusion, &config);
    ld2450_frame_t frame = frame_with_target(0, 0, 1000, 0);
    presence_fusion_result_t result = confirm_entry(&fusion, &frame, 0);
    require_true(result.ring_requested, "first visit rings");
    (void)confirm_leave(&fusion, 1000);
    result = confirm_entry(&fusion, &frame, 4000);
    require_true(result.event == PRESENCE_EVENT_ENTER_CONFIRMED && !result.ring_requested,
                 "visit during cooldown confirms without ring");
    ld2450_frame_t two_targets = frame_with_two_targets();
    result = presence_fusion_process(&fusion, &two_targets, 6000);
    require_true(result.state == PRESENCE_STATE_PRESENT &&
                     result.event == PRESENCE_EVENT_NONE &&
                     !result.ring_requested && !result.state_changed,
                 "two simultaneous in-zone targets maintain presence");
    ld2450_frame_t replacement = frame_with_target(2, 10, 1100, 0);
    result = presence_fusion_process(&fusion, &replacement, 7000);
    require_true(result.state == PRESENCE_STATE_PRESENT &&
                     result.event == PRESENCE_EVENT_NONE &&
                     !result.ring_requested && !result.state_changed,
                 "different valid slot maintains presence without re-entry");
    result = presence_fusion_process(&fusion, &replacement, 15000);
    require_true(result.state == PRESENCE_STATE_PRESENT &&
                     result.event == PRESENCE_EVENT_NONE &&
                     !result.ring_requested && !result.state_changed,
                 "suppressed visit stays quiet beyond cooldown expiry");
    (void)confirm_leave(&fusion, 16000);
    result = confirm_entry(&fusion, &replacement, 28000);
    require_true(result.event == PRESENCE_EVENT_ENTER_CONFIRMED && result.ring_requested,
                 "fresh visit rings after the new leave cooldown expires");

    presence_fusion_init(&fusion, &config);
    result = presence_fusion_process(&fusion, &frame, UINT32_MAX - 500U);
    require_true(result.state == PRESENCE_STATE_ENTERING, "wraparound starts entry timer");
    result = presence_fusion_process(&fusion, &frame, 499U);
    require_true(result.event == PRESENCE_EVENT_ENTER_CONFIRMED && result.ring_requested,
                 "entry timer confirms across uint32 wraparound");
}

static void test_validation_and_null_frames(void)
{
    presence_fusion_config_t config = presence_fusion_default_config();
    presence_fusion_t fusion;
    require_true(!presence_fusion_init(NULL, &config), "NULL fusion rejected");
    require_true(!presence_fusion_init(&fusion, NULL), "NULL config rejected");
    presence_fusion_config_t invalid = config;
    invalid.x_min_mm = invalid.x_max_mm + 1;
    require_true(!presence_fusion_init(&fusion, &invalid), "inverted x rejected");
    invalid = config; invalid.enter_confirm_ms = 0;
    require_true(!presence_fusion_init(&fusion, &invalid), "zero entry rejected");
    invalid = config; invalid.leave_confirm_ms = 0;
    require_true(!presence_fusion_init(&fusion, &invalid), "zero leave rejected");
    invalid = config; invalid.direction_deadband_cm_s = -1;
    require_true(!presence_fusion_init(&fusion, &invalid), "negative deadband rejected");
    require_true(presence_fusion_init(&fusion, &config), "valid config accepted");
    ld2450_frame_t occupied = frame_with_target(0, 0, 1000, 0);
    (void)presence_fusion_process(&fusion, &occupied, 0);
    (void)presence_fusion_process(&fusion, &occupied, 1000);
    require_true(fusion.state == PRESENCE_STATE_PRESENT, "fixture reaches PRESENT");
    presence_fusion_result_t result = presence_fusion_process(&fusion, NULL, 1100);
    require_true(result.state == PRESENCE_STATE_PRESENT && result.event == PRESENCE_EVENT_NONE &&
                     !result.state_changed && !result.ring_requested,
                 "NULL frame is a non-mutating safe result");
    require_true(fusion.state == PRESENCE_STATE_PRESENT, "NULL frame retains state");
    result = presence_fusion_process(NULL, NULL, 100);
    require_true(result.state == PRESENCE_STATE_CLEAR && result.event == PRESENCE_EVENT_NONE &&
                     result.representative_slot == SIZE_MAX,
                 "NULL fusion returns safe result");
}

static void test_filtering_representative_and_names(void)
{
    presence_fusion_config_t config = presence_fusion_default_config();
    presence_fusion_t fusion;
    require_true(presence_fusion_init(&fusion, &config), "valid config accepted");
    ld2450_frame_t frame = {0};
    frame.targets[0] = (ld2450_target_t){true, 0, 1200, 0, 0};
    frame.targets[1] = (ld2450_target_t){true, 0, 700, -20, 0};
    frame.targets[2] = (ld2450_target_t){true, 5000, 500, 0, 0};
    frame.target_count = 3;
    presence_fusion_result_t result = presence_fusion_process(&fusion, &frame, 0);
    require_true(result.in_zone_count == 2 && result.representative_slot == 1 &&
                     result.direction == PRESENCE_DIRECTION_APPROACHING,
                 "out-of-zone targets are filtered and nearest y is representative");
    frame.targets[2] = (ld2450_target_t){false, 0, 400, -30, 0};
    result = presence_fusion_process(&fusion, &frame, 1);
    require_true(result.in_zone_count == 2 && result.representative_slot == 1,
                 "invalid in-zone targets are excluded");
    require_true(strcmp(presence_state_name((presence_state_t)99), "UNKNOWN") == 0 &&
                     strcmp(presence_event_name((presence_event_t)99), "UNKNOWN") == 0 &&
                     strcmp(presence_direction_name((presence_direction_t)99), "UNKNOWN") == 0,
                 "invalid enum names return UNKNOWN");
}

int main(void)
{
    test_defaults_and_boundaries();
    test_direction_classification();
    test_entry_presence_and_representative_target();
    test_interruption_dropout_and_leave();
    test_cooldown_slot_and_wraparound();
    test_validation_and_null_frames();
    test_filtering_representative_and_names();
    puts("PASS: presence fusion state machine");
    return EXIT_SUCCESS;
}
