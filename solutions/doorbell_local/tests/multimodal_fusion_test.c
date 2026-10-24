#include "multimodal_fusion.h"

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

static void test_radar_then_vision_rings_once(void)
{
    multimodal_fusion_t fusion;
    multimodal_fusion_config_t config = multimodal_fusion_default_config();
    require_true(config.vision_valid_ms == 2000U,
                 "default vision freshness window is two seconds");
    require_true(multimodal_fusion_init(&fusion, &config), "valid config accepted");

    require_true(!multimodal_fusion_on_radar(&fusion, true, true, 1000U),
                 "radar presence alone does not ring");
    require_true(multimodal_fusion_on_vision(&fusion, 2500U),
                 "fresh vision detection while radar is present rings");
    require_true(!multimodal_fusion_on_vision(&fusion, 2600U),
                 "same radar visit rings only once");
}

static void test_only_semantic_vision_can_ring(void)
{
    multimodal_fusion_t fusion;
    multimodal_fusion_config_t config = multimodal_fusion_default_config();
    multimodal_fusion_init(&fusion, &config);

    require_true(!multimodal_fusion_on_radar(&fusion, true, true, 1000U),
                 "radar alone does not ring");
    require_true(!multimodal_fusion_on_vision_evidence(
                     &fusion, VISION_EVIDENCE_MOTION, 1100U),
                 "camera motion cannot satisfy semantic vision");
    require_true(multimodal_fusion_on_vision_evidence(
                     &fusion, VISION_EVIDENCE_FACE, 1200U),
                 "confirmed face can satisfy semantic vision");

    (void)multimodal_fusion_on_radar(&fusion, false, false, 2000U);
    require_true(!multimodal_fusion_on_vision_evidence(
                     &fusion, VISION_EVIDENCE_PEDESTRIAN, 2100U),
                 "pedestrian alone does not ring without radar");
    require_true(multimodal_fusion_on_radar(&fusion, true, true, 2200U),
                 "fresh pedestrian evidence rings when radar arrives");
}

static void test_vision_then_radar_and_freshness(void)
{
    multimodal_fusion_t fusion;
    multimodal_fusion_config_t config = multimodal_fusion_default_config();
    multimodal_fusion_init(&fusion, &config);

    require_true(!multimodal_fusion_on_vision(&fusion, 1000U),
                 "vision alone does not ring");
    require_true(multimodal_fusion_on_radar(&fusion, true, true, 2999U),
                 "recent vision can be fused when radar confirms later");

    require_true(!multimodal_fusion_on_radar(&fusion, false, false, 4000U),
                 "confirmed leave resets the visit");
    require_true(!multimodal_fusion_on_vision(&fusion, 5000U),
                 "new vision alone remains insufficient");
    require_true(!multimodal_fusion_on_radar(&fusion, true, true, 7001U),
                 "stale vision is not fused");
    require_true(multimodal_fusion_on_vision(&fusion, 7100U),
                 "a fresh vision result completes the new visit");
}

static void test_cooldown_and_wraparound(void)
{
    multimodal_fusion_t fusion;
    multimodal_fusion_config_t config = multimodal_fusion_default_config();
    multimodal_fusion_init(&fusion, &config);

    (void)multimodal_fusion_on_vision(&fusion, 100U);
    require_true(!multimodal_fusion_on_radar(&fusion, true, false, 200U),
                 "a radar visit suppressed by cooldown cannot ring");
    require_true(!multimodal_fusion_on_vision(&fusion, 300U),
                 "later vision does not bypass radar cooldown eligibility");

    (void)multimodal_fusion_on_radar(&fusion, false, false, UINT32_MAX - 100U);
    (void)multimodal_fusion_on_vision(&fusion, UINT32_MAX - 50U);
    require_true(multimodal_fusion_on_radar(&fusion, true, true, 25U),
                 "freshness calculation works across uint32 wraparound");
}

static void test_validation(void)
{
    multimodal_fusion_t fusion;
    multimodal_fusion_config_t config = multimodal_fusion_default_config();
    require_true(!multimodal_fusion_init(NULL, &config), "NULL state rejected");
    require_true(!multimodal_fusion_init(&fusion, NULL), "NULL config rejected");
    config.vision_valid_ms = 0;
    require_true(!multimodal_fusion_init(&fusion, &config), "zero window rejected");
    require_true(!multimodal_fusion_on_radar(NULL, true, true, 0),
                 "NULL radar update is safe");
    require_true(!multimodal_fusion_on_vision(NULL, 0),
                 "NULL vision update is safe");
}

int main(void)
{
    test_radar_then_vision_rings_once();
    test_only_semantic_vision_can_ring();
    test_vision_then_radar_and_freshness();
    test_cooldown_and_wraparound();
    test_validation();
    puts("PASS: radar and vision multimodal fusion");
    return EXIT_SUCCESS;
}
