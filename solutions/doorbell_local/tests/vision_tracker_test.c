#include "vision_tracker.h"

#include <stdio.h>
#include <stdlib.h>

static void require_true(bool value, const char *message)
{
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static vision_detection_t box(vision_kind_t kind, float score,
                              int x1, int y1, int x2, int y2)
{
    vision_detection_t detection = {
        .kind = kind,
        .score = score,
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
    };
    return detection;
}

static vision_tracker_t make_tracker(void)
{
    vision_tracker_t tracker;
    vision_tracker_config_t config = vision_tracker_default_config();
    require_true(vision_tracker_init(&tracker, &config),
                 "default tracker initializes");
    return tracker;
}

static vision_update_t update_one(vision_tracker_t *tracker,
                                  vision_detection_t *detection,
                                  uint32_t now_ms)
{
    return vision_tracker_update(tracker, detection, detection ? 1U : 0U,
                                 320U, 180U, now_ms);
}

static void test_three_of_four_confirms_face(void)
{
    vision_tracker_t tracker = make_tracker();
    vision_detection_t face = box(VISION_KIND_FACE, 0.86f, 110, 20, 180, 100);
    vision_update_t result = update_one(&tracker, &face, 0U);
    require_true(!result.confirmed_present, "first face is a candidate");
    result = update_one(&tracker, NULL, 500U);
    require_true(!result.confirmed_present, "one miss remains unconfirmed");
    face.x1 += 2;
    face.x2 += 2;
    result = update_one(&tracker, &face, 1000U);
    require_true(!result.confirmed_present, "second hit remains a candidate");
    face.x1 += 1;
    face.x2 += 1;
    result = update_one(&tracker, &face, 1500U);
    require_true(result.confirmed_present, "three hits in four confirm face");
    require_true(result.targets[0].confirmed, "reported face is confirmed");
}

static void test_three_misses_clear_confirmation(void)
{
    vision_tracker_t tracker = make_tracker();
    vision_detection_t person =
        box(VISION_KIND_PEDESTRIAN, 0.82f, 90, 10, 210, 175);
    update_one(&tracker, &person, 0U);
    update_one(&tracker, &person, 500U);
    require_true(update_one(&tracker, &person, 1000U).confirmed_present,
                 "pedestrian confirms after three hits");
    require_true(update_one(&tracker, NULL, 1500U).confirmed_present,
                 "first miss holds confirmation");
    require_true(update_one(&tracker, NULL, 2000U).confirmed_present,
                 "second miss holds confirmation");
    require_true(!update_one(&tracker, NULL, 2500U).confirmed_present,
                 "third miss clears confirmation");
}

static void test_invalid_and_motion_results_never_confirm(void)
{
    vision_tracker_t tracker = make_tracker();
    vision_detection_t full =
        box(VISION_KIND_PEDESTRIAN, 0.99f, 0, 0, 319, 179);
    for (int i = 0; i < 4; ++i) {
        require_true(!update_one(&tracker, &full, (uint32_t)i * 500U)
                          .confirmed_present,
                     "full-frame result is rejected");
    }
    vision_detection_t motion =
        box(VISION_KIND_MOTION, 0.99f, 80, 20, 220, 170);
    for (int i = 0; i < 4; ++i) {
        require_true(!update_one(&tracker, &motion, 3000U + (uint32_t)i * 500U)
                          .confirmed_present,
                     "motion cannot confirm a person");
    }
    vision_detection_t low_face =
        box(VISION_KIND_FACE, 0.74f, 100, 20, 180, 110);
    require_true(update_one(&tracker, &low_face, 6000U).target_count == 0U,
                 "face below threshold is rejected");
}

static void test_face_inside_pedestrian_counts_once(void)
{
    vision_tracker_t tracker = make_tracker();
    vision_detection_t detections[2] = {
        box(VISION_KIND_PEDESTRIAN, 0.84f, 70, 5, 245, 178),
        box(VISION_KIND_FACE, 0.91f, 125, 20, 185, 90),
    };
    vision_update_t result = {0};
    for (int i = 0; i < 3; ++i) {
        result = vision_tracker_update(&tracker, detections, 2U, 320U, 180U,
                                       (uint32_t)i * 500U);
    }
    require_true(result.target_count == 2U, "both useful boxes are reported");
    require_true(result.confirmed_present, "combined vision is confirmed");
    require_true(result.confirmed_person_count == 1U,
                 "face inside pedestrian counts as one person");
}

static void test_configuration_validation(void)
{
    vision_tracker_t tracker;
    vision_tracker_config_t config = vision_tracker_default_config();
    config.confirm_hits = 0U;
    require_true(!vision_tracker_init(&tracker, &config),
                 "zero required hits is invalid");
    config = vision_tracker_default_config();
    config.confirm_hits = 5U;
    require_true(!vision_tracker_init(&tracker, &config),
                 "required hits cannot exceed window");
}

int main(void)
{
    test_three_of_four_confirms_face();
    test_three_misses_clear_confirmation();
    test_invalid_and_motion_results_never_confirm();
    test_face_inside_pedestrian_counts_once();
    test_configuration_validation();
    puts("PASS: vision tracker");
    return 0;
}
