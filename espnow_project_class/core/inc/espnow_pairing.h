#pragma once

/**
 * @file espnow_pairing.h
 * @brief Máquina de estados de pairing ESP-NOW.
 *
 * La tarea pairing_task gestiona el estado PAIR_REQUEST → PAIR_REQUESTED → PAIR_PAIRED.
 * Una vez emparejado, libera el semáforo s_send_ready para que send_task pueda operar.
 */

#include "espnow_types.h"
#include "espnow_nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Estados de la máquina de emparejamiento */
typedef enum {
    PAIR_STATE_REQUEST   = 0,  /**< Enviando broadcast de solicitud */
    PAIR_STATE_REQUESTED,      /**< Esperando respuesta del gateway */
    PAIR_STATE_PAIRED,         /**< Emparejado y listo para enviar */
} pair_state_t;

/**
 * @brief Inicializa el módulo de pairing.
 * @param store       Puntero al store NVS ya cargado.
 * @param send_ready  Semáforo que se da cuando el pairing está completado.
 * @param start_chan  Canal inicial de escaneo (1–13).
 * @param timeout_ms  Timeout de pairing en ms (0 = sin límite).
 */
void espnow_pairing_init(nvs_store_t *store,
                         SemaphoreHandle_t send_ready,
                         uint8_t start_chan,
                         uint32_t timeout_ms);

/**
 * @brief Tarea FreeRTOS de pairing.
 *        Registrar con xTaskCreate(..., 4096, ..., 5, ...).
 */
void espnow_pairing_task(void *pvParameters);

/**
 * @brief Devuelve true si el nodo está emparejado.
 */
bool espnow_pairing_is_paired(void);

/**
 * @brief Devuelve la MAC del gateway emparejado.
 * @param[out] mac  Buffer de 6 bytes.
 */
void espnow_pairing_get_gateway_mac(uint8_t mac[6]);

/**
 * @brief Devuelve el canal del gateway emparejado.
 */
uint8_t espnow_pairing_get_channel(void);

/**
 * @brief Notifica al módulo de pairing que se recibió un mensaje PAIRING
 *        desde el gateway. Llamar desde el callback de recepción ESP-NOW.
 * @param pkt  Puntero al paquete de pairing recibido.
 */
void espnow_pairing_on_recv(const espnow_pairing_t *pkt);

#ifdef __cplusplus
}
#endif
