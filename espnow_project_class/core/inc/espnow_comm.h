#pragma once

/**
 * @file espnow_comm.h
 * @brief API de envío y recepción ESP-NOW.
 *
 * Expone:
 *  - espnow_comm_init()      : registra callbacks send/recv y crea colas internas.
 *  - espnow_send()           : encola un mensaje para envío.
 *  - espnow_send_task()      : tarea FreeRTOS que procesa la cola de envío.
 *  - espnow_recv_task()      : tarea FreeRTOS que procesa la cola de recepción.
 */

#include "espnow_types.h"
#include "espnow_nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el módulo de comunicación ESP-NOW.
 *
 * @param store         Store NVS con info de pairing (gateway MAC + channel).
 * @param send_sem      Semáforo binario de control de envío (tomado por send_task,
 *                      dado por recv_task o por el ACK de send_cb).
 * @param pan_address   Identificador PAN de este nodo.
 * @param mqtt_cb       Callback de mensajes MQTT descendentes (puede ser NULL).
 * @param pan_cb        Callback de mensajes PAN (puede ser NULL).
 */
void espnow_comm_init(nvs_store_t       *store,
                      SemaphoreHandle_t  send_sem,
                      uint32_t           pan_address,
                      espnow_mqtt_cb_t   mqtt_cb,
                      espnow_pan_cb_t    pan_cb);

/**
 * @brief Encola un mensaje para ser enviado por send_task.
 *
 * @param topic    Topic MQTT (null-terminated).
 * @param payload  Payload JSON (null-terminated).
 * @param request_check  Si true, añade FLAG_CHECK al tipo de mensaje.
 * @return ENVIO_OK o código de error.
 */
uint8_t espnow_send(const char *topic,
                    const char *payload,
                    bool        request_check);

/**
 * @brief Tarea FreeRTOS de envío. Stack recomendado: 4096.
 */
void espnow_send_task(void *pvParameters);

/**
 * @brief Tarea FreeRTOS de recepción. Stack recomendado: 4096.
 */
void espnow_recv_task(void *pvParameters);

/**
 * @brief Indica si hay slot de envío disponible (sem tomado y listo).
 */
bool espnow_comm_send_available(uint32_t wait_ms);

#ifdef __cplusplus
}
#endif
