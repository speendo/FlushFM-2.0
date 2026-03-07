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

// I2S pins (placeholder – will be finalized when PCM5102A is connected)
// See docs/pinout.md for planned peripheral assignments
static constexpr int I2S_BCK_PIN  = 14;
static constexpr int I2S_WS_PIN   = 15;
static constexpr int I2S_DOUT_PIN = 13;
