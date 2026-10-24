#include "multimodal_fusion.h"

#include <string.h>

multimodal_fusion_config_t multimodal_fusion_default_config(void)
{
    multimodal_fusion_config_t config = {
        .vision_valid_ms = 2000U,
    };
    return config;
}

bool multimodal_fusion_init(multimodal_fusion_t *fusion,
                            const multimodal_fusion_config_t *config)
{
    if (fusion == NULL || config == NULL || config->vision_valid_ms == 0U) {
        return false;
    }
    memset(fusion, 0, sizeof(*fusion));
    fusion->config = *config;
    return true;
}

static bool vision_is_fresh(const multimodal_fusion_t *fusion, uint32_t now_ms)
{
    return fusion->has_vision_detection &&
           (uint32_t)(now_ms - fusion->last_vision_ms) <=
               fusion->config.vision_valid_ms;
}

static bool request_ring_if_fused(multimodal_fusion_t *fusion, uint32_t now_ms)
{
    if (!fusion->radar_present || !fusion->radar_ring_eligible ||
        fusion->ring_emitted_for_visit || !vision_is_fresh(fusion, now_ms)) {
        return false;
    }
    fusion->ring_emitted_for_visit = true;
    return true;
}

bool multimodal_fusion_on_radar(multimodal_fusion_t *fusion, bool present,
                                bool ring_eligible, uint32_t now_ms)
{
    if (fusion == NULL) {
        return false;
    }
    if (!present) {
        fusion->radar_present = false;
        fusion->radar_ring_eligible = false;
        fusion->ring_emitted_for_visit = false;
        fusion->has_vision_detection = false;
        fusion->last_vision_evidence = VISION_EVIDENCE_NONE;
        return false;
    }
    if (!fusion->radar_present) {
        fusion->radar_present = true;
        fusion->radar_ring_eligible = ring_eligible;
        fusion->ring_emitted_for_visit = false;
    }
    return request_ring_if_fused(fusion, now_ms);
}

bool multimodal_fusion_on_vision(multimodal_fusion_t *fusion, uint32_t now_ms)
{
    return multimodal_fusion_on_vision_evidence(
        fusion, VISION_EVIDENCE_PEDESTRIAN, now_ms);
}

bool multimodal_fusion_on_vision_evidence(multimodal_fusion_t *fusion,
                                          vision_evidence_t evidence,
                                          uint32_t now_ms)
{
    if (fusion == NULL ||
        (evidence != VISION_EVIDENCE_PEDESTRIAN &&
         evidence != VISION_EVIDENCE_FACE)) {
        return false;
    }
    fusion->has_vision_detection = true;
    fusion->last_vision_evidence = evidence;
    fusion->last_vision_ms = now_ms;
    return request_ring_if_fused(fusion, now_ms);
}
