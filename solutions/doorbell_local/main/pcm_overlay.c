#include "pcm_overlay.h"

#include <limits.h>

void pcm_overlay_start(pcm_overlay_t *overlay, const int16_t *samples,
                       size_t sample_count)
{
    if (overlay == NULL) {
        return;
    }
    overlay->samples = samples;
    overlay->sample_count = sample_count;
    overlay->sample_position = 0;
    overlay->active = samples != NULL && sample_count > 0;
}

void pcm_overlay_stop(pcm_overlay_t *overlay)
{
    if (overlay == NULL) {
        return;
    }
    overlay->samples = NULL;
    overlay->sample_count = 0;
    overlay->sample_position = 0;
    overlay->active = false;
}

bool pcm_overlay_active(const pcm_overlay_t *overlay)
{
    return overlay != NULL && overlay->active;
}

size_t pcm_overlay_mix(pcm_overlay_t *overlay, int16_t *destination,
                       size_t destination_sample_count)
{
    if (!pcm_overlay_active(overlay) || destination == NULL) {
        return 0;
    }

    size_t remaining = overlay->sample_count - overlay->sample_position;
    size_t mix_count = destination_sample_count < remaining
                           ? destination_sample_count
                           : remaining;
    for (size_t i = 0; i < mix_count; ++i) {
        int32_t mixed = (int32_t)destination[i] +
                        (int32_t)overlay->samples[overlay->sample_position + i];
        if (mixed > INT16_MAX) {
            mixed = INT16_MAX;
        } else if (mixed < INT16_MIN) {
            mixed = INT16_MIN;
        }
        destination[i] = (int16_t)mixed;
    }
    overlay->sample_position += mix_count;
    if (overlay->sample_position >= overlay->sample_count) {
        overlay->active = false;
    }
    return mix_count;
}

size_t pcm_overlay_mix_mono(pcm_overlay_t *overlay, int16_t *destination,
                            size_t destination_sample_count,
                            unsigned destination_channels)
{
    if (destination_channels == 1) {
        return pcm_overlay_mix(overlay, destination, destination_sample_count);
    }
    if (!pcm_overlay_active(overlay) || destination == NULL ||
        destination_channels == 0) {
        return 0;
    }

    size_t destination_frames = destination_sample_count / destination_channels;
    size_t remaining = overlay->sample_count - overlay->sample_position;
    size_t mix_frames = destination_frames < remaining
                            ? destination_frames
                            : remaining;
    for (size_t frame = 0; frame < mix_frames; ++frame) {
        int32_t prompt = overlay->samples[overlay->sample_position + frame];
        for (unsigned channel = 0; channel < destination_channels; ++channel) {
            size_t index = frame * destination_channels + channel;
            int32_t mixed = (int32_t)destination[index] + prompt;
            if (mixed > INT16_MAX) {
                mixed = INT16_MAX;
            } else if (mixed < INT16_MIN) {
                mixed = INT16_MIN;
            }
            destination[index] = (int16_t)mixed;
        }
    }
    overlay->sample_position += mix_frames;
    if (overlay->sample_position >= overlay->sample_count) {
        overlay->active = false;
    }
    return mix_frames;
}

size_t pcm_overlay_copy_mono(pcm_overlay_t *overlay, int16_t *destination,
                             size_t destination_sample_count,
                             unsigned destination_channels)
{
    if (!pcm_overlay_active(overlay) || destination == NULL ||
        destination_channels == 0) {
        return 0;
    }

    size_t destination_frames = destination_sample_count / destination_channels;
    size_t remaining = overlay->sample_count - overlay->sample_position;
    size_t copy_frames = destination_frames < remaining
                             ? destination_frames
                             : remaining;
    for (size_t frame = 0; frame < copy_frames; ++frame) {
        int16_t prompt = overlay->samples[overlay->sample_position + frame];
        for (unsigned channel = 0; channel < destination_channels; ++channel) {
            destination[frame * destination_channels + channel] = prompt;
        }
    }
    overlay->sample_position += copy_frames;
    if (overlay->sample_position >= overlay->sample_count) {
        overlay->active = false;
    }
    return copy_frames;
}

bool pcm_overlay_fallback_due(bool active, int64_t now_us,
                              int64_t last_remote_write_us,
                              int64_t silence_threshold_us)
{
    return active && silence_threshold_us >= 0 &&
           now_us >= last_remote_write_us &&
           now_us - last_remote_write_us >= silence_threshold_us;
}
