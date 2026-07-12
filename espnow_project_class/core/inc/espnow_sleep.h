#pragma once

/**
 * @file espnow_sleep.h
 * @brief Gestión de deep sleep con tiempos configurables.
 */

#include <stdint.h>
#include "esp_sleep.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configura el tiempo de sleep.
 * @param sleep_sec  Segundos de deep sleep.
 */
void espnow_sleep_set_duration(uint32_t sleep_sec);

/**
 * @brief Registra la hora de entrada a sleep (debe llamarse justo antes).
 *        Permite calcular el tiempo dormido al despertar.
 */
void espnow_sleep_record_entry(void);

/**
 * @brief Devuelve el tiempo dormido en ms desde el último record_entry.
 */
int32_t espnow_sleep_elapsed_ms(void);

/**
 * @brief Entra en deep sleep.
 *        Si enable == false, solo registra y retorna (útil para depuración).
 */
void espnow_sleep_enter(bool enable);

#ifdef __cplusplus
}
#endif
