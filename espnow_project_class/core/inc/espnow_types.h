#pragma once

/**
 * @file espnow_types.h
 * @brief Tipos, constantes y estructuras compartidas por todos los módulos.
 *        SIN cuerpos de función (evita múltiples definiciones al incluir en varias TUs).
 */

#include <stdint.h>
#include "esp_now.h"

/* ── Códigos de retorno ─────────────────────────────────────────────────── */
#define ENVIO_OK              0
#define ERROR_NOT_PAIRED      1
#define ERROR_MSG_TOO_LARGE   2
#define ERROR_SIN_RESPUESTA   3
#define ERROR_ENVIO_ESPNOW    4

/* ── Tipos de mensaje (bits 0-1) ────────────────────────────────────────── */
#define MSG_PAIRING    0b00000000
#define MSG_DATA       0b00000001
#define MSG_PAN_DATA   0b00000010
#define MSG_NODATA     0b00000011
#define MASK_MSG_TYPE  0b00000011

/* ── Flags de mensaje ───────────────────────────────────────────────────── */
#define FLAG_CHECK     0b10000000   /**< Solicita mensajes pendientes al gateway */
#define FLAG_RESERVED  0b01000000

/* ── Dirección PAN (bits 2-5) ───────────────────────────────────────────── */
#define MASK_PAN       0b00111100
#define PAN_OFFSET     2

/* ── Roles de nodo ──────────────────────────────────────────────────────── */
#define NODE_GATEWAY   0
#define NODE_DEVICE    1

/* ── NVS magic codes ────────────────────────────────────────────────────── */
#define MAGIC_CODE1    0xA5A5
#define MAGIC_CODE2    0xC7C7
#define INVALID_CODE   0

/* ── WiFi / ESP-NOW ─────────────────────────────────────────────────────── */
#define ESPNOW_WIFI_MODE     WIFI_MODE_STA
#define ESPNOW_WIFI_IF       WIFI_IF_STA
#define ESPNOW_MAXDELAY      512
#define ESPNOW_QUEUE_SIZE    10
#define ESPNOW_CTRL_BYTE     1
#define ESPNOW_MAX_READINGS  150

/* ── Layout del mensaje PAN ─────────────────────────────────────────────── */
#define PAN_type_offset    0
#define PAN_MAC_offset     1
#define PAN_MAC_size       6
#define PAN_MSold_offset   7   /* 1 + 6 */
#define PAN_MSold_size     4
#define PAN_payload_offset 11  /* 1 + 6 + 4 */

/* ── Tamaño máximo de configuración en NVS ──────────────────────────────── */
#ifndef MAX_CONFIG_SIZE
#define MAX_CONFIG_SIZE 64
#endif

/* ── Tamaño máximo de payload ESP-NOW ───────────────────────────────────── */
#define ESPNOW_PAYLOAD_SIZE  243

/* ── OTA ────────────────────────────────────────────────────────────────── */
#define OTA_RECV_TIMEOUT   5000
#define OTA_MAX_RETRY      5

/* ═══════════════════════════════════════════════════════════════════════════
   Tipos / estructuras
   ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    UPDATE_NONE = 0,
    UPDATE_AVAILABLE,
} update_status_t;

/** Estructura de emparejamiento ESP-NOW */
typedef struct {
    uint8_t  msgType;
    uint8_t  id;
    uint8_t  macAddr[6];
    uint8_t  channel;
    uint8_t  padding[3];
} espnow_pairing_t;

/** Resultado de un envío ESP-NOW (uso interno en colas FreeRTOS) */
typedef struct {
    uint8_t              mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} espnow_send_cb_t;

/** Trama ESP-NOW completa */
typedef struct {
    uint8_t msgType;
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t payload[ESPNOW_PAYLOAD_SIZE];
} espnow_frame_t;

/** Mensaje recibido ya parseado (topic + payload heap-allocated) */
typedef struct {
    char    *topic;           /**< Null-terminated; caller must free() */
    char    *payload;         /**< Null-terminated; caller must free() */
    uint8_t  macAddr[6];
    uint32_t ms_old;
} espnow_rcv_msg_t;

/** Configuración persistida en NVS (user-space) */
typedef struct {
    uint8_t  pan;
    uint16_t timeout_ms;
    uint16_t sleep_sec;
    uint16_t reserved;
} espnow_config_t;

/* ── Callbacks de usuario ───────────────────────────────────────────────── */
typedef void (*espnow_mqtt_cb_t)(espnow_rcv_msg_t *msg);
typedef void (*espnow_pan_cb_t) (espnow_rcv_msg_t *msg);

/* ── Utilidades inline (sin linkage externo) ────────────────────────────── */

/**
 * @brief Convierte un tipo de mensaje a string descriptivo.
 * @note  El caller es responsable de liberar la memoria con free().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline char *espnow_msg_type_str(uint8_t type)
{
    const char *base;
    switch (type & MASK_MSG_TYPE) {
        case MSG_PAN_DATA: base = "PAN_DATA";   break;
        case MSG_DATA:     base = "DATA";        break;
        case MSG_PAIRING:  base = "PAIRING";     break;
        case MSG_NODATA:   base = "NODATA";      break;
        default:           base = "UNKNOWN";     break;
    }
    char *buf = (char *)malloc(64);
    if (!buf) return NULL;
    snprintf(buf, 64, "%s PAN:%d%s",
             base,
             (type & MASK_PAN) >> PAN_OFFSET,
             (type & FLAG_CHECK) ? " +CHECK" : "");
    return buf;
}
