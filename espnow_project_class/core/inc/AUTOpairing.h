#pragma once

/**
 * @file AUTOpairing.h
 * @brief Fachada pública del sistema ESP-NOW auto-pairing.
 *
 * Esta cabecera es el único punto de entrada para el usuario final.
 * Incluye y reexporta todos los módulos necesarios. El código de aplicación
 * solo necesita incluir este fichero.
 *
 * Módulos internos:
 *   espnow_types.h   – tipos, constantes y estructuras
 *   espnow_nvs.h     – persistencia NVS
 *   espnow_wifi.h    – init WiFi/ESP-NOW
 *   espnow_pairing.h – máquina de estados de pairing
 *   espnow_comm.h    – envío y recepción con colas FreeRTOS
 *   espnow_ota.h     – actualización OTA HTTPS
 *   espnow_sleep.h   – deep sleep configurable
 *   espnow_sensor.h  – tarea de sensor ADC
 */

#include "espnow_types.h"
#include "espnow_nvs.h"
#include "espnow_wifi.h"
#include "espnow_pairing.h"
#include "espnow_comm.h"
#include "espnow_ota.h"
#include "espnow_sleep.h"
#include "espnow_sensor.h"
#include "ADConeshot.h"

/* Compatibilidad con código legado ───────────────────────────────────────── */
/* Los siguientes aliases permiten que código antiguo que use los nombres
   originales siga compilando sin cambios. */

typedef espnow_rcv_msg_t  struct_espnow_rcv_msg;
typedef espnow_pairing_t  struct_pairing;
typedef espnow_config_t   struct_config;
typedef update_status_t   UpdateStatus;

#define NO_UPDATE_FOUND   UPDATE_NONE
#define THERE_IS_AN_UPDATE_AVAILABLE UPDATE_AVAILABLE

#define DATA     MSG_DATA
#define PAIRING  MSG_PAIRING
#define NODATA   MSG_NODATA
#define PAN_DATA MSG_PAN_DATA
#define CHECK    FLAG_CHECK
