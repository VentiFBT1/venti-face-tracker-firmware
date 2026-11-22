// Shared definitions for the ESP-NOW link test (sender on the face tracker, receiver on the dongle).
// This is a standalone test. It does not touch the face tracker firmware in overlay/.
#pragma once

#include <stdint.h>
#include "esp_wifi_types.h"

// Wi-Fi channel the dongle (receiver) listens on. The sender finds it automatically.
// US devices: use 1-11. Pick a channel away from your router's 2.4 GHz channel if you know it.
#define ESPNOW_TEST_CHANNEL 11
#define ESPNOW_TEST_MAX_CHANNEL 11

// Dummy "camera" frames: size and rate similar to the face tracker's 240x240 MJPEG at 60 fps.
#define ESPNOW_TEST_FRAME_BYTES 10000
#define ESPNOW_TEST_FPS 60

// The sender steps through these data rates, ESPNOW_TEST_SECONDS_PER_RATE seconds each, then repeats.
#define ESPNOW_TEST_SECONDS_PER_RATE 20

#define ESPNOW_TEST_MAGIC 0x5654  // "VT"
#define ESPNOW_TEST_VERSION 1

enum {
    ESPNOW_TEST_MSG_BEACON = 1,  // dongle -> broadcast, "I'm here on this channel"
    ESPNOW_TEST_MSG_CHUNK = 2,   // tracker -> dongle, one piece of a frame
};

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
} espnow_test_hdr_t;

typedef struct __attribute__((packed)) {
    espnow_test_hdr_t hdr;
    uint8_t channel;
} espnow_test_beacon_t;

typedef struct __attribute__((packed)) {
    espnow_test_hdr_t hdr;
    uint8_t rate_index;    // index into espnow_test_rates[]
    uint8_t chunk_index;
    uint8_t chunk_count;
    uint8_t reserved;
    uint32_t frame_id;
    uint16_t frame_bytes;
    uint16_t chunk_bytes;
} espnow_test_chunk_hdr_t;

// 1470 bytes is the ESP-NOW v2 maximum payload (ESP-IDF 5.4+).
#define ESPNOW_TEST_PACKET_BYTES 1470
#define ESPNOW_TEST_CHUNK_PAYLOAD (ESPNOW_TEST_PACKET_BYTES - sizeof(espnow_test_chunk_hdr_t))

typedef struct {
    wifi_phy_rate_t rate;
    const char *name;
} espnow_test_rate_t;

// 802.11n HT20 rates, long guard interval. Raw link speed in brackets.
static const espnow_test_rate_t espnow_test_rates[] = {
    {WIFI_PHY_RATE_MCS1_LGI, "MCS1 (13 Mbps)"},
    {WIFI_PHY_RATE_MCS3_LGI, "MCS3 (26 Mbps)"},
    {WIFI_PHY_RATE_MCS5_LGI, "MCS5 (52 Mbps)"},
    {WIFI_PHY_RATE_MCS7_LGI, "MCS7 (65 Mbps)"},
};
#define ESPNOW_TEST_RATE_COUNT (sizeof(espnow_test_rates) / sizeof(espnow_test_rates[0]))

static inline const char *espnow_test_rate_name(uint8_t index)
{
    return index < ESPNOW_TEST_RATE_COUNT ? espnow_test_rates[index].name : "unknown";
}
