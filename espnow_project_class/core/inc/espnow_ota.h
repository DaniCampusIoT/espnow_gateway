#pragma once

/**
 * @file espnow_ota.h
 * @brief API de actualización OTA vía HTTPS.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configura los parámetros OTA.
 * @param url       URL HTTPS del binario firmware.
 * @param ssid      SSID del AP para conectarse durante la actualización.
 * @param password  Contraseña del AP.
 */
void espnow_ota_set_config(const char *url, const char *ssid, const char *password);

/**
 * @brief Inicia la actualización OTA.
 *        Conecta al AP, descarga el firmware, valida y reinicia.
 *        Llama a esp_restart() al finalizar (con éxito o error).
 *        Debe llamarse desde una tarea FreeRTOS con stack suficiente (≥8KB).
 */
void espnow_ota_start(void);

/**
 * @brief Tarea FreeRTOS que ejecuta la actualización OTA.
 *        Registrar con xTaskCreate(..., 8192, ...).
 */
void espnow_ota_task(void *pvParameters);

#ifdef __cplusplus
}
#endif
