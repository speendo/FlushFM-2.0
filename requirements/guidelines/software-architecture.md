# Rule: Software Architecture
[Status: Active | Updated: 2026-03-10]
**Context:** ESP32-S3 Framework & Tasking | **Goal:** Define the library stack, task distribution, and power/config strategies

---

## 1. Core Rules

### Framework Stack
- **Base:** `Arduino-ESP32` + `FreeRTOS`
- **Audio:** `ESP32-audioI2S` (by github user schreibfaul1) for streaming and decoding (MP3, AAC, FLAC), wrapped in an `IAudioPlayer` interface
- **Display:** `TFT_eSPI` for ILI9341 SPI communication
- **Filesystem:** `LittleFS` (Web UI assets) and `NVS` (persistent configuration)
- **Web:** `ESPAsyncWebServer` for the REST API and single-page configuration UI
- **Low-Level:** Use `ESP-IDF` directly for ULP (Ultra Low Power) coprocessor programming and sleep control

### Task Architecture
- **Core 0 (System):** Handles WiFi stack, Light Sensor monitoring (ADC), Relay management, Display output (TFT_eSPI), and the Async Web Server
- **Core 1 (Audio):** Dedicated to the `IAudioPlayer` implementation. Handles HTTP stream fetching, decoding, and I2S DMA output
- See `concurrency.md` for task creation rules and IPC patterns.

### Power & Relay Strategy
- **Tier 1 (Light Sleep):** Default idle; CPU suspended; WiFi association maintained; ULP monitors Light Sensor; **Relay cuts VCC to Display & DAC**; wake latency <1ms
- **Tier 2 (Deep Sleep):** Triggered after long (e.g. >24h) inactivity; WiFi dropped; CPU off; ULP active; Wake latency 5-8s
- **Relay Management:** Must support a primary relay for Display/DAC and provide logic for an **optional second relay** for external speakers

### Configuration & Recovery
- **AP Fallback:** WPA2-secured Access Point if no WiFi is configured or reachable
- **Discovery:** Announce the device as `flushfm.local` via `mDNS`
- **Serial CLI:** Maintain an active command protocol over Serial as a permanent recovery path

---

## 2. Constraints & Exceptions
- **Limit:** The `audio.loop()` function must be called with high priority on Core 1 to prevent stream stuttering
- **Never:** Allow a missing display or DAC to block the boot sequence (→ `modularity.md`: Graceful Degradation)
- **Exception:** Internal library-managed tasks (e.g., WiFi/Audio DMA) are exempt from manual pinning rules as they are handled by the framework

---

## 3. Reference Pattern
```cpp
// Architectural Wrapper for the ESP32-audioI2S library
class AudioPlayerESP32 : public IAudioPlayer {
    Audio _audio; // Instance of ESP32-audioI2S
public:
    bool begin() override { 
        _audio.setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT); 
        return true; 
    }
    void loop() override { 
        _audio.loop(); // Crucial: must be called in Core 1 loop
    }
    bool connect(const char* url) override { 
        return _audio.connecttohost(url); 
    }
};