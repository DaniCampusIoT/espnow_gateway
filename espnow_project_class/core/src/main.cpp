/**
 * @file main.cpp
 * @brief Punto de entrada del firmware ESP-NOW sensor node.
 *
 * Arquitectura de tareas FreeRTOS:
 *
 *   Core 0:
 *     pairing_task  (prio 5) – state machine de pairing
 *     send_task     (prio 4) – procesa cola de envíos
 *     recv_task     (prio 4) – procesa cola de recepciones
 *
 *   Core 1:
 *     sensor_task   (prio 3) – lee ADC, construye JSON y llama a espnow_send()
 *     ota_task      (prio 2) – solo se crea si hay actualización disponible
 *
 * Comunicación entre tareas exclusivamente mediante colas y semáforos FreeRTOS.
 * No hay variables globales compartidas directamente entre tareas.
 *
 * Credenciales WiFi (OTA) y URL de firmware configuradas mediante menuconfig
 * (Kconfig.projbuild).
 */

#include "AUTOpairing.h"
#include "AnomalyDetection.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

extern "C" { void app_main(void); }

static const char *TAG = "main";

/* ── Credenciales desde menuconfig ─────────────────────────────────────── */
#ifndef CONFIG_ESPNOW_OTA_WIFI_SSID
#define CONFIG_ESPNOW_OTA_WIFI_SSID     "MyWiFi"
#endif
#ifndef CONFIG_ESPNOW_OTA_WIFI_PASS
#define CONFIG_ESPNOW_OTA_WIFI_PASS     "MyPassword"
#endif
#ifndef CONFIG_ESPNOW_OTA_URL
#define CONFIG_ESPNOW_OTA_URL           "https://example.com/firmware.bin"
#endif

/* ── Configuración de sensores ADC ──────────────────────────────────────── */
static adc_channel_t s_adc_channels[] = {
    ADC_CHANNEL_0,
    ADC_CHANNEL_1,
    ADC_CHANNEL_2,
    ADC_CHANNEL_4,
};
static const uint8_t NUM_ADC_CHANNELS =
    sizeof(s_adc_channels) / sizeof(adc_channel_t);

/* ── Configuración del nodo ─────────────────────────────────────────────── */
#define NODE_PAN_ID    4     /**< Identificador de red PAN */
#define NODE_APP_ID    5     /**< Identificador de aplicación dentro de la PAN */
#define ESPNOW_START_CHANNEL  1

/* ── Datos en RTC RAM (sobreviven al deep sleep) ────────────────────────── */
RTC_DATA_ATTR static uint16_t s_own_raw[ESPNOW_MAX_READINGS];
RTC_DATA_ATTR static uint16_t s_peer_raw[ESPNOW_MAX_READINGS];
RTC_DATA_ATTR static uint8_t  s_own_count  = 0;
RTC_DATA_ATTR static uint8_t  s_peer_count = 0;

/* ── Estado de actualización OTA ────────────────────────────────────────── */
static volatile update_status_t s_update_status = UPDATE_NONE;

/* ── NVS store (compartido entre módulos, acceso serializado por tareas) ─ */
static nvs_store_t s_nvs_store;

/* ── Semáforo de control de envío ───────────────────────────────────────── */
static SemaphoreHandle_t s_send_ready = NULL;   /**< dado cuando pairing OK */
static SemaphoreHandle_t s_send_sem   = NULL;   /**< control flujo de envío */

/* ══════════════════════════════════════════════════════════════════════════
   Callbacks de usuario
   ══════════════════════════════════════════════════════════════════════════ */

static void on_mqtt_msg(espnow_rcv_msg_t *msg)
{
    if (!msg || !msg->topic || !msg->payload) return;

    ESP_LOGI(TAG, "MQTT msg | topic: %s | payload: %s",
             msg->topic, msg->payload);

    cJSON *root = cJSON_Parse(msg->payload);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        goto cleanup;
    }

    if (strcmp(msg->topic, "config") == 0) {
        /* Actualiza configuración dinámica */
        cJSON *j_sleep   = cJSON_GetObjectItem(root, "sleep");
        cJSON *j_timeout = cJSON_GetObjectItem(root, "timeout");

        if (j_sleep)   espnow_sleep_set_duration(j_sleep->valueint);
        if (j_timeout) { /* podría actualizar el timeout de pairing */ }

        /* Persiste en NVS */
        if (j_sleep)   s_nvs_store.config[2] = (uint16_t)j_sleep->valueint;
        if (j_timeout) s_nvs_store.config[1] = (uint16_t)j_timeout->valueint;
        s_nvs_store.code2 = MAGIC_CODE2;
        espnow_nvs_save(&s_nvs_store);
        ESP_LOGI(TAG, "Config updated");
    }
    else if (strcmp(msg->topic, "update") == 0) {
        ESP_LOGI(TAG, "OTA update requested");
        s_update_status = UPDATE_AVAILABLE;
        /* La tarea OTA se crea dinámicamente */
        xTaskCreatePinnedToCore(
            espnow_ota_task, "ota_task",
            8192, NULL, 2, NULL, 1);
    }

cleaning:
    cJSON_Delete(root);
cleanup:
    free(msg->topic);
    free(msg->payload);
    free(msg);
}

static void on_pan_msg(espnow_rcv_msg_t *msg)
{
    if (!msg || !msg->payload) return;

    ESP_LOGI(TAG, "PAN msg | age: %" PRIu32 " ms | MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             msg->ms_old,
             msg->macAddr[0], msg->macAddr[1], msg->macAddr[2],
             msg->macAddr[3], msg->macAddr[4], msg->macAddr[5]);

    cJSON *root = cJSON_Parse(msg->payload);
    if (root) {
        cJSON *sensor0 = cJSON_GetObjectItem(root, "Sensor0");
        if (sensor0) {
            cJSON *raw = cJSON_GetObjectItem(sensor0, "raw_voltage");
            if (raw) {
                s_peer_raw[s_peer_count % ESPNOW_MAX_READINGS] =
                    (uint16_t)raw->valueint;
                s_peer_count++;
                ESP_LOGI(TAG, "Peer raw: %d", raw->valueint);
            }
        }
        cJSON_Delete(root);
    }

    free(msg->payload);
    free(msg);
}

/* ══════════════════════════════════════════════════════════════════════════
   app_main
   ══════════════════════════════════════════════════════════════════════════ */

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP-NOW Sensor Node booting ===");
    ESP_LOGI(TAG, "Deep sleep elapsed: %" PRId32 " ms",
             espnow_sleep_elapsed_ms());

    /* 1. Cargar NVS ──────────────────────────────────────────────────────── */
    espnow_nvs_load(&s_nvs_store);

    /* 2. Leer configuración guardada ────────────────────────────────────── */
    uint32_t timeout_ms = 3000;
    uint32_t sleep_sec  = 30;
    if (s_nvs_store.code2 == MAGIC_CODE2) {
        timeout_ms = s_nvs_store.config[1];
        sleep_sec  = s_nvs_store.config[2];
        ESP_LOGI(TAG, "Config from NVS: timeout=%" PRIu32 " ms, sleep=%" PRIu32 " s",
                 timeout_ms, sleep_sec);
    }

    /* 3. Configurar deep sleep ──────────────────────────────────────────── */
    espnow_sleep_set_duration(sleep_sec);

    /* 4. Configurar OTA ─────────────────────────────────────────────────── */
    espnow_ota_set_config(
        CONFIG_ESPNOW_OTA_URL,
        CONFIG_ESPNOW_OTA_WIFI_SSID,
        CONFIG_ESPNOW_OTA_WIFI_PASS);

    /* 5. Inicializar WiFi (modo ESP-NOW) ─────────────────────────────────── */
    espnow_wifi_init();

    /* 6. Crear semáforos ─────────────────────────────────────────────────── */
    s_send_ready = xSemaphoreCreateBinary();   /* dado por pairing_task al emparejar */
    s_send_sem   = xSemaphoreCreateBinary();   /* control de flujo de envío */
    xSemaphoreGive(s_send_sem);                /* libre al arrancar */

    /* 7. Inicializar módulo de comunicación ─────────────────────────────── */
    espnow_comm_init(&s_nvs_store, s_send_sem,
                     NODE_PAN_ID, on_mqtt_msg, on_pan_msg);

    /* 8. Inicializar módulo de pairing ──────────────────────────────────── */
    espnow_pairing_init(&s_nvs_store, s_send_ready,
                        ESPNOW_START_CHANNEL, timeout_ms);

    /* 9. Crear tareas FreeRTOS ──────────────────────────────────────────── */
    ESP_LOGI(TAG, "Creating FreeRTOS tasks");

    xTaskCreatePinnedToCore(
        espnow_pairing_task, "pairing_task",
        4096, NULL, 5, NULL, 0);

    xTaskCreatePinnedToCore(
        espnow_send_task, "send_task",
        4096, NULL, 4, NULL, 0);

    xTaskCreatePinnedToCore(
        espnow_recv_task, "recv_task",
        4096, NULL, 4, NULL, 0);

    /* Parámetros de la tarea de sensor (estáticos: no se liberan) */
    static sensor_task_params_t sensor_params = {
        .channels      = s_adc_channels,
        .num_channels  = NUM_ADC_CHANNELS,
        .send_topic    = "sensors/node",
        .request_check = true,
    };

    xTaskCreatePinnedToCore(
        espnow_sensor_task, "sensor_task",
        6144, &sensor_params, 3, NULL, 1);

    /* 10. app_main termina: FreeRTOS scheduler gestiona todo ────────────── */
    ESP_LOGI(TAG, "All tasks created – scheduler running");

    /*
     * Espera a que pairing_task libere s_send_ready.
     * Si no se empareja en timeout_ms, también se libera para hacer deep sleep.
     */
    if (xSemaphoreTake(s_send_ready, portMAX_DELAY) == pdTRUE) {
        if (!espnow_pairing_is_paired()) {
            ESP_LOGW(TAG, "Pairing timed out – entering deep sleep");
            espnow_sleep_enter(true);
        }
    }
    /* Si llegamos aquí es que estamos emparejados y operando normalmente */
}
