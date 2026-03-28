// wifi_manager.cpp – WiFi connection and event handling
#include "wifi_manager.h"

#include <WiFi.h>

#include "config.h"
#include "debug.h"

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------
static char s_ssid[64] = {};
static char s_pass[64] = {};
static bool s_connected = false;
static wifi_manager::ConnectedCallback s_connectedCallback = nullptr;
static void* s_connectedCallbackContext = nullptr;

// ---------------------------------------------------------------------------
// WiFi event handlers
// ---------------------------------------------------------------------------
static void onWiFiDisconnect(WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
    s_connected = false;
    PROD_LOG("WiFi disconnected (reason %d) – attempting reconnect",
             info.wifi_sta_disconnected.reason);
    WiFi.reconnect();
}

static void onWiFiConnect(WiFiEvent_t /*event*/, WiFiEventInfo_t /*info*/) {
    s_connected = true;
    PROD_LOG("WiFi reconnected – IP: %s", WiFi.localIP().toString().c_str());
    if (s_connectedCallback) {
        s_connectedCallback(s_connectedCallbackContext);
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
namespace wifi_manager {

void init() {
    WiFi.onEvent(onWiFiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(onWiFiConnect,    ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

void setConnectedCallback(ConnectedCallback callback, void* context) {
    s_connectedCallback = callback;
    s_connectedCallbackContext = context;
}

void setSsid(const char* ssid) {
    if (ssid) strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
}

void setPass(const char* pass) {
    if (pass) strncpy(s_pass, pass, sizeof(s_pass) - 1);
}

void connect() {
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
        s_connected = true;
        PROD_LOG("WiFi connected – IP: %s  RSSI: %d dBm",
                 WiFi.localIP().toString().c_str(), WiFi.RSSI());
        if (s_connectedCallback) {
            s_connectedCallback(s_connectedCallbackContext);
        }
    } else {
        ERROR_LOG("WiFi connection timed out");
    }
}

bool isConnected() {
    return s_connected;
}

} // namespace wifi_manager
