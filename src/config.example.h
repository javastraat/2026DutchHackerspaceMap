#pragma once

// --- WiFi slots (6 slots, tried in order on boot) ---
#define WIFI_SLOT_COUNT  6

#define WIFI_S0_LABEL    "TechInc"
#define WIFI_S0_SSID     "your_ssid_here"
#define WIFI_S0_PASS     "your_password_here"

#define WIFI_S1_LABEL    "Home"
#define WIFI_S1_SSID     ""
#define WIFI_S1_PASS     ""

#define WIFI_S2_LABEL    "Mobile"
#define WIFI_S2_SSID     ""
#define WIFI_S2_PASS     ""

#define WIFI_S3_LABEL    "Work"
#define WIFI_S3_SSID     ""
#define WIFI_S3_PASS     ""

#define WIFI_S4_LABEL    "Friends"
#define WIFI_S4_SSID     ""
#define WIFI_S4_PASS     ""

#define WIFI_S5_LABEL    "Other"
#define WIFI_S5_SSID     ""
#define WIFI_S5_PASS     ""

// --- Fallback AP & OTA ---
#define FALLBACK_AP_SSID   "HackerspaceMap-OTA"
#define FALLBACK_AP_PASS   "your_password_here"
#define OTA_HOSTNAME       "hackerspace-status"
#define OTA_PASS           "your_password_here"
#define WIFI_CONNECT_TIMEOUT_MS  15000

// --- LED hardware ---
#define LED_PIN            10
#define LED_COUNT          38
#define MAP_LED_COUNT      18
#define BACKLIGHT_COUNT    (LED_COUNT - MAP_LED_COUNT)
#define LED_BITS           (LED_COUNT * 24 * 4)

// --- Polling & animation timing ---
#define POLL_INTERVAL_MS   (2 * 60 * 1000)   // how often to fetch SpaceAPI (ms)
#define ANIM_TICK_MS       30                 // animation update interval (ms)

// --- Animation modes ---
#define ANIM_MODE_SPARKLE    0    // map LEDs randomly twinkle white on top of state color
#define ANIM_MODE_BREATHE    1    // backlight slowly pulses in and out
#define ANIM_MODE_ORIGINAL   2    // Theo Borm: each space breathes at its own speed + rotating rainbow backlight

#define ANIM_MODE_DEFAULT    ANIM_MODE_ORIGINAL
#define SPARKLE_BRIGHTNESS   2    // 0 (off) to 10 (max) — white flash intensity

// --- MQTT defaults ---
#define MQTT_BROKER_DEFAULT   "mqtt_broker_address_here"
#define MQTT_PORT_DEFAULT     1883
#define MQTT_TOPIC_DEFAULT    "hackerspace/status"
#define MQTT_HA_ENABLE_DEFAULT true
