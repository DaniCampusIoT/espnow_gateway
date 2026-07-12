#pragma once

/**
 * @file espnow_wifi.h
 * @brief Inicialización WiFi en modo STA para ESP-NOW y para OTA.
 */

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa WiFi + ESP-NOW en modo STA (sin conectarse a ningún AP).
 *        Indispensable antes de usar cualquier función ESP-NOW.
 * @note  Llama a esp_event_loop_create_default() internamente si no se ha
 *        creado aún.
 */
void espnow_wifi_init(void);

/**
 * @brief Detiene ESP-NOW y reinicializa WiFi en modo STA conectado a un AP.
 *        Usado exclusivamente por el módulo OTA.
 * @param ssid      SSID del punto de acceso.
 * @param password  Contraseña del punto de acceso.
 * @return ESP_OK si se conectó correctamente, ESP_FAIL si agotó reintentos.
 */
esp_err_t espnow_wifi_connect_sta(const char *ssid, const char *password);

/**
 * @brief Indica si el event loop por defecto ya fue creado.
 *        Útil para evitar llamar a esp_event_loop_create_default() dos veces.
 */
bool espnow_wifi_event_loop_running(void);

#ifdef __cplusplus
}
#endif
