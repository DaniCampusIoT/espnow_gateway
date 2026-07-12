/**
 * @file espnow_nvs.cpp
 * @brief Implementación de la capa de persistencia NVS.
 */

#include "espnow_nvs.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "espnow_nvs";
static const char *NVS_NAMESPACE = "storage";
static const char *NVS_KEY       = "nvs_struct";

bool espnow_nvs_load(nvs_store_t *store)
{
    memset(store, 0, sizeof(nvs_store_t));

    /* Inicializa NVS; borra partición si hay incompatibilidad */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS dirty – erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed (%s) – using defaults", esp_err_to_name(ret));
        return false;
    }

    size_t size = sizeof(nvs_store_t);
    ret = nvs_get_blob(handle, NVS_KEY, store, &size);
    nvs_close(handle);

    if (ret == ESP_OK) {
        /* Sanity: pairing en canal 0 no es válido */
        if (store->code1 == MAGIC_CODE1 && store->pairing.channel == 0) {
            ESP_LOGW(TAG, "Pairing channel=0, invalidating");
            store->code1 = INVALID_CODE;
        }
        ESP_LOGI(TAG, "NVS loaded OK");
        return true;
    }

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No NVS data yet – first boot");
    } else {
        ESP_LOGE(TAG, "NVS read error: %s", esp_err_to_name(ret));
    }
    return false;
}

void espnow_nvs_save(const nvs_store_t *store)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open (RW) failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_blob(handle, NVS_KEY, store, sizeof(nvs_store_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(ret));
    else
        ESP_LOGI(TAG, "NVS saved OK");

    nvs_close(handle);
}

void espnow_nvs_invalidate_pairing(nvs_store_t *store)
{
    store->code1 = INVALID_CODE;
    memset(&store->pairing, 0, sizeof(store->pairing));
    espnow_nvs_save(store);
    ESP_LOGI(TAG, "Pairing invalidated");
}
