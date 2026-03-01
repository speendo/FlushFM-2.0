# Guideline: Software Architecture

> **Status:** Active  
> **Last updated:** 2026-02-28

---

## Purpose

Defines the software framework stack, task architecture, power management strategy, and configuration approach for FlushFM 2.0.

---

## Rules

### Framework Stack

1. **FreeRTOS** – task management, queues, mutexes (provided by Arduino-ESP32)

2. **Arduino-ESP32** – hardware abstraction, WiFi, NVS, I2S, SPI, ADC

3. **ESP32-audioI2S** (schreibfaul1) – HTTP streaming, audio decoding (multiple formats: MP3, AAC, FLAC, WAV), I2S output. Wrapped behind an `IAudioPlayer` interface to allow library substitution without affecting the rest of the system.

4. **TFT_eSPI** – ILI9341 display driver via SPI

5. **ESP-IDF** (called directly where Arduino-ESP32 does not expose the API) – ULP coprocessor programming, light sleep / deep sleep control

6. **LittleFS** – filesystem in flash for serving the configuration web UI

### Task Architecture

7. **Core 0 – System tasks:**
   - WiFi stack (managed by framework)
   - Light sensor monitoring (ULP during sleep, ADC task when active)
   - Relay management
   - Display output (TFT_eSPI)
   - REST configuration server (AsyncWebServer, only active when requested)

8. **Core 1 – Audio tasks:**
   - ESP32-audioI2S (pinned to Core 1 via custom task wrapper)
   - HTTP stream fetching, audio decoding, I2S DMA output, PSRAM buffer management (all handled internally by the library)
   - **Note:** Core 1 pinning requires early verification – the library's internal task behavior needs testing to confirm effective core isolation.

9. **Inter-task communication** via FreeRTOS queues and mutexes only – no shared global variables. See concurrency guideline.

### Power Management

10. **Two-tier idle strategy:**
    - **Tier 1 – Light sleep** (normal idle): WiFi association maintained, CPU suspended, ULP monitors TEMT6000 ADC. Wake latency <1ms. HTTP stream disconnected. Relay cuts power to display and DAC.
    - **Tier 2 – Deep sleep** (extended absence): Triggered after extended consecutive hours without a light-on event. WiFi dropped, full CPU off, ULP continues light monitoring. Wake latency ~5-8 seconds including WiFi reconnect. Timer resets on every light-on event.

11. **Relay control:** Display and PCM5102A DAC are physically powered off via relay during both sleep tiers. This is the primary mechanism for idle power reduction. A second relay for externally powered speakers is optional but should be planned for in the relay management component to avoid a hardware redesign later.

12. **ULP coprocessor:** Monitors TEMT6000 ADC during both light sleep and deep sleep. Triggers a hardware interrupt / wake event when light level crosses the configured threshold.

### Configuration

13. **REST API** served by a lightweight AsyncWebServer on Core 0. Configurable settings: WiFi credentials, station list, light sensitivity threshold.

14. **Single-page UI** stored in LittleFS and served by the REST server. Accessible from any phone browser – no app required.

15. **mDNS** announces the device as `flushfm.local` for easy discovery without knowing the IP address.

16. **WPA2 Access Point fallback:** The device opens a WPA2-secured access point in two situations: (a) no WiFi credentials are configured (first-time setup), or (b) a WiFi connection cannot be established within a configurable timeout after waking. The AP password is derived from the ESP32's MAC address. This ensures WiFi credentials are never transferred in plain text and provides a recovery path when the configured network is unreachable. After credentials are saved or the connection is restored, the device switches back to station mode.

17. **Serial configuration interface:** All settings configurable via the REST API are also accessible through the Serial monitor using a simple command protocol. The serial interface is always active – it is a hardware peripheral with negligible resource cost. It serves as a developer convenience and as a last-resort recovery path when neither WiFi station mode nor the AP is accessible.

18. **Persistent storage** of all settings in ESP32 NVS (Non-Volatile Storage), surviving both sleep tiers and power cycles.

---

## Rationale

**ESP32-audioI2S** solves the hardest part – reliable HTTP radio streaming with decoding and I2S output – in a well-maintained library. The `IAudioPlayer` wrapper preserves architectural flexibility to replace it if needed.

**TFT_eSPI** is the most widely used and performant driver for ILI9341 on ESP32, with excellent DMA support.

**LittleFS over SPIFFS** for the config UI: LittleFS is the current recommended filesystem for Arduino-ESP32, with better reliability and wear leveling.

**Light sleep over deep sleep** as the default idle tier avoids multi-second startup delays while keeping idle power low. Deep sleep is reserved for genuinely extended absence (24h+) where the cold boot penalty is acceptable.

**WPA2 on the setup access point** protects credentials in transit. HTTPS is not required since WPA2 already encrypts the WiFi link.

---

## Exceptions

- **Interrupt service routines** (ULP wake, GPIO) are minimal and access hardware directly.
- **ESP32-audioI2S internal tasks** are managed by the library and do not follow the component interface pattern – this is accepted as per the modularity guideline's exception for hardware-bound vendor libraries.

---

## Examples

```cpp
// Jump seat: IAudioPlayer interface wrapping ESP32-audioI2S
class IAudioPlayer {
public:
    virtual bool begin() = 0;
    virtual bool connectToStation(const char* url) = 0;
    virtual void stop() = 0;
    virtual bool isPlaying() = 0;
    virtual void setVolume(uint8_t volume) = 0;
    virtual ~IAudioPlayer() = default;
};

class AudioPlayerESP32AudioI2S : public IAudioPlayer {
public:
    bool begin() override { audio_.setPinout(BCK_PIN, WS_PIN, DIN_PIN); return true; }
    bool connectToStation(const char* url) override { return audio_.connecttohost(url); }
    void stop() override { audio_.stopSong(); }
    bool isPlaying() override { return audio_.isRunning(); }
    void setVolume(uint8_t volume) override { audio_.setVolume(volume); }
    void update() { audio_.loop(); }  // Must be called regularly from Core 1 task
private:
    Audio audio_;
};
```
