#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t vision_valid_ms;
} multimodal_fusion_config_t;

typedef enum {
    VISION_EVIDENCE_NONE = 0,
    VISION_EVIDENCE_MOTION,
    VISION_EVIDENCE_PEDESTRIAN,
    VISION_EVIDENCE_FACE,
} vision_evidence_t;

typedef struct {
    multimodal_fusion_config_t config;
    bool radar_present;
    bool radar_ring_eligible;
    bool ring_emitted_for_visit;
    bool has_vision_detection;
    vision_evidence_t last_vision_evidence;
    uint32_t last_vision_ms;
} multimodal_fusion_t;

multimodal_fusion_config_t multimodal_fusion_default_config(void);
bool multimodal_fusion_init(multimodal_fusion_t *fusion,
                            const multimodal_fusion_config_t *config);
bool multimodal_fusion_on_radar(multimodal_fusion_t *fusion, bool present,
                                bool ring_eligible, uint32_t now_ms);
bool multimodal_fusion_on_vision(multimodal_fusion_t *fusion, uint32_t now_ms);
bool multimodal_fusion_on_vision_evidence(multimodal_fusion_t *fusion,
                                          vision_evidence_t evidence,
                                          uint32_t now_ms);

#ifdef __cplusplus
}
#endif
