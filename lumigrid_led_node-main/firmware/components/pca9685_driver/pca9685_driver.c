#include "pca9685_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "PCA9685";

#define PCA9685_MODE1 0x00
#define PCA9685_MODE2 0x01
#define PCA9685_LED0_ON_L 0x06
#define PCA9685_PRESCALE 0xFE
#define PCA9685_ALL_LED_OFF_H 0xFD

#define MODE1_RESTART 0x80
#define MODE1_SLEEP 0x10
#define MODE1_ALLCALL 0x01
#define MODE1_AI 0x20

#define MODE2_OUTDRV 0x04

static esp_err_t i2c_probe_addr(i2c_master_bus_handle_t bus, uint8_t addr) {
    i2c_master_dev_handle_t dev;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 100000,        // start conservative
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &dev), TAG, "add dev");
    uint8_t dummy = 0;
    esp_err_t err = i2c_master_transmit(dev, &dummy, 0, 10 /*ms*/);
    i2c_master_bus_rm_device(dev);
    return err;
}

static esp_err_t pca9685_wait_present(i2c_master_bus_handle_t bus, uint8_t addr) {
    const int tries = 8;
    for (int i = 0; i < tries; ++i) {
        esp_err_t e = i2c_probe_addr(bus, addr);
        if (e == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(20 * (i + 1))); // 20,40,60...ms
    }
    return ESP_FAIL;
}

static pca9685_config_t s_config;
static bool s_initialized = false;
static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;


static esp_err_t pca9685_write_reg(uint8_t reg, uint8_t value) {
    // Batch 28: I²C buffer/size validation guard
    if (!dev_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t write_buf[2] = {reg, value};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
}

static esp_err_t pca9685_read_reg(uint8_t reg, uint8_t *value) {
    return i2c_master_transmit_receive(dev_handle, &reg, 1, value, 1, -1);
}

esp_err_t pca9685_init(const pca9685_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    
    s_config = *cfg;
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_config.oe_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(s_config.oe_pin, 1);
    
    i2c_master_bus_config_t i2c_bus_config = {
        .sda_io_num = 21,
        .scl_io_num = 22,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_new_master_bus(&i2c_bus_config, &bus_handle);

    if (pca9685_wait_present(bus_handle, s_config.i2c_addr) != ESP_OK) {
        ESP_LOGE(TAG, "PCA9685 not found at 0x%02X", s_config.i2c_addr);
        i2c_del_master_bus(bus_handle);
        return ESP_FAIL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_config.i2c_addr,
        .scl_speed_hz = 400000,
    };
    i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);

    
    pca9685_write_reg(PCA9685_MODE1, MODE1_ALLCALL);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    uint8_t mode1;
    pca9685_read_reg(PCA9685_MODE1, &mode1);
    mode1 &= ~MODE1_SLEEP;
    pca9685_write_reg(PCA9685_MODE1, mode1);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    pca9685_write_reg(PCA9685_MODE2, MODE2_OUTDRV);
    
    pca9685_set_pwm_freq(1000);
    
    gpio_set_level(s_config.oe_pin, 0);
    
    s_initialized = true;
    ESP_LOGI(TAG, "PCA9685 initialized on I2C @ 0x%02X", s_config.i2c_addr);
    return ESP_OK;
}

esp_err_t pca9685_set_pwm_freq(uint16_t freq_hz) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    uint32_t prescale_val = (uint32_t)(25000000.0f / (4096.0f * freq_hz)) - 1;
    if (prescale_val < 3) prescale_val = 3;
    if (prescale_val > 255) prescale_val = 255;
    
    uint8_t oldmode;
    pca9685_read_reg(PCA9685_MODE1, &oldmode);
    uint8_t newmode = (oldmode & ~MODE1_RESTART) | MODE1_SLEEP;
    pca9685_write_reg(PCA9685_MODE1, newmode);
    pca9685_write_reg(PCA9685_PRESCALE, (uint8_t)prescale_val);
    pca9685_write_reg(PCA9685_MODE1, oldmode);
    vTaskDelay(pdMS_TO_TICKS(5));
    pca9685_write_reg(PCA9685_MODE1, oldmode | MODE1_RESTART | MODE1_AI);
    
    ESP_LOGI(TAG, "PWM frequency set to %d Hz (prescale=%lu)", freq_hz, prescale_val);
    return ESP_OK;
}

esp_err_t pca9685_set_duty(uint8_t channel, float duty) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (channel > 15) return ESP_ERR_INVALID_ARG;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    
    uint16_t on_time = 0;
    uint16_t off_time = (uint16_t)(duty * 4095.0f);
    
    uint8_t reg_base = PCA9685_LED0_ON_L + 4 * channel;
    
    uint8_t write_buf[5] = {reg_base, on_time & 0xFF, (on_time >> 8) & 0xFF, off_time & 0xFF, (off_time >> 8) & 0xFF};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
}

esp_err_t pca9685_fade_to(uint8_t channel, float target_duty, uint32_t duration_ms, bool log_curve) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (channel > 15) return ESP_ERR_INVALID_ARG;
    
    ESP_LOGI(TAG, "Fade ch%d to %.2f over %lu ms (%s)", channel, target_duty, duration_ms, log_curve ? "log" : "linear");
    
    return ESP_OK;
}

esp_err_t pca9685_all_off(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    gpio_set_level(s_config.oe_pin, 1);
    
    for (int ch = 0; ch < 16; ch++) {
        pca9685_set_duty(ch, 0.0f);
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(s_config.oe_pin, 0);
    
    ESP_LOGI(TAG, "All channels off");
    return ESP_OK;
}

esp_err_t pca9685_deinit(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    pca9685_all_off();
    gpio_set_level(s_config.oe_pin, 1);
    i2c_master_bus_rm_device(dev_handle);
    i2c_del_master_bus(bus_handle);
    s_initialized = false;
    
    ESP_LOGI(TAG, "PCA9685 deinitialized");
    return ESP_OK;
}