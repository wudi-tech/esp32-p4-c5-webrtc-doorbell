#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const int16_t *samples;
    size_t sample_count;
    size_t sample_position;
    bool active;
} pcm_overlay_t;

void pcm_overlay_start(pcm_overlay_t *overlay, const int16_t *samples,
                       size_t sample_count);
void pcm_overlay_stop(pcm_overlay_t *overlay);
bool pcm_overlay_active(const pcm_overlay_t *overlay);
size_t pcm_overlay_mix(pcm_overlay_t *overlay, int16_t *destination,
                       size_t destination_sample_count);
size_t pcm_overlay_mix_mono(pcm_overlay_t *overlay, int16_t *destination,
                            size_t destination_sample_count,
                            unsigned destination_channels);
size_t pcm_overlay_copy_mono(pcm_overlay_t *overlay, int16_t *destination,
                             size_t destination_sample_count,
                             unsigned destination_channels);
bool pcm_overlay_fallback_due(bool active, int64_t now_us,
                              int64_t last_remote_write_us,
                              int64_t silence_threshold_us);

#ifdef __cplusplus
}
#endif
