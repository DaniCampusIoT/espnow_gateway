/**
 * @file espnow_ota.cpp
 * @brief Implementación OTA HTTPS con validación de cabecera de imagen.
 */

#include "espnow_ota.h"
#include "espnow_wifi.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_efuse.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "espnow_ota";

static char s_ota_url[256]     = {0};
static char s_ota_ssid[64]     = {0};
static char s_ota_password[64] = {0};

/* ── Validación de cabecera ─────────────────────────────────────────────── */
static esp_err_t validate_image_header(esp_app_desc_t *new_info)
{
    if (!new_info) return ESP_ERR_INVALID_ARG;

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t current_info;
    if (esp_ota_get_partition_description(running, &current_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running FW: %s", current_info.version);
        if (memcmp(new_info->version, current_info.version,
                   sizeof(new_info->version)) == 0) {
            ESP_LOGW(TAG, "Same version – skipping update");
            return ESP_FAIL;
        }
    }

    uint32_t hw_sec = esp_efuse_read_secure_version();
    if (new_info->secure_version < hw_sec) {
        ESP_LOGW(TAG, "Secure version too low (%" PRIu32 " < %" PRIu32 ")",
                 new_info->secure_version, hw_sec);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t http_client_init_cb(esp_http_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}

/* ── API pública ────────────────────────────────────────────────────────── */

void espnow_ota_set_config(const char *url, const char *ssid, const char *password)
{
    strncpy(s_ota_url,      url,      sizeof(s_ota_url) - 1);
    strncpy(s_ota_ssid,     ssid,     sizeof(s_ota_ssid) - 1);
    strncpy(s_ota_password, password, sizeof(s_ota_password) - 1);
}

void espnow_ota_start(void)
{
    ESP_LOGI(TAG, "OTA requested – connecting to AP");

    if (espnow_wifi_connect_sta(s_ota_ssid, s_ota_password) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed – restarting");
        esp_restart();
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    esp_http_client_config_t http_cfg = {};
    http_cfg.url              = s_ota_url;
    http_cfg.timeout_ms       = 5000;
    http_cfg.keep_alive_enable = true;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config        = &http_cfg;
    ota_cfg.http_client_init_cb = http_client_init_cb;

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed");
        esp_restart();
    }

    esp_app_desc_t app_desc;
    if (esp_https_ota_get_img_desc(handle, &app_desc) != ESP_OK ||
        validate_image_header(&app_desc) != ESP_OK) {
        ESP_LOGE(TAG, "Image validation failed");
        esp_https_ota_abort(handle);
        esp_restart();
    }

    while (1) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        ESP_LOGD(TAG, "Downloaded %d bytes",
                 esp_https_ota_get_image_len_read(handle));
    }

    if (!esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "Incomplete download");
        esp_https_ota_abort(handle);
        esp_restart();
    }

    esp_err_t finish_err = esp_https_ota_finish(handle);
    if (err == ESP_OK && finish_err == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful – rebooting");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    if (finish_err == ESP_ERR_OTA_VALIDATE_FAILED)
        ESP_LOGE(TAG, "Image corrupted");
    ESP_LOGE(TAG, "OTA failed (0x%x) – restarting", finish_err);
    esp_restart();
}

void espnow_ota_task(void *pvParameters)
{
    (void)pvParameters;
    espnow_ota_start();
    vTaskDelete(NULL);  /* Nunca debería llegar aquí (esp_restart antes) */
}
