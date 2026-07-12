#pragma once

/**
 * @file espnow_nvs.h
 * @brief API de persistencia NVS para pairing y configuración de usuario.
 */

#include <stdbool.h>
#include <stdint.h>
#include "espnow_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estructura interna guardada en NVS.
 *        code1 == MAGIC_CODE1  →  pairing válido
 *        code2 == MAGIC_CODE2  →  config de usuario válida
 */
typedef struct {
    uint16_t       code1;
    uint16_t       code2;
    espnow_pairing_t pairing;
    uint16_t       config[MAX_CONFIG_SIZE];
} nvs_store_t;

/**
 * @brief Inicializa el subsistema NVS y carga datos si existen.
 * @param[out] store  Estructura que se rellena con los datos leídos.
 * @return true si la lectura fue exitosa (store contiene datos válidos).
 */
bool espnow_nvs_load(nvs_store_t *store);

/**
 * @brief Persiste la estructura store en NVS.
 * @param[in] store  Datos a guardar.
 */
void espnow_nvs_save(const nvs_store_t *store);

/**
 * @brief Invalida el pairing almacenado (borra magic code1).
 * @param[in,out] store  Estructura a invalidar y guardar.
 */
void espnow_nvs_invalidate_pairing(nvs_store_t *store);

#ifdef __cplusplus
}
#endif
