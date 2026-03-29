# FlushFM 2.0

An ESP32-based internet radio with display and ambient light control.

## Project Status

- Active development
- Story organization: `requirements/user-stories/open/`, `requirements/user-stories/in-progress/`, `requirements/user-stories/done/`

## Implemented Features

- WiFi-based internet radio streaming with runtime serial CLI control
- HTTP audio playback with metadata callbacks (station, title, bitrate)
- PCM5102A I2S DAC output with runtime volume and balance control
- Stable task/core setup for audio runtime under load
- Persistent settings in NVS (WiFi credentials and last station)
- Runtime maintenance commands (`play`, `switch`, `stop`, `forget`, `reset`, `tasks`)

## Planned Features

- ILI9341 display integration for station and track info
- Ambient light based on/off behavior
- Onboard LED state visualization for YD-ESP32-2.3 (defined in US-0008)
- Web configuration UI (ESPAsyncWebServer + LittleFS)
- Relay to switch off unneeded consumers in IDLE/OFF mode

## Project Documentation

- **Requirements and Guidelines:** `requirements/`
- **User Stories:** `requirements/user-stories/`
- **Technical Documentation:** `docs/`

## Notes

- Documentation and story status conventions are defined in `requirements/guidelines/documentation.md`.
