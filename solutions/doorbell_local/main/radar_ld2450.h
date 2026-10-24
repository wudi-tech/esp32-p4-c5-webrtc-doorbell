#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LD2450_MAX_TARGETS 3
#define LD2450_HEADER_SIZE 4
#define LD2450_TARGET_DATA_SIZE 8
#define LD2450_TAIL_SIZE 2
#define LD2450_FRAME_SIZE \
    (LD2450_HEADER_SIZE + LD2450_MAX_TARGETS * LD2450_TARGET_DATA_SIZE + \
     LD2450_TAIL_SIZE)

#ifdef ESP_PLATFORM
#include "esp_err.h"

#define LD2450_UART_BAUD_RATE 256000
#define LD2450_UART_RX_GPIO 4
#define LD2450_UART_TX_GPIO 5
#endif

typedef struct {
    bool valid;
    int16_t x_mm;
    int16_t y_mm;
    int16_t speed_cm_s;
    uint16_t distance_resolution_mm;
} ld2450_target_t;

typedef struct {
    ld2450_target_t targets[LD2450_MAX_TARGETS];
    size_t target_count;
} ld2450_frame_t;

typedef enum {
    LD2450_PARSE_OK = 0,
    LD2450_PARSE_BAD_ARGUMENT,
    LD2450_PARSE_BAD_LENGTH,
    LD2450_PARSE_BAD_HEADER,
    LD2450_PARSE_BAD_TAIL,
} ld2450_parse_result_t;

typedef struct {
    uint8_t buffer[LD2450_FRAME_SIZE];
    size_t length;
} ld2450_stream_decoder_t;

typedef struct {
    bool captured[LD2450_MAX_TARGETS + 1];
} ld2450_sample_tracker_t;

typedef void (*ld2450_frame_callback_t)(
    const uint8_t raw_frame[LD2450_FRAME_SIZE],
    const ld2450_frame_t *frame, void *ctx);

ld2450_parse_result_t ld2450_parse_frame(const uint8_t *data, size_t length,
                                         ld2450_frame_t *frame);
void ld2450_stream_decoder_init(ld2450_stream_decoder_t *decoder);
size_t ld2450_stream_feed(ld2450_stream_decoder_t *decoder,
                          const uint8_t *data, size_t length,
                          ld2450_frame_callback_t callback, void *ctx);
bool ld2450_should_capture_sample(ld2450_sample_tracker_t *tracker,
                                  size_t target_count);

#ifdef ESP_PLATFORM
esp_err_t radar_ld2450_init(ld2450_frame_callback_t callback, void *ctx);
#endif

#ifdef __cplusplus
}
#endif
