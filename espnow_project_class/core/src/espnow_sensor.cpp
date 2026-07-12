/**
 * @file espnow_sensor.cpp
 * @brief Lectura ADC + construcción JSON + envío ESP-NOW.
 */

#include "espnow_sensor.h"
#include "espnow_comm.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "espnow_sensor";

void espnow_sensor_task(void *pvParameters)
{
    sensor_task_params_t *params = (sensor_task_params_t *)pvParameters;

    /* Espera a que el envío esté disponible (pairing completado) */
    while (!espnow_comm_send_available(1000)) {
        ESP_LOGD(TAG, "Waiting for comm ready...");
    }

    ESP_LOGI(TAG, "Building sensor payload");

    /* ── Inicializar ADC ── */
    ADConeshot_t adc;
    struct_adclist *reads = adc.set_adc_channel(params->channels,
                                                 params->num_channels);

    /* ── Construir JSON ── */
    cJSON *root = cJSON_CreateObject();

    /* Versión firmware */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t fw_info;
    if (esp_ota_get_partition_description(running, &fw_info) == ESP_OK)
        cJSON_AddStringToObject(root, "fw_version", fw_info.version);

    /* Lecturas ADC */
    char tag[16];
    for (int i = 0; i < reads->length; i++) {
        snprintf(tag, sizeof(tag), "Sensor%d", i);
        cJSON *sensor = cJSON_CreateObject();
        cJSON_AddNumberToObject(sensor, "adc_voltage",
                                reads->channels[i].voltage_mv);
        cJSON_AddItemToObject(root, tag, sensor);
        ESP_LOGI(TAG, "Sensor%d: %d mV", i, reads->channels[i].voltage_mv);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Payload (%d bytes): %s", strlen(json_str), json_str);

    /* ── Enviar ── */
    uint8_t ret = espnow_send(params->send_topic, json_str,
                              params->request_check);
    if (ret != ENVIO_OK)
        ESP_LOGE(TAG, "Send error: %d", ret);

    cJSON_free(json_str);
    cJSON_Delete(root);
    free(reads);

    ESP_LOGI(TAG, "Sensor task done");
    vTaskDelete(NULL);
}
