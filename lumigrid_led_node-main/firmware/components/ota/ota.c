#include "ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_check.h"
#include <string.h>

static const char* TAG = "OTA";
static esp_ota_handle_t s_ota_handle;
static const esp_partition_t* s_update_part = NULL;

esp_err_t ota_http_url(const char* url) {
  if (!url) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_http_client_config_t cfg = {
    .url = url,
    .timeout_ms = 20000
  };
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  ESP_RETURN_ON_FALSE(client, ESP_FAIL, TAG, "http init failed");
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return err;
  }
  int content_length = esp_http_client_fetch_headers(client);
  s_update_part = esp_ota_get_next_update_partition(NULL);
  ESP_RETURN_ON_FALSE(s_update_part, ESP_FAIL, TAG, "no update partition");
  ESP_ERROR_CHECK(esp_ota_begin(s_update_part, content_length, &s_ota_handle));

  uint8_t buf[4096];
  int read;
  while ((read = esp_http_client_read(client, (char*)buf, sizeof(buf))) > 0) {
    ESP_ERROR_CHECK(esp_ota_write(s_ota_handle, buf, read));
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  ESP_ERROR_CHECK(esp_ota_end(s_ota_handle));
  ESP_ERROR_CHECK(esp_ota_set_boot_partition(s_update_part));
  ESP_LOGI(TAG, "OTA update applied, rebooting");
  esp_restart();
  return ESP_OK;
}

esp_err_t ota_begin_upload(void) {
  s_update_part = esp_ota_get_next_update_partition(NULL);
  if (!s_update_part) {
    return ESP_FAIL;
  }
  return esp_ota_begin(s_update_part, OTA_SIZE_UNKNOWN, &s_ota_handle);
}

esp_err_t ota_write_chunk(const uint8_t* data, int len) {
  if (!data || len <= 0) {
    return ESP_ERR_INVALID_ARG;
  }
  return esp_ota_write(s_ota_handle, data, len);
}

esp_err_t ota_end_and_commit(void) {
  ESP_ERROR_CHECK(esp_ota_end(s_ota_handle));
  ESP_ERROR_CHECK(esp_ota_set_boot_partition(s_update_part));
  return ESP_OK;
}

void ota_abort(void) {
  esp_ota_abort(s_ota_handle);
  s_update_part = NULL;
}

esp_err_t ota_mark_valid(void) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      ESP_LOGI(TAG, "Marking OTA image as valid");
      return esp_ota_mark_app_valid_cancel_rollback();
    }
  }
  return ESP_OK;
}

esp_err_t ota_check_rollback(void) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    ESP_LOGI(TAG, "OTA state: %d", ota_state);
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      ESP_LOGW(TAG, "OTA image pending verification - will rollback if not marked valid");
      // Return OK - this is normal during first boot after OTA
      return ESP_OK;
    } else if (ota_state == ESP_OTA_IMG_INVALID) {
      ESP_LOGE(TAG, "OTA image marked invalid - triggering rollback");
      esp_ota_mark_app_invalid_rollback_and_reboot();
      // Function does not return - device will reboot
    }
  }
  return ESP_OK;
}
