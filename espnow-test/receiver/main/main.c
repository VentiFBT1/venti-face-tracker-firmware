// ESP-NOW link test - RECEIVER. Flash on a spare Venti dongle (ESP32-S3FN8).
// Announces itself on ESPNOW_TEST_CHANNEL, receives the dummy frames from the test tracker and
// prints once per second on the dongle's USB serial port how many frames arrived complete,
// how many were broken or missing, and how even the frame timing was (stutter).
// Does not touch the SlimeVR dongle firmware; flash that again to use the dongle for SlimeVR.

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "espnow_test_protocol.h"

// Venti dongle status LED: GPIO1, active high
#define DONGLE_LED_GPIO GPIO_NUM_1

#define EXPECTED_GAP_US (1000000 / ESPNOW_TEST_FPS)
#define STUTTER_GAP_US (EXPECTED_GAP_US * 3 / 2)

static const uint8_t BROADCAST[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// Current frame being assembled
static bool s_have_frame;
static uint32_t s_cur_id;
static uint32_t s_cur_mask;
static uint8_t s_cur_count;
static bool s_cur_done;
static int64_t s_cur_first_us;
static int64_t s_last_complete_us;

// One-second window stats
static uint32_t s_ok, s_broken, s_missing, s_stutters;
static int64_t s_gap_total, s_gap_max, s_assembly_total;
static uint32_t s_gap_count;
static int32_t s_rssi_total;
static uint32_t s_packets;
static uint8_t s_rate_index = 0xff;
static uint8_t s_sender_mac[ESP_NOW_ETH_ALEN];
static bool s_have_sender;

static void finish_current_locked(void)
{
    if (s_have_frame && !s_cur_done)
        s_broken++;
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(espnow_test_chunk_hdr_t))
        return;
    const espnow_test_chunk_hdr_t *h = (const espnow_test_chunk_hdr_t *)data;
    if (h->hdr.magic != ESPNOW_TEST_MAGIC || h->hdr.type != ESPNOW_TEST_MSG_CHUNK || h->chunk_count == 0 || h->chunk_count > 32 ||
        h->chunk_index >= h->chunk_count)
        return;

    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    s_packets++;
    s_rssi_total += info->rx_ctrl->rssi;
    s_rate_index = h->rate_index;
    if (!s_have_sender)
    {
        memcpy(s_sender_mac, info->src_addr, ESP_NOW_ETH_ALEN);
        s_have_sender = true;
    }

    if (!s_have_frame || h->frame_id != s_cur_id)
    {
        if (s_have_frame && h->frame_id < s_cur_id)
        {
            if (s_cur_id - h->frame_id < 100)
            {
                portEXIT_CRITICAL(&s_lock);  // late duplicate of an old frame
                return;
            }
            s_have_frame = false;  // tracker restarted and its frame counter began again
            s_last_complete_us = 0;
        }
        finish_current_locked();
        if (s_have_frame && h->frame_id > s_cur_id + 1)
            s_missing += h->frame_id - s_cur_id - 1;  // frames where not a single packet arrived
        s_have_frame = true;
        s_cur_id = h->frame_id;
        s_cur_mask = 0;
        s_cur_count = h->chunk_count;
        s_cur_done = false;
        s_cur_first_us = now;
    }

    if (!s_cur_done)
    {
        s_cur_mask |= (1u << h->chunk_index);
        const uint32_t full = (s_cur_count >= 32) ? 0xffffffffu : ((1u << s_cur_count) - 1u);
        if (s_cur_mask == full)
        {
            s_cur_done = true;
            s_ok++;
            s_assembly_total += now - s_cur_first_us;
            if (s_last_complete_us)
            {
                const int64_t gap = now - s_last_complete_us;
                s_gap_total += gap;
                s_gap_count++;
                if (gap > s_gap_max)
                    s_gap_max = gap;
                if (gap > STUTTER_GAP_US)
                    s_stutters++;
            }
            s_last_complete_us = now;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

static void beacon_task(void *arg)
{
    (void)arg;
    espnow_test_beacon_t b = {
        .hdr = {.magic = ESPNOW_TEST_MAGIC, .version = ESPNOW_TEST_VERSION, .type = ESPNOW_TEST_MSG_BEACON},
        .channel = ESPNOW_TEST_CHANNEL,
    };
    while (true)
    {
        esp_now_send(BROADCAST, (const uint8_t *)&b, sizeof(b));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void stats_task(void *arg)
{
    (void)arg;
    bool led = false;
    uint32_t total_ok = 0, total_bad = 0, total_stutters = 0;
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        portENTER_CRITICAL(&s_lock);
        uint32_t ok = s_ok, broken = s_broken, missing = s_missing, stutters = s_stutters, packets = s_packets, gaps = s_gap_count;
        int64_t gap_total = s_gap_total, gap_max = s_gap_max, assembly = s_assembly_total;
        int32_t rssi_total = s_rssi_total;
        uint8_t rate = s_rate_index;
        bool have_sender = s_have_sender;
        s_ok = s_broken = s_missing = s_stutters = s_packets = s_gap_count = 0;
        s_gap_total = s_gap_max = s_assembly_total = 0;
        s_rssi_total = 0;
        portEXIT_CRITICAL(&s_lock);

        if (!have_sender)
        {
            printf("Waiting for the test tracker on channel %d...\n", ESPNOW_TEST_CHANNEL);
            led = !led;
            gpio_set_level(DONGLE_LED_GPIO, led);
            continue;
        }
        gpio_set_level(DONGLE_LED_GPIO, ok > 0);
        total_ok += ok;
        total_bad += broken + missing;
        total_stutters += stutters;
        printf("[%s] ch %d signal %" PRId32 " dBm | frames complete %" PRIu32 "/%d, broken %" PRIu32 ", missing %" PRIu32
               " | frame gap avg %.1f ms, max %.1f ms, stutters %" PRIu32 " | frame arrives over %.1f ms"
               " | totals ok %" PRIu32 " bad %" PRIu32 " stutters %" PRIu32 "\n",
               espnow_test_rate_name(rate), ESPNOW_TEST_CHANNEL, packets ? rssi_total / (int32_t)packets : 0, ok, ESPNOW_TEST_FPS,
               broken, missing, gaps ? (double)gap_total / gaps / 1000.0 : 0.0, (double)gap_max / 1000.0, stutters,
               ok ? (double)assembly / ok / 1000.0 : 0.0, total_ok, total_bad, total_stutters);
    }
}

void app_main(void)
{
    gpio_reset_pin(DONGLE_LED_GPIO);
    gpio_set_direction(DONGLE_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DONGLE_LED_GPIO, 0);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_TEST_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BROADCAST, ESP_NOW_ETH_ALEN);
    peer.channel = ESPNOW_TEST_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    uint8_t mac[ESP_NOW_ETH_ALEN];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    printf("\nVenti ESP-NOW link test: RECEIVER (dongle) " MACSTR " on channel %d\n", MAC2STR(mac), ESPNOW_TEST_CHANNEL);
    printf("Stutter = a gap between complete frames longer than %.1f ms\n", STUTTER_GAP_US / 1000.0);

    xTaskCreate(beacon_task, "beacon", 3072, NULL, 3, NULL);
    xTaskCreate(stats_task, "stats", 4096, NULL, 2, NULL);
}
