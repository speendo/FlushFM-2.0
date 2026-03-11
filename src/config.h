#pragma once

// ---------------------------------------------------------------------------
// FlushFM 2.0 – Build-time configuration constants
// ---------------------------------------------------------------------------

// Serial
static constexpr uint32_t SERIAL_BAUD_RATE      = 115200;
static constexpr uint32_t SERIAL_TIMEOUT_MS     = 3000;
static constexpr size_t   SERIAL_CMD_BUF_SIZE   = 256;

// WiFi
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

// ---------------------------------------------------------------------------
// I2S – PCM5102A Audio DAC
// GPIO group 4-6: no strapping conflicts, isolated from SPI bus
// See docs/pinout.md for full wiring details
// ---------------------------------------------------------------------------
static constexpr int I2S_BCK_PIN  = 4;
static constexpr int I2S_WS_PIN   = 5;
static constexpr int I2S_DOUT_PIN = 6;

// ---------------------------------------------------------------------------
// SPI – ILI9341 Display (SPI2 / FSPI IOMUX pins for best throughput)
// ---------------------------------------------------------------------------
static constexpr int TFT_SCK_PIN  = 12;
static constexpr int TFT_MOSI_PIN = 11;
static constexpr int TFT_CS_PIN   = 10;
static constexpr int TFT_DC_PIN   =  9;
static constexpr int TFT_RST_PIN  =  8;

// ---------------------------------------------------------------------------
// ADC – TEMT6000 Light Sensor (ADC1 only; ADC2 unavailable when WiFi active)
// ---------------------------------------------------------------------------
static constexpr int LIGHT_SENSOR_PIN = 1;  // ADC1_CH0

// ---------------------------------------------------------------------------
// Relay – Switched Power Domain (Display + DAC)
// ---------------------------------------------------------------------------
static constexpr int RELAY_PIN = 21;

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
// Default volume at startup (0 = mute, up to AUDIO_VOLUME_STEPS max; ESP32-audioI2S default steps = 21).
// Suitable for headphones connected directly to PCM5102A LOUT/ROUT.
static constexpr uint8_t AUDIO_VOLUME_STEPS   = 21;   // passed to setVolumeSteps() – increase for finer control
static constexpr uint8_t AUDIO_VOLUME_DEFAULT = 10;

// ---------------------------------------------------------------------------
// Audio – Buffering & Task
// ---------------------------------------------------------------------------
// Larger ring buffers reduce dropouts on high-bitrate streams (256 kbps MP3).
// Values are passed to ESP32-audioI2S via setInternalBufferSize / PSRAM buffer.
#define AUDIO_TASK_STACK_SIZE   (8192)          // FreeRTOS stack for audio task
#define AUDIO_TASK_PRIORITY     (2)             // Priority 2: high enough for smooth audio, leaves room for watchdog
#define AUDIO_TASK_CORE         (1)             // Core 1 – dedicated audio core (US-0003)
