# Guideline: Concurrency and Threading

> **Status:** Active  
> **Last updated:** 2026-02-28

---

## Purpose

Defines the FreeRTOS task model for FlushFM 2.0, including core assignment, inter-task communication, and synchronization rules.

---

## Rules

### Core Assignment

1. **Core 0 – System tasks:** WiFi stack, light sensor, relay management, display, REST configuration server.

2. **Core 1 – Audio tasks:** ESP32-audioI2S and all audio processing (HTTP fetch, decode, I2S DMA output, PSRAM buffering).

3. **Explicitly pin tasks to their assigned core** using `xTaskCreatePinnedToCore()`. Never use `xTaskCreate()` which assigns cores arbitrarily.

### Inter-Task Communication

4. **Use FreeRTOS queues for all inter-task data transfer.** Never pass data between tasks via shared global variables.

5. **Use FreeRTOS mutexes for shared resources** that must be accessed from multiple tasks (e.g. NVS reads/writes, shared state).

6. **Queue operations must be non-blocking on the sending side** – use `xQueueSend()` with a timeout of 0 or a short timeout; never block a task indefinitely waiting to send.

### Task Design

7. **Each task runs a simple loop** with a clear responsibility. Tasks should not perform work outside their defined domain.

8. **Interrupt service routines (ISRs) are minimal:** only set a flag, give a semaphore, or send to a queue using the ISR-safe variants (`xQueueSendFromISR`). No logic in ISRs.

9. **Audio task must not be starved:** System Core 0 tasks must not monopolize CPU time. Use appropriate `vTaskDelay()` calls to yield regularly.

---

## Rationale

Audio streaming has strict real-time requirements – the I2S DMA buffer must be kept fed continuously to avoid audio dropouts. Pinning audio tasks to Core 1 isolates them from system activity on Core 0 (WiFi, display updates, sensor polling) which can cause unpredictable latency spikes.

FreeRTOS queues and mutexes enforce safe inter-task communication without race conditions. Prohibiting shared global variables eliminates an entire class of hard-to-debug concurrency bugs.

---

## Exceptions

- **ESP32-audioI2S internal tasks** are created and pinned by the library itself. Their internal implementation is outside our control but they are expected to run on Core 1.
- **Arduino framework WiFi tasks** run in the background on Core 0, managed transparently by the framework.
- **ISRs** are exempt from the no-global-variable rule for the single flag/semaphore they set.

---

## Examples

```cpp
// Good: Non-blocking state machine
class AudioStreamer {
private:
    enum State { IDLE, CONNECTING, STREAMING };
    State state_ = IDLE;
    
public:
    void update() {  // Called from main loop
        switch (state_) {
            case IDLE:
                // Do nothing, waiting for connect() call
                break;
            case CONNECTING:
                if (httpClient_.connected()) {
                    state_ = STREAMING;
                    DEBUG_LOG("Connection established");
                } else if (connectionTimeout()) {
                    state_ = IDLE;
                    ERROR_LOG("Connection failed");
                }
                break;
            case STREAMING:
                processAudioData();  // Non-blocking data processing
                break;
        }
    }
    
    bool connect(const char* url) {
        if (state_ != IDLE) return false;
        httpClient_.beginAsync(url);  // Non-blocking connection start
        state_ = CONNECTING;
        return true;
    }
};
```

```cpp
// Bad: Blocking operation
class AudioStreamer {
public:
    bool connect(const char* url) {
        while (!httpClient_.connect(url)) {  // Bad: blocks main thread
            delay(100);  // Bad: freezes entire system
        }
        return true;
    }
};
```