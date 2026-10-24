#include "radar_ld2450.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const uint8_t frame_header[LD2450_HEADER_SIZE] = {
    0xAA, 0xFF, 0x03, 0x00,
};
static const uint8_t frame_tail[LD2450_TAIL_SIZE] = {0x55, 0xCC};

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int16_t decode_direction_magnitude(uint16_t raw)
{
    int16_t magnitude = (int16_t)(raw & 0x7FFFU);
    return (raw & 0x8000U) ? magnitude : (int16_t)-magnitude;
}

ld2450_parse_result_t ld2450_parse_frame(const uint8_t *data, size_t length,
                                         ld2450_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return LD2450_PARSE_BAD_ARGUMENT;
    }
    if (length != LD2450_FRAME_SIZE) {
        return LD2450_PARSE_BAD_LENGTH;
    }
    if (memcmp(data, frame_header, sizeof(frame_header)) != 0) {
        return LD2450_PARSE_BAD_HEADER;
    }
    if (memcmp(&data[LD2450_FRAME_SIZE - LD2450_TAIL_SIZE], frame_tail,
               sizeof(frame_tail)) != 0) {
        return LD2450_PARSE_BAD_TAIL;
    }

    memset(frame, 0, sizeof(*frame));
    for (size_t i = 0; i < LD2450_MAX_TARGETS; ++i) {
        const uint8_t *target_data =
            &data[LD2450_HEADER_SIZE + i * LD2450_TARGET_DATA_SIZE];
        bool valid = false;
        for (size_t j = 0; j < LD2450_TARGET_DATA_SIZE; ++j) {
            if (target_data[j] != 0) {
                valid = true;
                break;
            }
        }
        if (!valid) {
            continue;
        }

        ld2450_target_t *target = &frame->targets[i];
        target->valid = true;
        target->x_mm = decode_direction_magnitude(read_le16(&target_data[0]));
        target->y_mm = decode_direction_magnitude(read_le16(&target_data[2]));
        target->speed_cm_s =
            decode_direction_magnitude(read_le16(&target_data[4]));
        target->distance_resolution_mm = read_le16(&target_data[6]);
        frame->target_count++;
    }
    return LD2450_PARSE_OK;
}

void ld2450_stream_decoder_init(ld2450_stream_decoder_t *decoder)
{
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(*decoder));
    }
}

static void accept_sync_byte(ld2450_stream_decoder_t *decoder, uint8_t byte)
{
    if (byte == frame_header[decoder->length]) {
        decoder->buffer[decoder->length++] = byte;
    } else if (byte == frame_header[0]) {
        decoder->buffer[0] = byte;
        decoder->length = 1;
    } else {
        decoder->length = 0;
    }
}

size_t ld2450_stream_feed(ld2450_stream_decoder_t *decoder,
                          const uint8_t *data, size_t length,
                          ld2450_frame_callback_t callback, void *ctx)
{
    if (decoder == NULL || (data == NULL && length != 0)) {
        return 0;
    }

    size_t emitted = 0;
    for (size_t i = 0; i < length; ++i) {
        if (decoder->length < LD2450_HEADER_SIZE) {
            accept_sync_byte(decoder, data[i]);
            continue;
        }

        decoder->buffer[decoder->length++] = data[i];
        if (decoder->length != LD2450_FRAME_SIZE) {
            continue;
        }

        ld2450_frame_t frame;
        if (ld2450_parse_frame(decoder->buffer, decoder->length, &frame) ==
            LD2450_PARSE_OK) {
            emitted++;
            if (callback != NULL) {
                callback(decoder->buffer, &frame, ctx);
            }
        }
        decoder->length = 0;
    }
    return emitted;
}

bool ld2450_should_capture_sample(ld2450_sample_tracker_t *tracker,
                                  size_t target_count)
{
    if (tracker == NULL || target_count > LD2450_MAX_TARGETS ||
        tracker->captured[target_count]) {
        return false;
    }
    tracker->captured[target_count] = true;
    return true;
}

#ifdef ESP_PLATFORM

#define LD2450_UART_PORT UART_NUM_1
#define LD2450_UART_RX_BUFFER_SIZE 2048
#define LD2450_READ_CHUNK_SIZE 256
#define LD2450_TASK_STACK_SIZE 4096
#define LD2450_TASK_PRIORITY 5
#define LD2450_OFFLINE_TIMEOUT_MS 2000

static const char *TAG = "LD2450";
static bool uart_initialized;
static ld2450_sample_tracker_t raw_sample_tracker;
static ld2450_frame_callback_t app_frame_callback;
static void *app_frame_callback_ctx;

static void handle_frame(const uint8_t raw_frame[LD2450_FRAME_SIZE],
                         const ld2450_frame_t *frame, void *ctx)
{
    (void)ctx;
    if (ld2450_should_capture_sample(&raw_sample_tracker,
                                     frame->target_count)) {
        ESP_LOGI(TAG, "Raw frame sample for %u target(s)",
                 (unsigned)frame->target_count);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, raw_frame, LD2450_FRAME_SIZE,
                                 ESP_LOG_INFO);
    }

    if (app_frame_callback != NULL) {
        app_frame_callback(raw_frame, frame, app_frame_callback_ctx);
    }
}

static void radar_uart_task(void *arg)
{
    (void)arg;
    uint8_t data[LD2450_READ_CHUNK_SIZE];
    ld2450_stream_decoder_t decoder;
    ld2450_stream_decoder_init(&decoder);
    TickType_t last_valid_frame_tick = xTaskGetTickCount();
    bool offline_logged = false;

    while (true) {
        int length = uart_read_bytes(LD2450_UART_PORT, data, sizeof(data),
                                     pdMS_TO_TICKS(100));
        if (length > 0) {
            size_t emitted = ld2450_stream_feed(&decoder, data, (size_t)length,
                                                handle_frame, NULL);
            if (emitted > 0U) {
                last_valid_frame_tick = xTaskGetTickCount();
                if (offline_logged) {
                    ESP_LOGI(TAG, "Valid frame stream restored");
                    offline_logged = false;
                }
            }
        }

        TickType_t now_tick = xTaskGetTickCount();
        if (!offline_logged &&
            (TickType_t)(now_tick - last_valid_frame_tick) >=
                pdMS_TO_TICKS(LD2450_OFFLINE_TIMEOUT_MS)) {
            ESP_LOGW(TAG, "No valid LD2450 frame for 2000 ms");
            offline_logged = true;
        }
    }
}

esp_err_t radar_ld2450_init(ld2450_frame_callback_t callback, void *ctx)
{
    if (uart_initialized) {
        return ESP_OK;
    }

    const uart_config_t uart_config = {
        .baud_rate = LD2450_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_param_config(LD2450_UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = uart_set_pin(LD2450_UART_PORT, LD2450_UART_TX_GPIO,
                       LD2450_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = uart_driver_install(LD2450_UART_PORT, LD2450_UART_RX_BUFFER_SIZE,
                              0, 0, NULL, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    app_frame_callback = callback;
    app_frame_callback_ctx = ctx;
    if (xTaskCreate(radar_uart_task, "ld2450_uart", LD2450_TASK_STACK_SIZE,
                    NULL, LD2450_TASK_PRIORITY, NULL) != pdPASS) {
        app_frame_callback = NULL;
        app_frame_callback_ctx = NULL;
        uart_driver_delete(LD2450_UART_PORT);
        return ESP_ERR_NO_MEM;
    }

    uart_initialized = true;
    ESP_LOGI(TAG, "UART%d ready at %d baud (RX GPIO%d, TX GPIO%d)",
             (int)LD2450_UART_PORT, LD2450_UART_BAUD_RATE,
             LD2450_UART_RX_GPIO, LD2450_UART_TX_GPIO);
    return ESP_OK;
}

#endif
