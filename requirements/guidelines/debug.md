# Rule: Debug and Logging
[Status: Active | Updated: 2026-03-09]
**Context:** ESP32, C++, PlatformIO | **Goal:** Zero-overhead diagnostics and production error tracking

---

## 1. Core Rules
- **Binary Tiers:** Use `PROD_LOG`/`ERROR_LOG` (always) and `DEBUG_LOG` (conditional via `DEBUG_ENABLED`)
- **Zero Overhead:** Use preprocessor macros (`#define`) to ensure debug code is stripped during compilation
- **Traceability:** Include `tag` and timestamp (`millis()` or NTP-time if synced) in every log entry
- **Logging Events:** Log state changes (e.g. "idle -> streaming"), public function entry/exit, and external comms
- **Throttling:** Never log per data-chunk; use periodic status updates (e.g., every 10s) for continuous streams
- **Build Control:** Define `-DDEBUG_ENABLED` in `platformio.ini` for debug environments only (→ `build-system.md`)

## 2. Constraints & Exceptions
- **Limit:** Production logs must be minimal (WiFi status, critical hardware/init errors only)
- **Never:** Use `Serial.print()` directly; use provided macros for consistency and stripping
- **Never:** Use runtime `if`-conditions for debug logs; use preprocessor `#ifdef` only
- **Never:** Execute logic or heavy functions inside log arguments (e.g. `DEBUG_LOG(tag, get_status())`)
- **Exception:** Component-specific tests may use verbose logging unconditionally

## 3. Reference Pattern

```cpp
// debug.h
#ifdef DEBUG_ENABLED
    #define DEBUG_LOG(tag, msg, ...) Serial.printf("[%lu][DEBUG][%s] " msg "\n", millis(), tag, ##__VA_ARGS__)
#else
    #define DEBUG_LOG(tag, msg, ...) // Compiled to nothing
#endif

#define PROD_LOG(tag, msg, ...)  Serial.printf("[%lu][INFO][%s] "  msg "\n", millis(), tag, ##__VA_ARGS__)
#define ERROR_LOG(tag, msg, ...) Serial.printf("[%lu][ERROR][%s] " msg "\n", millis(), tag, ##__VA_ARGS__)

// Usage
void AudioPlayer::connect() {
    DEBUG_LOG("Audio", "Connecting to %s", url);
    if (!wifi.ok()) ERROR_LOG("WiFi", "Connection failed");
}