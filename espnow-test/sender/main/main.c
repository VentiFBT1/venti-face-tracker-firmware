// ESP-NOW link test - SENDER. Flash on a face tracker (ESP32-S3 N4R2).
// Sends dummy 10 KB frames at 60 fps to the test dongle and prints link stats once per second
// on the tracker's USB serial port. No camera, no Wi-Fi router. Flash the normal tracker
// firmware again afterwards.

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "espnow_test_protocol.h"

static const char *TAG = "espnow-sender";

// Face tracker IR LED MOSFET gate (Q1). Held low so the IR LEDs stay off during the test.
#define TRACKER_IR_GPIO GPIO_NUM_1

static SemaphoreHandle_t s_send_done;
static volatile bool s_send_ok;
static volatile bool s_found = false;
static uint8_t s_dongle_mac[ESP_NOW_ETH_ALEN];
static volatile uint8_t s_found_channel = 0;

// Stats for the current one-second window
static uint32_t s_frames_sent, s_packets_ok, s_packets_failed, s_frames_failed;
static int64_t s_frame_send_us_total, s_frame_send_us_max;
static uint8_t s_rate_index;

static void on_sent(const uint8_t *mac, esp_now_send_status_t status)
{
    (void)mac;
    s_send_ok = (status == ESP_NOW_SEND_SUCCESS);
    xSemaphoreGive(s_send_done);
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (s_found || len < (int)sizeof(espnow_test_beacon_t))
        return;
    const espnow_test_beacon_t *b = (const espnow_test_beacon_t *)data;
    if (b->hdr.magic != ESPNOW_TEST_MAGIC || b->hdr.type != ESPNOW_TEST_MSG_BEACON)
        return;
    memcpy(s_dongle_mac, info->src_addr, ESP_NOW_ETH_ALEN);
    s_found_channel = b->channel;
    s_found = true;
}

static void set_rate(uint8_t index)
{
    esp_now_rate_config_t cfg = {
        .phymode = WIFI_PHY_MODE_HT20,
        .rate = espnow_test_rates[index].rate,
        .ersu = false,
        .dcm = false,
    };
    esp_err_t err = esp_now_set_peer_rate_config(s_dongle_mac, &cfg);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Setting rate %s failed: %s", espnow_test_rates[index].name, esp_err_to_name(err));
    s_rate_index = index;
    printf("\n=== Data rate now %s ===\n", espnow_test_rates[index].name);
}

static void find_dongle(void)
{
    printf("Searching for the test dongle on channels 1-%d...\n", ESPNOW_TEST_MAX_CHANNEL);
    uint8_t ch = 1;
    while (!s_found)
    {
        ESP_ERROR_CHECK(esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE));
        vTaskDelay(pdMS_TO_TICKS(150));  // dongle beacons every 50 ms
        ch = ch >= ESPNOW_TEST_MAX_CHANNEL ? 1 : ch + 1;
    }
    ESP_ERROR_CHECK(esp_wifi_set_channel(s_found_channel, WIFI_SECOND_CHAN_NONE));

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_dongle_mac, ESP_NOW_ETH_ALEN);
    peer.channel = s_found_channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(s_dongle_mac))
        ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    printf("Found dongle " MACSTR " on channel %u\n", MAC2STR(s_dongle_mac), s_found_channel);
}

static bool send_packet(const uint8_t *buf, size_t len)
{
    for (int attempt = 0; attempt < 3; attempt++)
    {
        esp_err_t err = esp_now_send(s_dongle_mac, buf, len);
        if (err == ESP_ERR_ESPNOW_NO_MEM)
        {
            vTaskDelay(1);
            continue;
        }
        if (err != ESP_OK)
            return false;
        if (xSemaphoreTake(s_send_done, pdMS_TO_TICKS(20)) != pdTRUE)
            return false;
        return s_send_ok;
    }
    return false;
}

static void stats_task(void *arg)
{
    (void)arg;
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t frames = s_frames_sent;
        printf("[%s] ch %u | frames sent %" PRIu32 ", frames with a lost packet %" PRIu32
               " | packets ok %" PRIu32 ", lost %" PRIu32 " | time to send one frame avg %.1f ms, max %.1f ms\n",
               espnow_test_rate_name(s_rate_index), s_found_channel, frames, s_frames_failed, s_packets_ok, s_packets_failed,
               frames ? (double)s_frame_send_us_total / frames / 1000.0 : 0.0, (double)s_frame_send_us_max / 1000.0);
        s_frames_sent = s_frames_failed = s_packets_ok = s_packets_failed = 0;
        s_frame_send_us_total = s_frame_send_us_max = 0;
    }
}

void app_main(void)
{
    gpio_reset_pin(TRACKER_IR_GPIO);
    gpio_set_direction(TRACKER_IR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(TRACKER_IR_GPIO, 0);

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

    s_send_done = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    printf("\nVenti ESP-NOW link test: SENDER (face tracker)\n");
    printf("Frame %d bytes, %d fps, packets of %d bytes\n", ESPNOW_TEST_FRAME_BYTES, ESPNOW_TEST_FPS, ESPNOW_TEST_PACKET_BYTES);

    find_dongle();
    set_rate(0);
    xTaskCreate(stats_task, "stats", 4096, NULL, 2, NULL);

    static uint8_t packet[ESPNOW_TEST_PACKET_BYTES];
    espnow_test_chunk_hdr_t *hdr = (espnow_test_chunk_hdr_t *)packet;
    const uint8_t chunk_count = (ESPNOW_TEST_FRAME_BYTES + ESPNOW_TEST_CHUNK_PAYLOAD - 1) / ESPNOW_TEST_CHUNK_PAYLOAD;
    for (size_t i = sizeof(*hdr); i < sizeof(packet); i++)
        packet[i] = (uint8_t)i;  // dummy image data

    const int64_t frame_interval_us = 1000000 / ESPNOW_TEST_FPS;
    int64_t next_frame_us = esp_timer_get_time();
    int64_t rate_started_us = next_frame_us;
    uint32_t frame_id = 0;

    while (true)
    {
        // Pace frames like a camera: one every 1/60 s
        int64_t now = esp_timer_get_time();
        if (now < next_frame_us)
        {
            int64_t wait_ms = (next_frame_us - now) / 1000;
            if (wait_ms > 0)
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
            while (esp_timer_get_time() < next_frame_us)
            {
            }
        }
        next_frame_us += frame_interval_us;
        if (esp_timer_get_time() - next_frame_us > frame_interval_us)
            next_frame_us = esp_timer_get_time();  // fell far behind; don't burst

        if (esp_timer_get_time() - rate_started_us > (int64_t)ESPNOW_TEST_SECONDS_PER_RATE * 1000000)
        {
            set_rate((s_rate_index + 1) % ESPNOW_TEST_RATE_COUNT);
            rate_started_us = esp_timer_get_time();
        }

        const int64_t frame_start = esp_timer_get_time();
        bool frame_ok = true;
        for (uint8_t c = 0; c < chunk_count; c++)
        {
            size_t offset = (size_t)c * ESPNOW_TEST_CHUNK_PAYLOAD;
            size_t bytes = ESPNOW_TEST_FRAME_BYTES - offset;
            if (bytes > ESPNOW_TEST_CHUNK_PAYLOAD)
                bytes = ESPNOW_TEST_CHUNK_PAYLOAD;
            hdr->hdr.magic = ESPNOW_TEST_MAGIC;
            hdr->hdr.version = ESPNOW_TEST_VERSION;
            hdr->hdr.type = ESPNOW_TEST_MSG_CHUNK;
            hdr->rate_index = s_rate_index;
            hdr->chunk_index = c;
            hdr->chunk_count = chunk_count;
            hdr->reserved = 0;
            hdr->frame_id = frame_id;
            hdr->frame_bytes = ESPNOW_TEST_FRAME_BYTES;
            hdr->chunk_bytes = (uint16_t)bytes;
            if (send_packet(packet, sizeof(*hdr) + bytes))
                s_packets_ok++;
            else
            {
                s_packets_failed++;
                frame_ok = false;
            }
        }
        const int64_t took = esp_timer_get_time() - frame_start;
        s_frame_send_us_total += took;
        if (took > s_frame_send_us_max)
            s_frame_send_us_max = took;
        s_frames_sent++;
        if (!frame_ok)
            s_frames_failed++;
        frame_id++;
    }
}
