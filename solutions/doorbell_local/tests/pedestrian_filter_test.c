#include "pedestrian_filter.h"

#include <stdio.h>
#include <stdlib.h>

static void require_true(bool value, const char *message)
{
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static pedestrian_filter_t make_filter(void)
{
    pedestrian_filter_t filter;
    pedestrian_filter_config_t config = pedestrian_filter_default_config();
    require_true(pedestrian_filter_init(&filter, &config),
                 "default pedestrian filter initializes");
    return filter;
}

static bool update_box(pedestrian_filter_t *filter, float score,
                       int x1, int y1, int x2, int y2)
{
    pedestrian_detection_t detection = {
        .score = score,
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
    };
    return pedestrian_filter_update(filter, &detection, 320, 180);
}

static void test_full_frame_false_positive_is_rejected(void)
{
    pedestrian_filter_t filter = make_filter();
    for (int i = 0; i < 10; ++i) {
        require_true(!update_box(&filter, 0.86f, 0, 0, 319, 179),
                     "full-frame detection never becomes confirmed");
    }
}

static void test_model_default_score_detection_can_confirm(void)
{
    pedestrian_filter_t filter = make_filter();
    require_true(!update_box(&filter, 0.72f, 100, 20, 190, 170),
                 "first model-default candidate is not confirmed");
    require_true(!update_box(&filter, 0.72f, 101, 20, 191, 170),
                 "second model-default candidate is not confirmed");
    require_true(update_box(&filter, 0.72f, 102, 20, 192, 170),
                 "stable localized detection above 0.70 confirms");
}

static void test_stable_person_requires_three_frames(void)
{
    pedestrian_filter_t filter = make_filter();
    require_true(!update_box(&filter, 0.82f, 100, 20, 190, 170),
                 "first plausible frame is only a candidate");
    require_true(!update_box(&filter, 0.83f, 102, 21, 191, 169),
                 "second plausible frame is only a candidate");
    require_true(update_box(&filter, 0.84f, 101, 20, 192, 170),
                 "third overlapping frame confirms a person");
    require_true(update_box(&filter, 0.81f, 103, 22, 193, 171),
                 "confirmed person remains reportable while tracked");
}

static void test_unstable_boxes_do_not_confirm(void)
{
    pedestrian_filter_t filter = make_filter();
    require_true(!update_box(&filter, 0.85f, 10, 20, 70, 170),
                 "first candidate is not confirmed");
    require_true(!update_box(&filter, 0.85f, 240, 20, 300, 170),
                 "non-overlapping candidate restarts confirmation");
    require_true(!update_box(&filter, 0.85f, 10, 20, 70, 170),
                 "alternating candidates never confirm");
}

static void test_missing_frames_clear_confirmation(void)
{
    pedestrian_filter_t filter = make_filter();
    require_true(!update_box(&filter, 0.85f, 100, 20, 190, 170),
                 "confirmation frame one");
    require_true(!update_box(&filter, 0.85f, 100, 20, 190, 170),
                 "confirmation frame two");
    require_true(update_box(&filter, 0.85f, 100, 20, 190, 170),
                 "confirmation frame three");

    require_true(!pedestrian_filter_update(&filter, NULL, 320, 180),
                 "first miss is not reportable");
    require_true(!pedestrian_filter_update(&filter, NULL, 320, 180),
                 "second miss is not reportable");
    require_true(!pedestrian_filter_update(&filter, NULL, 320, 180),
                 "third miss clears confirmation");

    require_true(!update_box(&filter, 0.85f, 100, 20, 190, 170),
                 "person must reconfirm after being cleared");
}

static void test_invalid_configuration_and_box_are_safe(void)
{
    pedestrian_filter_t filter;
    pedestrian_filter_config_t config = pedestrian_filter_default_config();
    config.confirm_frames = 0;
    require_true(!pedestrian_filter_init(&filter, &config),
                 "zero confirmation frames are invalid");

    filter = make_filter();
    require_true(!update_box(&filter, 0.99f, 120, 40, 120, 100),
                 "zero-width box is rejected");
    require_true(!update_box(&filter, 0.99f, -1, 10, 50, 100),
                 "out-of-bounds box is rejected");
}

int main(void)
{
    test_full_frame_false_positive_is_rejected();
    test_model_default_score_detection_can_confirm();
    test_stable_person_requires_three_frames();
    test_unstable_boxes_do_not_confirm();
    test_missing_frames_clear_confirmation();
    test_invalid_configuration_and_box_are_safe();
    puts("PASS: pedestrian false-positive filter");
    return 0;
}
