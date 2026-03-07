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
