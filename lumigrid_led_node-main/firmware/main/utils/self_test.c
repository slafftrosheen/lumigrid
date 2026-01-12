#include "self_test.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "aled_state.h"
#include "aled_rmt.h"
#include "esp_log.h"
#include "esp_err.h"
#include "effects.h"
#include "pca9685_driver.h"

#define SELF_TEST_STACK      (4096)
#define SELF_TEST_PRIORITY   (5)
#define SELF_TEST_SCRATCH_LEDS (16)
#define SELF_TEST_COLOR_DELAY_MS (150U)
#define SELF_TEST_PWM_DELAY_MS   (120U)

static const char *TAG = "self_test";

static TaskHandle_t s_self_test_task = NULL;
static volatile bool s_cancel_requested = false;

static bool self_test_delay(uint32_t delay_ms){
    TickType_t remaining = pdMS_TO_TICKS(delay_ms);
    const TickType_t slice = pdMS_TO_TICKS(25);

    while (remaining > 0){
        TickType_t wait = remaining < slice ? remaining : slice;
        if (ulTaskNotifyTake(pdTRUE, wait) > 0){
            return false;
        }
        if (s_cancel_requested){
            return false;
        }
        remaining -= wait;
    }
    return true;
}

static void self_test_task(void *arg){
    (void)arg;

    s_self_test_task = xTaskGetCurrentTaskHandle();
    ESP_LOGI(TAG, "Self-test sequence started");
    ulTaskNotifyTake(pdTRUE, 0); // clear pending notifications

    px_rgba_t scratch[SELF_TEST_SCRATCH_LEDS];
    const px_rgba_t colors[] = {
        {255,   0,   0,   0},
        {0,   255,   0,   0},
        {0,     0, 255,   0},
        {0,     0,   0, 255}
    };

    bool led_hw_ready = true;
    int led_channels = aled_count();
    for (int ch = 0; ch < led_channels && !s_cancel_requested && led_hw_ready; ++ch){
        aled_channel_t *C = &aled_ch[ch];
        int pixels = C->n_pixels;
        if (pixels <= 0){
            continue;
        }

        int burst_len = pixels < SELF_TEST_SCRATCH_LEDS ? pixels : SELF_TEST_SCRATCH_LEDS;

        for (size_t c = 0; c < sizeof(colors)/sizeof(colors[0]) && !s_cancel_requested; ++c){
            for (int i = 0; i < burst_len; ++i){
                scratch[i] = colors[c];
            }
            esp_err_t err = aled_rmt_write(C->ch, scratch, burst_len, C->type, C->order);
            if (err == ESP_ERR_INVALID_ARG){
                ESP_LOGW(TAG, "aled_rmt_write ch%d unavailable: %s", C->ch, esp_err_to_name(err));
                led_hw_ready = false;
                break;
            } else if (err != ESP_OK){
                ESP_LOGW(TAG, "aled_rmt_write ch%d failed: %s", C->ch, esp_err_to_name(err));
            }
            if (!self_test_delay(SELF_TEST_COLOR_DELAY_MS)){
                break;
            }
        }

        if (s_cancel_requested){
            break;
        }

        if (!led_hw_ready){
            break;
        }

        memset(scratch, 0, sizeof(scratch));
        esp_err_t err = aled_rmt_write(C->ch, scratch, burst_len, C->type, C->order);
        if (err == ESP_ERR_INVALID_ARG){
            ESP_LOGW(TAG, "aled_rmt_write off ch%d unavailable: %s", C->ch, esp_err_to_name(err));
            led_hw_ready = false;
        } else if (err != ESP_OK){
            ESP_LOGW(TAG, "aled_rmt_write off ch%d failed: %s", C->ch, esp_err_to_name(err));
        }
        if (!self_test_delay(SELF_TEST_COLOR_DELAY_MS)){
            break;
        }
    }

    bool pwm_hw_ready = true;
    for (uint8_t pwm_ch = 0; pwm_ch < 16 && !s_cancel_requested && pwm_hw_ready; ++pwm_ch){
        esp_err_t err = pca9685_set_duty(pwm_ch, 1.0f);
        if (err == ESP_ERR_INVALID_STATE){
            ESP_LOGW(TAG, "pca9685 ch%u unavailable: %s", pwm_ch, esp_err_to_name(err));
            pwm_hw_ready = false;
            break;
        } else if (err != ESP_OK){
            ESP_LOGW(TAG, "pca9685_set_duty on ch%u failed: %s", pwm_ch, esp_err_to_name(err));
        }
        if (!self_test_delay(SELF_TEST_PWM_DELAY_MS)){
            break;
        }
        err = pca9685_set_duty(pwm_ch, 0.0f);
        if (err == ESP_ERR_INVALID_STATE){
            ESP_LOGW(TAG, "pca9685 ch%u unavailable for off: %s", pwm_ch, esp_err_to_name(err));
            pwm_hw_ready = false;
            break;
        } else if (err != ESP_OK){
            ESP_LOGW(TAG, "pca9685_set_duty off ch%u failed: %s", pwm_ch, esp_err_to_name(err));
        }
        if (!self_test_delay(SELF_TEST_PWM_DELAY_MS)){
            break;
        }
    }

    if (pwm_hw_ready){
        for (uint8_t pwm_ch = 0; pwm_ch < 16; ++pwm_ch){
            pca9685_set_duty(pwm_ch, 0.0f);
        }
    }

    bool cancelled = s_cancel_requested;
    s_cancel_requested = false;
    s_self_test_task = NULL;
    ESP_LOGI(TAG, "Self-test %s", cancelled ? "cancelled" : "complete");
    vTaskDelete(NULL);
}

void self_test_run(void){
    if (s_self_test_task){
        ESP_LOGW(TAG, "Self-test already running");
        return;
    }

    s_cancel_requested = false;
    TaskHandle_t handle = NULL;
    BaseType_t ok = xTaskCreate(self_test_task, "self_test", SELF_TEST_STACK, NULL, SELF_TEST_PRIORITY, &handle);
    if (ok != pdPASS || !handle){
        ESP_LOGE(TAG, "Failed to create self-test task (err=%ld)", (long)ok);
        s_self_test_task = NULL;
        return;
    }

    s_self_test_task = handle;
    ESP_LOGI(TAG, "Self-test task spawned");
}

void self_test_cancel(void){
    TaskHandle_t handle = s_self_test_task;
    if (!handle){
        ESP_LOGW(TAG, "Self-test cancel requested but task not running");
        return;
    }

    s_cancel_requested = true;
    xTaskNotifyGive(handle);
    ESP_LOGI(TAG, "Self-test cancellation signalled");
}

bool self_test_is_running(void){
    return s_self_test_task != NULL;
}
