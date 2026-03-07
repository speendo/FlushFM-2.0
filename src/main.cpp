#include <Arduino.h>
#include <WiFi.h>
#include <Audio.h>

#include "config.h"
#include "debug.h"

// ---------------------------------------------------------------------------
// Audio instance
// ---------------------------------------------------------------------------
static Audio s_audio;

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------
static char s_ssid[64] = {};
static char s_pass[64] = {};
static bool s_wifi_connected = false;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void printBoardInfo();
static void printHelp();
static void connectWiFi();
static bool readLine(char* buf, size_t maxLen);
static void processCommand(const char* line);
static void onWiFiDisconnect(WiFiEvent_t event, WiFiEventInfo_t info);
static void onWiFiConnect(WiFiEvent_t event, WiFiEventInfo_t info);

// Audio library callbacks – must be free functions (required by ESP32-audioI2S)
void audio_info(const char* info);
void audio_showstation(const char* info);
void audio_showstreamtitle(const char* info);
void audio_bitrate(const char* info);

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

    PROD_LOG("Hello FlushFM – US-0001 Audio Streaming Test");
    printBoardInfo();

    // Register WiFi event handlers for graceful reconnection
    WiFi.onEvent(onWiFiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(onWiFiConnect,    ARDUINO_EVENT_WIFI_STA_GOT_IP);

    // Initialize audio library
    // I2S pins are placeholder values – will be finalized when PCM5102A
    // hardware is connected (see docs/pinout.md)
    s_audio.setPinout(I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
    s_audio.setVolume(0); // No audio hardware connected for this test
    PROD_LOG("ESP32-audioI2S initialized (BCK=%d WS=%d DOUT=%d)",
             I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);

    printHelp();
}

void loop() {
    s_audio.loop();

    static char cmdBuf[SERIAL_CMD_BUF_SIZE];
    if (readLine(cmdBuf, sizeof(cmdBuf))) {
        processCommand(cmdBuf);
    }
}

// ---------------------------------------------------------------------------
// Serial command interface
// ---------------------------------------------------------------------------

static bool readLine(char* buf, size_t maxLen) {
    static size_t pos = 0;
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (pos > 0) {
                buf[pos] = '\0';
                pos = 0;
                return true;
            }
        } else if (pos < maxLen - 1) {
            buf[pos++] = c;
        }
    }
    return false;
}

static void processCommand(const char* line) {
    // Split into command word and optional argument
    char cmd[32] = {};
    const char* arg = nullptr;
    const char* space = strchr(line, ' ');
    if (space) {
        const size_t cmdLen = static_cast<size_t>(space - line);
        memcpy(cmd, line, cmdLen < sizeof(cmd) - 1 ? cmdLen : sizeof(cmd) - 1);
        arg = space + 1;
    } else {
        strncpy(cmd, line, sizeof(cmd) - 1);
    }

    if (strcmp(cmd, "ssid") == 0) {
        if (!arg || *arg == '\0') { ERROR_LOG("Usage: ssid <name>"); return; }
        strncpy(s_ssid, arg, sizeof(s_ssid) - 1);
        PROD_LOG("SSID set to: %s", s_ssid);

    } else if (strcmp(cmd, "pass") == 0) {
        if (!arg || *arg == '\0') { ERROR_LOG("Usage: pass <password>"); return; }
        strncpy(s_pass, arg, sizeof(s_pass) - 1);
        PROD_LOG("Password set");

    } else if (strcmp(cmd, "connect") == 0) {
        connectWiFi();

    } else if (strcmp(cmd, "play") == 0) {
        if (!arg || *arg == '\0') { ERROR_LOG("Usage: play <url>"); return; }
        if (!s_wifi_connected) { ERROR_LOG("Not connected to WiFi – run 'connect' first"); return; }
        PROD_LOG("Connecting to stream: %s", arg);
        s_audio.connecttohost(arg);

    } else if (strcmp(cmd, "stop") == 0) {
        s_audio.stopSong();
        PROD_LOG("Stream stopped");

    } else if (strcmp(cmd, "switch") == 0) {
        if (!arg || *arg == '\0') { ERROR_LOG("Usage: switch <url>"); return; }
        if (!s_wifi_connected) { ERROR_LOG("Not connected to WiFi – run 'connect' first"); return; }
        s_audio.stopSong();
        PROD_LOG("Switching to stream: %s", arg);
        s_audio.connecttohost(arg);

    } else if (strcmp(cmd, "help") == 0) {
        printHelp();

    } else {
        ERROR_LOG("Unknown command '%s' – type 'help' for available commands", cmd);
    }
}

static void printHelp() {
    Serial.println();
    Serial.println("FlushFM – US-0001 Serial Commands:");
    Serial.println("  ssid <name>      Set WiFi SSID");
    Serial.println("  pass <password>  Set WiFi password");
    Serial.println("  connect          Connect to WiFi with stored credentials");
    Serial.println("  play <url>       Start streaming from URL");
    Serial.println("  stop             Stop current stream");
    Serial.println("  switch <url>     Switch to a different stream URL");
    Serial.println("  help             Show this help");
    Serial.println();
    Serial.println("Test stations:");
    Serial.println("  http://ice1.somafm.com/groovesalad-256-mp3");
    Serial.println("  http://stream.srg-ssr.ch/m/drs3/mp3_128");
    Serial.println();
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

static void connectWiFi() {
    if (s_ssid[0] == '\0') {
        ERROR_LOG("No SSID set – use 'ssid <name>' first");
        return;
    }
    PROD_LOG("Connecting to WiFi: %s ...", s_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(s_ssid, s_pass[0] != '\0' ? s_pass : nullptr);

    const uint32_t timeout = millis() + WIFI_CONNECT_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        s_wifi_connected = true;
        PROD_LOG("WiFi connected – IP: %s  RSSI: %d dBm",
                 WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        ERROR_LOG("WiFi connection timed out");
    }
}

static void onWiFiDisconnect(WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
    s_wifi_connected = false;
    PROD_LOG("WiFi disconnected (reason %d) – attempting reconnect",
             info.wifi_sta_disconnected.reason);
    WiFi.reconnect();
}

static void onWiFiConnect(WiFiEvent_t /*event*/, WiFiEventInfo_t /*info*/) {
    s_wifi_connected = true;
    PROD_LOG("WiFi reconnected – IP: %s", WiFi.localIP().toString().c_str());
}

// ---------------------------------------------------------------------------
// Board info
// ---------------------------------------------------------------------------

static void printBoardInfo() {
    DEBUG_LOG("--- Board Info ---");
    PROD_LOG("Chip model   : %s rev%d", ESP.getChipModel(), ESP.getChipRevision());
    PROD_LOG("CPU freq     : %u MHz", ESP.getCpuFreqMHz());
    PROD_LOG("Flash size   : %u KB", ESP.getFlashChipSize() / 1024);
    PROD_LOG("Free heap    : %u B",  ESP.getFreeHeap());

#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        PROD_LOG("PSRAM size   : %u KB", ESP.getPsramSize() / 1024);
        PROD_LOG("Free PSRAM   : %u B",  ESP.getFreePsram());
    } else {
        ERROR_LOG("PSRAM not detected – check hardware configuration");
    }
#else
    DEBUG_LOG("BOARD_HAS_PSRAM not set – PSRAM check skipped");
#endif
}

// ---------------------------------------------------------------------------
// ESP32-audioI2S callbacks
// ---------------------------------------------------------------------------

void audio_info(const char* info) {
    PROD_LOG("[Audio] %s", info);
}

void audio_showstation(const char* info) {
    PROD_LOG("[Station] %s", info);
}

void audio_showstreamtitle(const char* info) {
    PROD_LOG("[Track]   %s", info);
}

void audio_bitrate(const char* info) {
    PROD_LOG("[Bitrate] %s", info);
}

