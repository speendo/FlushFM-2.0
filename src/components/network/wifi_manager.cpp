// wifi_manager.cpp – WiFi connection and event handling
#include "components/network/wifi_manager.h"

#include <WiFi.h>

#include "core/config.h"
#include "core/debug.h"

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------
static constexpr const char* kLogSource = "WiFiMgr";
static char s_ssid[64] = {};
static char s_pass[64] = {};
static bool s_connected = false;
static wifi_manager::WiFiState s_state = wifi_manager::WiFiState::DISCONNECTED;
static bool s_suppressReconnectOnce = false;
static wifi_manager::ConnectedCallback s_connectedCallback = nullptr;
static void* s_connectedCallbackContext = nullptr;
static wifi_manager::DisconnectedCallback s_disconnectedCallback = nullptr;
static void* s_disconnectedCallbackContext = nullptr;

// ---------------------------------------------------------------------------
// WiFi event handlers
// ---------------------------------------------------------------------------
static void onWiFiDisconnect(WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
    s_connected = false;
    s_state = wifi_manager::WiFiState::DISCONNECTED;

    if (s_suppressReconnectOnce) {
        s_suppressReconnectOnce = false;
        PROD_LOG(kLogSource, "WiFi disconnected by runtime reset");
        return;
    }

    PROD_LOG(kLogSource, "WiFi disconnected (reason %d) – attempting reconnect",
             info.wifi_sta_disconnected.reason);
    if (s_disconnectedCallback) {
        s_disconnectedCallback(s_disconnectedCallbackContext);
    }
    WiFi.reconnect();
}

static void onWiFiConnect(WiFiEvent_t /*event*/, WiFiEventInfo_t /*info*/) {
    s_connected = true;
    s_state = wifi_manager::WiFiState::CONNECTED;
    PROD_LOG(kLogSource, "WiFi reconnected – IP: %s", WiFi.localIP().toString().c_str());
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

void setDisconnectedCallback(DisconnectedCallback callback, void* context) {
    s_disconnectedCallback = callback;
    s_disconnectedCallbackContext = context;
}

void setSsid(const char* ssid) {
    if (ssid) strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
}

void setPass(const char* pass) {
    if (pass) strncpy(s_pass, pass, sizeof(s_pass) - 1);
}

void connect() {
    if (s_ssid[0] == '\0') {
        s_state = WiFiState::ERROR;
        ERROR_LOG(kLogSource, "No SSID set – use 'ssid <name>' first");
        return;
    }
    s_state = WiFiState::CONNECTING;
    PROD_LOG(kLogSource, "Connecting to WiFi: %s ...", s_ssid);
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
        s_state = WiFiState::CONNECTED;
        PROD_LOG(kLogSource, "WiFi connected – IP: %s  RSSI: %d dBm",
                 WiFi.localIP().toString().c_str(), WiFi.RSSI());
        if (s_connectedCallback) {
            s_connectedCallback(s_connectedCallbackContext);
        }
    } else {
        s_state = WiFiState::ERROR;
        ERROR_LOG(kLogSource, "WiFi connection timed out");
    }
}

void disconnect() {
    s_suppressReconnectOnce = (s_state == WiFiState::CONNECTED ||
                               s_state == WiFiState::CONNECTING ||
                               WiFi.status() == WL_CONNECTED);
    WiFi.disconnect(true, false);

    s_connected = false;
    s_state = WiFiState::DISCONNECTED;

    PROD_LOG(kLogSource, "WiFi runtime disconnect requested");
}

void resetSession() {
    s_suppressReconnectOnce = (s_state == WiFiState::CONNECTED ||
                               s_state == WiFiState::CONNECTING ||
                               WiFi.status() == WL_CONNECTED);
    WiFi.disconnect(true, false);

    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    s_connected = false;
    s_state = WiFiState::DISCONNECTED;

    PROD_LOG(kLogSource, "Runtime session reset: WiFi disconnected and volatile credentials cleared");
}

WiFiState state() {
    return s_state;
}

bool isConnected() {
    return s_state == WiFiState::CONNECTED || s_connected;
}

} // namespace wifi_manager
