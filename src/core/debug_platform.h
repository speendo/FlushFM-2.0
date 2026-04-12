#pragma once

// Platform adapter for logging dependencies.
// On ESP32 it forwards to Arduino APIs.
// On native/host builds it provides a tiny shim for Serial, millis, and delay.
#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdarg>
#include <cstdio>

struct NativeSerialShim {
  void begin(unsigned long) {}
  explicit operator bool() const { return true; }

  int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = std::vprintf(format, args);
    va_end(args);
    return result;
  }
};

inline NativeSerialShim Serial;

inline unsigned long millis() {
  return 0;
}

inline void delay(unsigned long) {}
#endif