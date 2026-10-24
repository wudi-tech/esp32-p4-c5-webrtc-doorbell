#include "door_lock.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define DOOR_LOCK_PWM_PERIOD_US       (1000000U / DOOR_LOCK_PWM_FREQUENCY_HZ)
#define DOOR_LOCK_DUTY_RESOLUTION     LEDC_TIMER_14_BIT
#define DOOR_LOCK_DUTY_COUNTS         (1U << 14)
#define DOOR_LOCK_LEDC_MODE           LEDC_LOW_SPEED_MODE
#define DOOR_LOCK_LEDC_TIMER          LEDC_TIMER_3
#define DOOR_LOCK_LEDC_CHANNEL        LEDC_CHANNEL_7
#define DOOR_LOCK_TASK_STACK_SIZE     3072
#define DOOR_LOCK_TASK_PRIORITY       5

static const char *TAG = "DOOR_LOCK";

typedef enum {
    DOOR_LOCK_COMMAND_OPEN = 0,
} door_lock_command_t;

static QueueHandle_t command_queue;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static door_lock_state_t state = DOOR_LOCK_STATE_LOCKED;
static bool initialized;
static bool command_pending;
static door_lock_state_callback_t state_callback;
static void *state_callback_ctx;

static uint32_t pulse_us_to_duty(uint32_t pulse_us)
{
    return (uint32_t)(((uint64_t)pulse_us * DOOR_LOCK_DUTY_COUNTS +
                       DOOR_LOCK_PWM_PERIOD_US / 2U) /
                      DOOR_LOCK_PWM_PERIOD_US);
}

static esp_err_t set_servo_pulse(uint32_t pulse_us)
{
    uint32_t duty = pulse_us_to_duty(pulse_us);
    esp_err_t ret = ledc_set_duty(DOOR_LOCK_LEDC_MODE,
                                  DOOR_LOCK_LEDC_CHANNEL, duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(DOOR_LOCK_LEDC_MODE,
                               DOOR_LOCK_LEDC_CHANNEL);
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Servo pulse %" PRIu32 " us (duty %" PRIu32 ")",
                 pulse_us, duty);
    } else {
        ESP_LOGE(TAG, "Failed to set servo pulse: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void notify_state(door_lock_state_t new_state)
{
    door_lock_state_callback_t callback;
    void *callback_ctx;

    portENTER_CRITICAL(&state_lock);
    state = new_state;
    callback = state_callback;
    callback_ctx = state_callback_ctx;
    portEXIT_CRITICAL(&state_lock);

    ESP_LOGI(TAG, "State -> %s", door_lock_state_name(new_state));
    if (callback) {
        callback(new_state, callback_ctx);
    }
}

static void fail_operation(void)
{
    notify_state(door_lock_state_transition(door_lock_get_state(),
                                            DOOR_LOCK_EVENT_FAILURE));
}

static void door_lock_task(void *arg)
{
    (void)arg;
    door_lock_command_t command;

    while (true) {
        if (xQueueReceive(command_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        portENTER_CRITICAL(&state_lock);
        command_pending = false;
        portEXIT_CRITICAL(&state_lock);

        if (command != DOOR_LOCK_COMMAND_OPEN) {
            fail_operation();
            continue;
        }

        notify_state(door_lock_state_transition(door_lock_get_state(),
                                                DOOR_LOCK_EVENT_OPEN_REQUEST));
        if (set_servo_pulse(DOOR_LOCK_UNLOCKED_PULSE_US) != ESP_OK) {
            fail_operation();
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(DOOR_LOCK_MOTION_TIME_MS));

        notify_state(door_lock_state_transition(door_lock_get_state(),
                                                DOOR_LOCK_EVENT_MOTION_COMPLETE));
        vTaskDelay(pdMS_TO_TICKS(DOOR_LOCK_HOLD_TIME_MS));

        notify_state(door_lock_state_transition(door_lock_get_state(),
                                                DOOR_LOCK_EVENT_HOLD_EXPIRED));
        if (set_servo_pulse(DOOR_LOCK_LOCKED_PULSE_US) != ESP_OK) {
            fail_operation();
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(DOOR_LOCK_MOTION_TIME_MS));

        notify_state(door_lock_state_transition(door_lock_get_state(),
                                                DOOR_LOCK_EVENT_MOTION_COMPLETE));
    }
}

esp_err_t door_lock_init(void)
{
    portENTER_CRITICAL(&state_lock);
    bool already_initialized = initialized;
    portEXIT_CRITICAL(&state_lock);
    if (already_initialized) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = DOOR_LOCK_LEDC_MODE,
        .duty_resolution = DOOR_LOCK_DUTY_RESOLUTION,
        .timer_num = DOOR_LOCK_LEDC_TIMER,
        .freq_hz = DOOR_LOCK_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PWM timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = DOOR_LOCK_SERVO_GPIO,
        .speed_mode = DOOR_LOCK_LEDC_MODE,
        .channel = DOOR_LOCK_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = DOOR_LOCK_LEDC_TIMER,
        .duty = pulse_us_to_duty(DOOR_LOCK_LOCKED_PULSE_US),
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ret = ledc_channel_config(&channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d PWM: %s",
                 DOOR_LOCK_SERVO_GPIO, esp_err_to_name(ret));
        return ret;
    }

    command_queue = xQueueCreate(1, sizeof(door_lock_command_t));
    if (command_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(door_lock_task, "door_lock", DOOR_LOCK_TASK_STACK_SIZE,
                    NULL, DOOR_LOCK_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(command_queue);
        command_queue = NULL;
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_ERR_NO_MEM;
    }

    portENTER_CRITICAL(&state_lock);
    state = DOOR_LOCK_STATE_LOCKED;
    command_pending = false;
    initialized = true;
    portEXIT_CRITICAL(&state_lock);

    ESP_LOGI(TAG, "Initialized GPIO%d at %d Hz; boot state LOCKED (%d us)",
             DOOR_LOCK_SERVO_GPIO, DOOR_LOCK_PWM_FREQUENCY_HZ,
             DOOR_LOCK_LOCKED_PULSE_US);
    return ESP_OK;
}

void door_lock_set_state_callback(door_lock_state_callback_t callback, void *ctx)
{
    portENTER_CRITICAL(&state_lock);
    state_callback = callback;
    state_callback_ctx = ctx;
    portEXIT_CRITICAL(&state_lock);
}

esp_err_t door_lock_request_open(void)
{
    door_lock_command_t command = DOOR_LOCK_COMMAND_OPEN;

    portENTER_CRITICAL(&state_lock);
    if (!initialized || command_queue == NULL) {
        portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (state == DOOR_LOCK_STATE_FAULT) {
        portEXIT_CRITICAL(&state_lock);
        return ESP_FAIL;
    }
    if (state != DOOR_LOCK_STATE_LOCKED || command_pending) {
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGI(TAG, "Open request is already in progress (%s)",
                 door_lock_state_name(door_lock_get_state()));
        return ESP_OK;
    }
    command_pending = true;
    portEXIT_CRITICAL(&state_lock);

    if (xQueueSend(command_queue, &command, 0) != pdTRUE) {
        portENTER_CRITICAL(&state_lock);
        command_pending = false;
        portEXIT_CRITICAL(&state_lock);
        fail_operation();
        return ESP_FAIL;
    }
    return ESP_OK;
}

door_lock_state_t door_lock_get_state(void)
{
    portENTER_CRITICAL(&state_lock);
    door_lock_state_t current_state = state;
    portEXIT_CRITICAL(&state_lock);
    return current_state;
}
