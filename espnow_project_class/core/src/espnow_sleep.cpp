/**
 * @file espnow_sleep.cpp
 * @brief Implementación del módulo de deep sleep.
 */

#include "espnow_sleep.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <sys/time.h>

static const char *TAG = "espnow_sleep";

/* Persistido en RTC RAM para sobrevivir al deep sleep */
typedef struct { struct timeval entry_time; } rtc_sleep_data_t;
static RTC_DATA_ATTR rtc_sleep_data_t s_rtc;

static uint32_t s_sleep_sec = 30;

void espnow_sleep_set_duration(uint32_t sleep_sec)
{
    s_sleep_sec = sleep_sec;
    esp_sleep_enable_timer_wakeup((uint64_t)sleep_sec * 1000000ULL);
    ESP_LOGI(TAG, "Sleep duration set to %" PRIu32 " s", sleep_sec);
}

void espnow_sleep_record_entry(void)
{
    gettimeofday(&s_rtc.entry_time, NULL);
}

int32_t espnow_sleep_elapsed_ms(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (int32_t)(
        (now.tv_sec  - s_rtc.entry_time.tv_sec)  * 1000 +
        (now.tv_usec - s_rtc.entry_time.tv_usec) / 1000
    );
}

void espnow_sleep_enter(bool enable)
{
    espnow_sleep_record_entry();
    if (enable) {
        ESP_LOGI(TAG, "Entering deep sleep for %" PRIu32 " s", s_sleep_sec);
        esp_deep_sleep_start();
    } else {
        ESP_LOGW(TAG, "Deep sleep disabled – skipping");
    }
}
