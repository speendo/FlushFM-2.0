# Tested Setups

## Setup 1 – ESP32-S3-DevKitC-1 (US-0000)

**Date:** 2026-03-01  
**Firmware:** US-0000 (Project Structure Setup)

### Hardware

| Component | Details |
|-----------|---------|
| Board | ESP32-S3-DevKitC-1-N8 |
| Flash | 8 MB QD (no PSRAM on this devkit) |
| CPU | ESP32-S3 @ 240 MHz |
| USB | Built-in USB CDC |

### Build environment

| Setting | Value |
|---------|-------|
| PlatformIO platform | espressif32 6.13.0 |
| Framework | Arduino-ESP32 3.20017.241212 |
| Build env | `debug` |
| Flash mode | QIO |

### Verified behaviour

- Board boots successfully
- WiFi hardware operational (network scan returns results)

### How to reproduce

1. Flash the `debug` environment via PlatformIO (`platformio run --target upload`)
2. Open serial monitor at 115200 baud
3. Press reset; observe startup output

---

## Setup 2 – ESP32-S3-DevKitC-1 Audio and Runtime Baseline

**Date:** 2026-03-29  
**Firmware Scope:** US-0001, US-0002, US-0003, US-0004a to US-0004f, US-0006, US-0010

### Verified behavior

- Runtime WiFi connect and stream start are functional
- Audio output through PCM5102A over I2S is functional
- Runtime control commands for playback and volume are functional
- State-machine based transitions are stable under normal serial CLI operation

### Board-specific notes (YD-ESP32-2.3)

- Target board for LED work is YD-ESP32-2.3 (tracked in US-0008)
- LED controller type and pin mapping are not verified yet
- No verified LED electrical characteristics are documented yet
- Required follow-up: identify LED controller and control method on real hardware during US-0008 implementation

---

## Validation Path (Template for New Setups)

Use this template whenever a new hardware setup is validated.

### Setup X - <Board or Variant>

**Date:** YYYY-MM-DD  
**Firmware Scope:** US-XXXX, US-YYYY

### Hardware

| Component | Details |
|-----------|---------|
| Board | <Exact board name> |
| Flash / PSRAM | <size and type> |
| Audio chain | <DAC/amp/headphones/speaker> |
| Display | <model or not used> |
| Sensors | <list or not used> |

### Build environment

| Setting | Value |
|---------|-------|
| PlatformIO platform | <value> |
| Framework | <value> |
| Build env | <value> |
| Flash mode | <value> |

### Verified behavior

- <behavior 1>
- <behavior 2>
- <behavior 3>

### Reproduction steps

1. <step 1>
2. <step 2>
3. <step 3>
