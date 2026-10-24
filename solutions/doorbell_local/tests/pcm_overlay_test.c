#include "pcm_overlay.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void require_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void test_mix_and_saturate(void)
{
    const int16_t prompt[] = {10000, -10000, -50};
    int16_t destination[] = {30000, -30000, 100};
    pcm_overlay_t overlay = {0};

    pcm_overlay_start(&overlay, prompt, 3);
    require_true(pcm_overlay_active(&overlay), "overlay starts active");
    require_true(pcm_overlay_mix(&overlay, destination, 3) == 3,
                 "all prompt samples are consumed");
    require_true(destination[0] == INT16_MAX, "positive mix saturates");
    require_true(destination[1] == INT16_MIN, "negative mix saturates");
    require_true(destination[2] == 50, "ordinary samples are added");
    require_true(!pcm_overlay_active(&overlay), "overlay stops at the end");
}

static void test_restart_and_stop(void)
{
    const int16_t prompt[] = {1, 2};
    int16_t first[] = {0};
    int16_t second[] = {0};
    pcm_overlay_t overlay = {0};

    pcm_overlay_start(&overlay, prompt, 2);
    require_true(pcm_overlay_mix(&overlay, first, 1) == 1 && first[0] == 1,
                 "first sample is mixed");
    pcm_overlay_start(&overlay, prompt, 2);
    require_true(pcm_overlay_mix(&overlay, second, 1) == 1 && second[0] == 1,
                 "restart begins again at sample zero");
    pcm_overlay_stop(&overlay);
    require_true(!pcm_overlay_active(&overlay), "explicit stop clears activity");
}

static void test_mono_prompt_is_copied_to_stereo(void)
{
    const int16_t prompt[] = {100, -200};
    int16_t destination[] = {1, 2, 3, 4};
    pcm_overlay_t overlay = {0};

    pcm_overlay_start(&overlay, prompt, 2);
    require_true(pcm_overlay_mix_mono(&overlay, destination, 4, 2) == 2,
                 "two mono prompt frames are consumed");
    require_true(destination[0] == 101 && destination[1] == 102,
                 "first prompt sample is mixed into both stereo channels");
    require_true(destination[2] == -197 && destination[3] == -196,
                 "second prompt sample is mixed into both stereo channels");
    require_true(!pcm_overlay_active(&overlay), "stereo mix completes overlay");
}

static void test_mono_prompt_can_fill_silent_output(void)
{
    const int16_t prompt[] = {120, -240};
    int16_t destination[] = {99, 99, 99, 99};
    pcm_overlay_t overlay = {0};

    pcm_overlay_start(&overlay, prompt, 2);
    require_true(pcm_overlay_copy_mono(&overlay, destination, 4, 2) == 2,
                 "two fallback prompt frames are copied");
    require_true(destination[0] == 120 && destination[1] == 120,
                 "first mono prompt sample fills both channels");
    require_true(destination[2] == -240 && destination[3] == -240,
                 "second mono prompt sample fills both channels");
    require_true(!pcm_overlay_active(&overlay),
                 "fallback copy completes overlay");
}

static void test_fallback_requires_remote_audio_silence(void)
{
    require_true(!pcm_overlay_fallback_due(false, 100000, 0, 40000),
                 "inactive prompt never needs fallback");
    require_true(!pcm_overlay_fallback_due(true, 100000, 70000, 40000),
                 "recent remote audio keeps normal mixing active");
    require_true(pcm_overlay_fallback_due(true, 110000, 70000, 40000),
                 "fallback starts at the silence threshold");
    require_true(!pcm_overlay_fallback_due(true, 60000, 70000, 40000),
                 "clock rollback does not trigger fallback");
}

int main(void)
{
    test_mix_and_saturate();
    test_restart_and_stop();
    test_mono_prompt_is_copied_to_stereo();
    test_mono_prompt_can_fill_silent_output();
    test_fallback_requires_remote_audio_silence();
    puts("PASS: PCM prompt overlay");
    return EXIT_SUCCESS;
}
