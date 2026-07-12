#pragma once

/**
 * @file espnow_sensor.h
 * @brief Tarea de lectura de sensores ADC y serialización JSON.
 *
 * sensor_task lee los canales ADC configurados, construye el JSON
 * y lo encola para envío mediante espnow_send().
 */

#include "espnow_types.h"
#include "ADConeshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parámetros de configuración para sensor_task.
 */
typedef struct {
    adc_channel_t *channels;     /**< Array de canales ADC */
    uint8_t        num_channels; /**< Número de canales */
    const char    *send_topic;   /**< Topic MQTT de destino */
    bool           request_check;/**< Solicitar mensajes pendientes al gateway */
} sensor_task_params_t;

/**
 * @brief Tarea FreeRTOS de sensor.
 *        Stack recomendado: 6144.
 *        pvParameters: puntero a sensor_task_params_t (no se libera).
 */
void espnow_sensor_task(void *pvParameters);

#ifdef __cplusplus
}
#endif
