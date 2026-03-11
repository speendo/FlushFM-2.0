# Rule: Concurrency and Threading
[Status: Active | Updated: 2026-03-09]
**Context:** ESP32, FreeRTOS, Dual Core | **Goal:** Audio and network stability via core isolation

---

## 1. Core Rules
- **Core Assignment:**
    - **Core 0:** System, SystemController, WiFi, Web Server, Sensors
    - **Core 1:** Audio Engine (Stream Client, Decoding, I2S DMA)
- **Task Design:** Each task must run a simple, dedicated loop; no mixing of domains
- **Task Creation:** Use `xTaskCreatePinnedToCore()` for all application tasks; never use `xTaskCreate()`
- **Library Pinning:** Initialize hardware-intensive libraries (Audio/Display) inside their pinned task
- **Communication:** Use Queues for data; use Mutexes for shared resources (NVS, Peripherals); Core 1 signals state changes to Core 0 via queue, never directly (→ `state-management.md`)
- **ISR Offloading:** Keep ISRs minimal; use `FromISR` signals to trigger task-level work
- **Yielding:** All loops must include `vTaskDelay()` or blocking calls to prevent starvation

## 2. Constraints & Exceptions
- **Limit:** Use `volatile` and `FromISR` variants for all ISR-to-task communication
- **Never:** Use shared global variables between tasks; use thread-safe IPC only
- **Never:** Access same hardware (I2C/SPI) from different cores without a Mutex
- **Never:** Perform blocking I/O in high-priority tasks or main loop
- **Never:** Run a busy-loop without periodically yielding via `vTaskDelay(1)` – even at low priority, a tight loop that never yields will starve `IDLE0`/`IDLE1` (priority 0) which need CPU time to reset their own WDT subscription, causing an abort and reboot. A brief yield every ~100 ms is sufficient.
- **Exception:** ISRs may modify a single `volatile` flag or signal a semaphore to wake a task

## 3. Reference Pattern

```cpp
void vAudioTask(void *pvParameters) {
    audio.begin(); // Forces affinity to Core 1
    for(;;) {
        audio.loop();
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}

// In setup()
xTaskCreatePinnedToCore(vAudioTask, "Audio", 8192, NULL, 2, NULL, 1);