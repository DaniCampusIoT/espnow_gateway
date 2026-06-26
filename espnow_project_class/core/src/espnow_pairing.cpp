/**
 * @file espnow_pairing.cpp
 * @brief Implementación de la máquina de estados de pairing.
 */

#include "espnow_pairing.h"
#include "espnow_wifi.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "espnow_pairing";

/* Broadcast MAC estándar */
static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

/* ── Contexto interno ───────────────────────────────────────────────────── */
static nvs_store_t       *s_store      = NULL;
static SemaphoreHandle_t  s_send_ready = NULL;
static pair_state_t       s_state      = PAIR_STATE_REQUEST;
static uint8_t            s_channel    = 1;
static uint32_t           s_timeout_ms = 3000;
static int64_t            s_start_us   = 0;

/* Datos de pairing recibidos (escrito por callback ISR-like, leído por task) */
static volatile bool      s_pair_recv  = false;
static espnow_pairing_t   s_pair_pkt;

/* ── Helpers ────────────────────────────────────────────────────────────── */
static void register_peer(const uint8_t *mac, uint8_t channel)
{
    esp_now_peer_info_t peer = {};
    peer.channel = channel;
    peer.ifidx   = ESPNOW_WIFI_IF;
    peer.encrypt = false;
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    esp_now_add_peer(&peer);
}

static void switch_channel(uint8_t ch)
{
    esp_now_deinit();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;
    esp_wifi_init(&cfg);
    esp_wifi_start();
    esp_wifi_set_mode(ESPNOW_WIFI_MODE);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    esp_wifi_disconnect();
    esp_now_init();

    /* Re-registra callbacks (los registra el módulo send/recv) */
    ESP_LOGI(TAG, "Switched to channel %d", ch);
}

/* ── API pública ────────────────────────────────────────────────────────── */

void espnow_pairing_init(nvs_store_t *store,
                         SemaphoreHandle_t send_ready,
                         uint8_t start_chan,
                         uint32_t timeout_ms)
{
    s_store      = store;
    s_send_ready = send_ready;
    s_channel    = start_chan;
    s_timeout_ms = timeout_ms;
    s_start_us   = esp_timer_get_time();
    s_state      = PAIR_STATE_REQUEST;
    s_pair_recv  = false;
}

bool espnow_pairing_is_paired(void)
{
    return (s_state == PAIR_STATE_PAIRED);
}

void espnow_pairing_get_gateway_mac(uint8_t mac[6])
{
    memcpy(mac, s_store->pairing.macAddr, 6);
}

uint8_t espnow_pairing_get_channel(void)
{
    return s_store->pairing.channel;
}

void espnow_pairing_on_recv(const espnow_pairing_t *pkt)
{
    memcpy(&s_pair_pkt, pkt, sizeof(espnow_pairing_t));
    s_pair_recv = true;   /* Flag sin mutex: escrito desde callback, leído en task */
}

/* ── Tarea principal ────────────────────────────────────────────────────── */

void espnow_pairing_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Pairing task started");

    /* Si ya tenemos pairing guardado en NVS, úsalo directamente */
    if (s_store->code1 == MAGIC_CODE1) {
        ESP_LOGI(TAG, "Restoring pairing from NVS, channel=%d",
                 s_store->pairing.channel);

        esp_wifi_set_channel(s_store->pairing.channel, WIFI_SECOND_CHAN_NONE);
        esp_now_init();
        register_peer(s_store->pairing.macAddr, s_store->pairing.channel);

        s_state = PAIR_STATE_PAIRED;
        xSemaphoreGive(s_send_ready);
        ESP_LOGI(TAG, "Paired (from NVS) – task done");
        vTaskDelete(NULL);
        return;
    }

    /* Bucle de emparejamiento activo */
    while (1) {
        /* Timeout global de pairing */
        if (s_timeout_ms > 0) {
            int64_t elapsed_ms = (esp_timer_get_time() - s_start_us) / 1000;
            if (elapsed_ms > (int64_t)s_timeout_ms) {
                ESP_LOGW(TAG, "Pairing timeout – going to sleep");
                /* El módulo sleep lo gestiona app_main */
                xSemaphoreGive(s_send_ready);  /* desbloquea app_main para sleep */
                vTaskDelete(NULL);
                return;
            }
        }

        switch (s_state) {

        case PAIR_STATE_REQUEST: {
            ESP_LOGI(TAG, "Sending pairing request on ch %d", s_channel);
            switch_channel(s_channel);

            register_peer(BROADCAST_MAC, s_channel);

            espnow_pairing_t req = {};
            req.msgType = MSG_PAIRING;
            req.id      = NODE_DEVICE;
            esp_now_send(BROADCAST_MAC, (uint8_t *)&req, sizeof(req));
            s_state = PAIR_STATE_REQUESTED;
            break;
        }

        case PAIR_STATE_REQUESTED:
            if (s_pair_recv) {
                s_pair_recv = false;
                ESP_LOGI(TAG, "Pairing response received on ch %d", s_pair_pkt.channel);

                register_peer(s_pair_pkt.macAddr, s_pair_pkt.channel);

                /* Persistir en NVS */
                s_store->code1 = MAGIC_CODE1;
                memcpy(&s_store->pairing, &s_pair_pkt, sizeof(espnow_pairing_t));
                espnow_nvs_save(s_store);

                s_state = PAIR_STATE_PAIRED;
                xSemaphoreGive(s_send_ready);
                ESP_LOGI(TAG, "Paired – task done");
                vTaskDelete(NULL);
                return;
            }
            /* Sin respuesta: avanza al siguiente canal */
            s_channel = (s_channel >= 13) ? 1 : s_channel + 1;
            s_state = PAIR_STATE_REQUEST;
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case PAIR_STATE_PAIRED:
            /* No debería llegar aquí */
            vTaskDelete(NULL);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
