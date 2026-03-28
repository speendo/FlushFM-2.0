// wifi_manager.h – WiFi connection and event handling
// Owns credentials and connection state; call init() once in setup().
#pragma once

namespace wifi_manager {

using ConnectedCallback = void (*)(void* context);

// Register WiFi event handlers. Must be called once in setup() before connect().
void init();

// Register a callback invoked on actual WiFi readiness (GOT_IP event).
void setConnectedCallback(ConnectedCallback callback, void* context);

// Store credentials; both are optional (pass nullptr or "" to skip).
void setSsid(const char* ssid);
void setPass(const char* pass);

// Start connection attempt; blocks up to WIFI_CONNECT_TIMEOUT_MS.
void connect();

bool isConnected();

} // namespace wifi_manager
