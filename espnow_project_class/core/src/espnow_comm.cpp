/**
 * @file espnow_comm.cpp
 * @brief Implementación de envío y recepción ESP-NOW con colas FreeRTOS.
 *
 * Arquitectura de colas:
 *
 *   send_task  ←─── q_send_requests  ←── espnow_send() (app)
 *       │
 *       └── esp_now_send() ─→ [ISR send_cb] ─→ q_send_results
 *       └── xQueueReceive(q_send_results) ──────────────────────
 *
 *   [ISR recv_cb] ──→ q_recv_frames ──→ recv_task ──→ user callbacks
 */

#include "espnow_comm.h"
#include "espnow_pairing.h"
#include "esp_now.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>

using namespace std;

static const char *TAG = "espnow_comm";

/* ── Contexto interno ───────────────────────────────────────────────────── */
typedef struct {
    char    topic[128];
    char    payload[240];
    bool    request_check;
} send_request_t;

static nvs_store_t      *s_store       = NULL;
static SemaphoreHandle_t s_send_sem    = NULL;
static uint32_t          s_pan_addr    = 1;
static espnow_mqtt_cb_t  s_mqtt_cb     = NULL;
static espnow_pan_cb_t   s_pan_cb      = NULL;

static QueueHandle_t s_q_send_req  = NULL;  /**< App → send_task: solicitudes */
static QueueHandle_t s_q_send_res  = NULL;  /**< send_cb → send_task: ACKs */
static QueueHandle_t s_q_recv      = NULL;  /**< recv_cb → recv_task: tramas */

/* ── Callbacks ESP-NOW (contexto ISR/WiFi task) ─────────────────────────── */

static void on_send_cb(const uint8_t *mac, esp_now_send_status_t status)
{
    espnow_send_cb_t result;
    memcpy(result.mac_addr, mac, ESP_NOW_ETH_ALEN);
    result.status = status;
    xQueueSendFromISR(s_q_send_res, &result, NULL);
}

static void on_recv_cb(const esp_now_recv_info_t *info,
                       const uint8_t *data, int len)
{
    if (!data || len <= 0) return;
    /* Copiar trama completa a la cola; recv_task la procesará */
    espnow_frame_t frame;
    memcpy(&frame, data, (len <= (int)sizeof(frame)) ? len : sizeof(frame));
    xQueueSendFromISR(s_q_recv, &frame, NULL);
}

/* ── API pública ────────────────────────────────────────────────────────── */

void espnow_comm_init(nvs_store_t       *store,
                      SemaphoreHandle_t  send_sem,
                      uint32_t           pan_address,
                      espnow_mqtt_cb_t   mqtt_cb,
                      espnow_pan_cb_t    pan_cb)
{
    s_store    = store;
    s_send_sem = send_sem;
    s_pan_addr = pan_address;
    s_mqtt_cb  = mqtt_cb;
    s_pan_cb   = pan_cb;

    s_q_send_req = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(send_request_t));
    s_q_send_res = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_send_cb_t));
    s_q_recv     = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_frame_t));

    ESP_ERROR_CHECK(esp_now_register_send_cb(on_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv_cb));

    ESP_LOGI(TAG, "Comm module initialized (PAN=%" PRIu32 ")", pan_address);
}

uint8_t espnow_send(const char *topic, const char *payload, bool request_check)
{
    if (!espnow_pairing_is_paired()) return ERROR_NOT_PAIRED;

    send_request_t req = {};
    strncpy(req.topic,   topic,   sizeof(req.topic) - 1);
    strncpy(req.payload, payload, sizeof(req.payload) - 1);
    req.request_check = request_check;

    if (xQueueSend(s_q_send_req, &req, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "Send queue full");
        return ERROR_SIN_RESPUESTA;
    }
    return ENVIO_OK;
}

bool espnow_comm_send_available(uint32_t wait_ms)
{
    if (xSemaphoreTake(s_send_sem, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
        xSemaphoreGive(s_send_sem);
        return true;
    }
    return false;
}

/* ── send_task ──────────────────────────────────────────────────────────── */

void espnow_send_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "send_task started");

    send_request_t   req;
    espnow_send_cb_t ack;

    while (1) {
        /* Espera a que haya una solicitud de envío */
        if (xQueueReceive(s_q_send_req, &req, portMAX_DELAY) != pdTRUE) continue;

        /* Espera token de envío (garantiza un solo envío en curso) */
        if (xSemaphoreTake(s_send_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGE(TAG, "Timeout waiting send semaphore");
            continue;
        }

        uint8_t gw_mac[6];
        espnow_pairing_get_gateway_mac(gw_mac);
        uint8_t gw_ch = espnow_pairing_get_channel();

        /* Construir trama */
        string full_payload = string(req.topic) + "|" + string(req.payload);
        uint8_t msg_type = MSG_DATA | (uint8_t)((s_pan_addr << PAN_OFFSET) & MASK_PAN);
        if (req.request_check) msg_type |= FLAG_CHECK;

        const size_t MAX_SINGLE = sizeof(espnow_frame_t) - ESPNOW_CTRL_BYTE - ESP_NOW_ETH_ALEN - 1;

        if (full_payload.size() <= MAX_SINGLE) {
            /* ── Envío en un solo paquete ── */
            espnow_frame_t frame = {};
            frame.msgType = msg_type;
            memcpy(frame.mac_addr, gw_mac, ESP_NOW_ETH_ALEN);
            memcpy(frame.payload, full_payload.c_str(), full_payload.size() + 1);

            esp_now_send(gw_mac, (uint8_t *)&frame, sizeof(frame));

            if (xQueueReceive(s_q_send_res, &ack, pdMS_TO_TICKS(500)) != pdTRUE) {
                ESP_LOGE(TAG, "No ACK received");
            } else if (ack.status != 0) {
                ESP_LOGW(TAG, "Send delivery failed");
            }
        } else {
            /* ── Envío fragmentado ── */
            size_t chunk = MAX_SINGLE - 2;  /* -2 para índice de parte */
            size_t total = (full_payload.size() + chunk - 1) / chunk;
            for (size_t i = 0; i < total; i++) {
                espnow_frame_t frame = {};
                frame.msgType = (i == total - 1) ?
                    (msg_type | FLAG_CHECK) : MSG_DATA;
                memcpy(frame.mac_addr, gw_mac, ESP_NOW_ETH_ALEN);

                string part = to_string(total - i - 1) +
                              full_payload.substr(i * chunk, chunk);
                memcpy(frame.payload, part.c_str(), part.size() + 1);

                esp_now_send(gw_mac, (uint8_t *)&frame, sizeof(frame));
                xQueueReceive(s_q_send_res, &ack, pdMS_TO_TICKS(500));
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }

        (void)gw_ch;
        xSemaphoreGive(s_send_sem);
        ESP_LOGI(TAG, "Message sent: %s", req.topic);
    }
}

/* ── recv_task ──────────────────────────────────────────────────────────── */

void espnow_recv_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "recv_task started");

    espnow_frame_t frame;

    while (1) {
        if (xQueueReceive(s_q_recv, &frame, portMAX_DELAY) != pdTRUE) continue;

        uint8_t type = frame.msgType;

        switch (type & MASK_MSG_TYPE) {

        case MSG_NODATA:
            ESP_LOGI(TAG, "NODATA from gateway");
            xSemaphoreGive(s_send_sem);
            break;

        case MSG_DATA: {
            /* Formato: tipo | topic '|' payload */
            const char *raw = (const char *)frame.payload;
            const char *sep = strchr(raw, '|');
            if (!sep) break;

            espnow_rcv_msg_t *msg =
                (espnow_rcv_msg_t *)malloc(sizeof(espnow_rcv_msg_t));
            if (!msg) break;

            size_t topic_len = sep - raw;
            msg->topic   = (char *)malloc(topic_len + 1);
            msg->payload = (char *)malloc(strlen(sep + 1) + 1);
            memcpy(msg->topic, raw, topic_len);
            msg->topic[topic_len] = '\0';
            strcpy(msg->payload, sep + 1);

            xSemaphoreGive(s_send_sem);
            if (s_mqtt_cb) s_mqtt_cb(msg);
            /* Nota: el callback es responsable de free(msg->topic),
               free(msg->payload) y free(msg) */
            break;
        }

        case MSG_PAN_DATA: {
            espnow_rcv_msg_t *msg =
                (espnow_rcv_msg_t *)malloc(sizeof(espnow_rcv_msg_t));
            if (!msg) break;

            size_t plen = strlen((char *)frame.payload + PAN_payload_offset);
            msg->topic   = NULL;
            msg->payload = (char *)malloc(plen + 1);
            memcpy(msg->payload, frame.payload + PAN_payload_offset, plen + 1);
            memcpy(msg->macAddr, frame.payload + PAN_MAC_offset, PAN_MAC_size);
            memcpy(&msg->ms_old, frame.payload + PAN_MSold_offset, PAN_MSold_size);

            xSemaphoreGive(s_send_sem);
            if (s_pan_cb) s_pan_cb(msg);
            break;
        }

        case MSG_PAIRING:
            /* Respuesta de pairing: delegar al módulo pairing */
            espnow_pairing_on_recv((const espnow_pairing_t *)&frame);
            break;

        default:
            ESP_LOGW(TAG, "Unknown message type: 0x%02X", type);
            break;
        }
    }
}
