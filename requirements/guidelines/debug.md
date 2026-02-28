# Guideline: Debug and Logging

> **Status:** Active  
> **Last updated:** 2026-02-28

---

## Purpose

Defines logging strategy for FlushFM 2.0 that provides insightful diagnostic information during development while ensuring zero performance impact and minimal resource usage in production builds.

---

## Rules

### Binary Logging Approach (Production vs Debug)

1. **All logging is controlled by a single `DEBUG_ENABLED` build flag.** When disabled, debug messages are not compiled at all – they consume zero flash memory and zero runtime cycles.

2. **Production messages are always present:** Critical errors and essential status information that help diagnose issues in deployed devices (e.g. WiFi connection failures, hardware initialization errors).

3. **Debug messages are conditional:** Verbose diagnostic information for development and troubleshooting, only compiled when `DEBUG_ENABLED` is set.

4. **Use preprocessor macros, not runtime checks.** This ensures debug code is completely eliminated from production builds by the compiler.

5. **Debug messages include context:** Component name, function name, or meaningful identifiers to make logs easily traceable.

6. **Keep production messages concise** to minimize resource usage while still being informative.

### When to Add Debug Messages

7. **Function entry/exit for public methods:** Log when entering and exiting public component methods with relevant parameters and return values.

8. **State changes:** Log whenever component state changes (e.g. "idle → connecting → streaming").

9. **External communication:** Log discrete operations like connection establishment, configuration requests, and error responses. For continuous data streams (e.g. audio streaming), log connection events and periodic status updates (e.g. every 10 seconds), but not individual data chunks.

10. **Error conditions and recovery:** Log detailed error information and recovery attempts, even if a higher-level error message exists in production code.

11. **Configuration and initialization:** Log component setup, configuration values loaded, and hardware detection results.

12. **Performance-critical sections:** Log entry/exit of loops, timing-sensitive operations, and resource allocations to identify bottlenecks.

### Implementation

13. **Define logging macros in a shared header file** that all components can include.

14. **Use PlatformIO build flags** to control `DEBUG_ENABLED` at compile time via `platformio.ini`.

15. **Serial output is the primary logging destination** – simple, universally available, and ESP32-friendly.

16. **Include timestamps for temporal correlation.** Use `millis()` uptime (always available) or real time if NTP sync is available. Timestamps help correlate events and debug timing-sensitive issues.

---

## Rationale

ESP32 devices have limited flash memory (typically 4MB) and RAM (320KB), making it crucial that debug code doesn't impact production performance or storage. By using preprocessor conditional compilation, debug logging can be as verbose as needed during development without any cost in the final product.

The binary approach (production vs debug) keeps complexity low while covering the most common use cases:
- During development: Rich diagnostic output to understand system behavior
- In production: Only essential error/status messages that help with remote troubleshooting

*Note: If more granular control becomes necessary in the future (e.g. INFO level for user-facing status updates), this can be extended to a three-tier system (ALWAYS/INFO/DEBUG) without changing the core conditional compilation approach. Currently, we only use a two-tier system (ALWAYS/DEBUG) with DEBUG*

---

## Exceptions

- **Critical error paths** may include both production messages (user-friendly) and debug messages (technical detail) to aid troubleshooting at different levels.
- **Component test programs** may use debug output unconditionally since they are development builds by nature.

---

## Examples

```cpp
// Good: Logging macros with conditional compilation and timestamps
// debug.h
#ifdef DEBUG_ENABLED
  #define DEBUG_LOG(msg, ...) Serial.printf("[%lu][DEBUG] " msg "\n", millis(), ##__VA_ARGS__)
  #define DEBUG_COMPONENT(component, msg, ...) Serial.printf("[%lu][DEBUG][%s] " msg "\n", millis(), component, ##__VA_ARGS__)
#else
  #define DEBUG_LOG(msg, ...)
  #define DEBUG_COMPONENT(component, msg, ...)
#endif

#define PROD_LOG(msg, ...) Serial.printf("[%lu][INFO] " msg "\n", millis(), ##__VA_ARGS__)
#define ERROR_LOG(msg, ...) Serial.printf("[%lu][ERROR] " msg "\n", millis(), ##__VA_ARGS__)

// Usage in component
class AudioStreamer {
private:
    enum State { IDLE, CONNECTING, STREAMING };
    State currentState_ = IDLE;
    
public:
    bool connect(const char* url) {
        DEBUG_COMPONENT("AudioStreamer", "connect() called with URL: %s", url);  // Function entry
        
        if (currentState_ != IDLE) {
            DEBUG_COMPONENT("AudioStreamer", "Already in state %d, cannot connect", currentState_);
            return false;
        }
        
        setState(CONNECTING);  // State change logged internally
        DEBUG_COMPONENT("AudioStreamer", "Starting HTTP connection...");  // External communication
        
        if (/* connection fails */) {
            ERROR_LOG("Failed to connect to radio station");  // Always present
            DEBUG_COMPONENT("AudioStreamer", "HTTP error code: %d, retry count: %d", errorCode, retryCount);  // Debug detail
            
            setState(IDLE);  // State change on error
            DEBUG_COMPONENT("AudioStreamer", "connect() returning false");  // Function exit
            return false;
        }
        
        setState(STREAMING);
        PROD_LOG("Connected to radio station");  // Always present
        DEBUG_COMPONENT("AudioStreamer", "Buffer size: %d, bitrate: %d", bufSize, bitrate);  // Config info
        DEBUG_COMPONENT("AudioStreamer", "connect() returning true");  // Function exit
        return true;
    }
    
    void processAudioData() {
        // Don't log every audio chunk - too verbose!
        static unsigned long lastStatusLog = 0;
        if (millis() - lastStatusLog > 10000) {  // Every 10 seconds
            DEBUG_COMPONENT("AudioStreamer", "Status: %d bytes buffered, %d kbps", bufferLevel_, currentBitrate_);
            lastStatusLog = millis();
        }
    }
    
private:
    void setState(State newState) {
        DEBUG_COMPONENT("AudioStreamer", "State change: %d -> %d", currentState_, newState);  // State changes
        currentState_ = newState;
    }
};
```

```ini
; platformio.ini - Debug build
[env:debug]
platform = espressif32
board = esp32dev
build_flags = -DDEBUG_ENABLED

; platformio.ini - Production build  
[env:release]
platform = espressif32
board = esp32dev
; No DEBUG_ENABLED flag = debug messages not compiled
```