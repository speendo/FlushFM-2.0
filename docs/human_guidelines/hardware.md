# Guideline: Hardware

> **Status:** Active  
> **Last updated:** 2026-02-28

---

## Purpose

Documents the hardware components used in FlushFM 2.0 and their interfaces.

---

## Rules

### Hardware Components

1. **Microcontroller:** ESP32-S3-D-1 N16R8 (16MB Flash, 8MB PSRAM)

2. **Display:** ILI9341 3.5" 240x320 TFT (no touch) via SPI

3. **Light sensor:** TEMT6000 analog sensor via ADC

4. **Audio output:** PCM5102A DAC board via I2S

5. **Power management:** Relay to switch off non-essential components (audio interface, display) during idle/off state for power efficiency

6. **Connectivity:** Built-in WiFi for internet radio streaming

---

## Rationale

The relay for power management enables true low-power idle states by completely disconnecting power to non-essential components (display, audio interface) when the device is off, rather than leaving them in standby mode.

---

## Exceptions

None.

---

## Examples

Hardware overview:
- ESP32-S3 as central processing unit with WiFi capability
- Display, audio, and sensors as peripheral components
- Relay-controlled power domains for efficient idle operation