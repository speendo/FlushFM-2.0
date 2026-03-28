#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// FlushFM 2.0 – Logging macros
//
// Controlled by the DEBUG_ENABLED build flag (set in platformio.ini).
// When DEBUG_ENABLED is not set all DEBUG_* macros expand to nothing,
// which means zero flash and zero runtime cost in production builds.
//
// Usage:
//   PROD_LOG("WiFi connected")             – always printed
//   ERROR_LOG("Init failed: %d", code)     – always printed
//   DEBUG_LOG("raw ADC: %d", value)        – debug builds only
//   DEBUG_COMPONENT("Sensor", "reading: %d", value)
// ---------------------------------------------------------------------------

#ifdef DEBUG_ENABLED
  #define DEBUG_LOG(msg, ...) \
      Serial.printf("[%lu][DEBUG] " msg "\n", millis(), ##__VA_ARGS__)
  #define DEBUG_COMPONENT(component, msg, ...) \
      Serial.printf("[%lu][DEBUG][%s] " msg "\n", millis(), component, ##__VA_ARGS__)
#else
  #define DEBUG_LOG(msg, ...)
  #define DEBUG_COMPONENT(component, msg, ...)
#endif

#define PROD_LOG(msg, ...) \
    Serial.printf("[%lu][INFO] " msg "\n", millis(), ##__VA_ARGS__)
#define ERROR_LOG(msg, ...) \
    Serial.printf("[%lu][ERROR] " msg "\n", millis(), ##__VA_ARGS__)
