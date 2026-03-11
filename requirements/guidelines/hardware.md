# Rule: Hardware
[Status: Active | Updated: 2026-03-09]
**Context:** ESP32-S3 Physical Layer | **Goal:** Define core components and power domain management

---

## 1. Core Rules
- **MCU:** Use ESP32-S3-D-1 (N16R8) with 16MB Flash and 8MB PSRAM.
- **Display:** Interface ILI9341 3.5" (240x320) TFT via SPI; do not implement touch functionality.
- **Audio:** Use PCM5102A DAC via I2S for audio output.
- **Light Sensor:** Connect TEMT6000 analog sensor to ADC1 pins only
- **Power Isolation:** Use a physical relay to switch VCC for the "Switched Domain"
- **Domain Mapping:** Always-On (MCU, WiFi, Sensor, Relay); Switched (Display, Audio)
- **Switching Logic:** Physically disconnect the Switched Domain during idle/off states to achieve zero standby current for peripherals (→ `state-management.md`: IDLE state)

## 2. Constraints & Exceptions
- **Limit:** Use ADC1 pins for the TEMT6000 (ADC2 is unavailable when WiFi is active)
- **Limit:** Ensure PSRAM is initialized and utilized for audio buffering to leverage the 8MB available
- **Never:** Communicate with SPI/I2S peripherals unless the Relay-State is confirmed HIGH (Power Domain active)
- **Exception:** Low-power sleep modes of the ESP32-S3 can be used as long as the Relay remains under active control

## 3. Reference Pattern
```cpp
void vPowerUpTask(void *pvParameters) {
    // 1. Activate Power Domain
    digitalWrite(PIN_RELAY, HIGH);
    vTaskDelay(pdMS_TO_TICKS(50)); // Voltage stabilization

    // 2. Initialize Switched Peripherals
    display.begin();
    audio.begin();

    DEBUG_LOG("HW", "Switched domain active");
    vTaskDelete(NULL);
}