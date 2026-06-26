/**
 * @file espnow_wifi.cpp
 * @brief Implementación de la inicialización WiFi.
 */

#include "espnow_wifi.h"
#include "espnow_types.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "espnow_wifi";

static bool s_event_loop_created = false;

/* ── Event group para conexión STA (OTA) ────────────────────────────────── */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     5

static EventGroupHandle_t s_sta_event_group = NULL;
static int s_retry_count = 0;

/* ── Handler de eventos WiFi (usado solo en modo STA conectado) ─────────── */
static void sta_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "Retrying WiFi connection (%d/%d)", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_sta_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        xEventGroupSetBits(s_sta_event_group, WIFI_CONNECTED_BIT);
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/* ── Implementación pública ─────────────────────────────────────────────── */

void espnow_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());

    if (!s_event_loop_created) {
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_event_loop_created = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;   /* NVS lo gestiona el módulo espnow_nvs */
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA (ESP-NOW mode) started");
}

esp_err_t espnow_wifi_connect_sta(const char *ssid, const char *password)
{
    s_sta_event_group = xEventGroupCreate();
    s_retry_count = 0;

    /* Desregistra ESP-NOW y para WiFi para reconfigurarlo */
    esp_now_deinit();
    esp_wifi_stop();

    if (!s_event_loop_created) {
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_event_loop_created = true;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t h1, h2;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, sta_event_handler, NULL, &h1));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, sta_event_handler, NULL, &h2));

    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid,     ssid,     sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);  /* Máximo throughput para OTA */

    ESP_LOGI(TAG, "Connecting to AP: %s", ssid);

    EventBits_t bits = xEventGroupWaitBits(
        s_sta_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    vEventGroupDelete(s_sta_event_group);
    s_sta_event_group = NULL;

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to %s", ssid);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to connect to %s", ssid);
    return ESP_FAIL;
}

bool espnow_wifi_event_loop_running(void)
{
    return s_event_loop_created;
}
