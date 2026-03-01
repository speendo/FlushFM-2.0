#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "debug.h"

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void printBoardInfo();
static void scanWiFiNetworks();

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);

    // Wait for Serial to be ready (USB CDC on ESP32-S3 needs a moment)
    const uint32_t start = millis();
    while (!Serial && (millis() - start) < SERIAL_TIMEOUT_MS) {
        delay(10);
    }

    PROD_LOG("Hello FlushFM");

    printBoardInfo();
    scanWiFiNetworks();
}

void loop() {
    // Nothing to do in the main loop for US-0000.
    // Future user stories will introduce FreeRTOS tasks that take over here.
    delay(1000);
}

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

static void printBoardInfo() {
    DEBUG_LOG("--- Board Info ---");

    PROD_LOG("Chip model   : %s rev%d", ESP.getChipModel(), ESP.getChipRevision());
    PROD_LOG("CPU freq     : %u MHz", ESP.getCpuFreqMHz());
    PROD_LOG("Flash size   : %u KB", ESP.getFlashChipSize() / 1024);
    PROD_LOG("Free heap    : %u B", ESP.getFreeHeap());

#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        PROD_LOG("PSRAM size   : %u KB", ESP.getPsramSize() / 1024);
        PROD_LOG("Free PSRAM   : %u B", ESP.getFreePsram());
    } else {
        ERROR_LOG("PSRAM not detected – check hardware configuration");
    }
#else
    DEBUG_LOG("BOARD_HAS_PSRAM not set – PSRAM check skipped");
#endif
}

static void scanWiFiNetworks() {
    DEBUG_LOG("--- WiFi Scan ---");

    WiFi.mode(WIFI_STA);
    PROD_LOG("MAC address  : %s", WiFi.macAddress().c_str());

    PROD_LOG("Scanning for WiFi networks...");
    const int networkCount = WiFi.scanNetworks();

    if (networkCount < 0) {
        ERROR_LOG("WiFi scan failed (error %d)", networkCount);
        return;
    }

    PROD_LOG("Found %d network(s):", networkCount);
    for (int i = 0; i < networkCount; ++i) {
        PROD_LOG("  [%2d] %-32s  RSSI: %4d dBm  CH: %2d  %s",
                 i + 1,
                 WiFi.SSID(i).c_str(),
                 WiFi.RSSI(i),
                 WiFi.channel(i),
                 WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SECURED");
    }

    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
}
