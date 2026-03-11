# Pinout and Hardware Configuration

> GPIO assignments finalized during US-0002 planning (2026-03-11).
> Reserved/blocked: GPIO 0 (boot), 3/45/46 (strapping), 19/20 (USB CDC), 26–32 (flash), 33–37 (OPI PSRAM).

## I2S – PCM5102A Audio DAC

| Signal | ESP32-S3 GPIO | PCM5102A pin | Notes |
|--------|---------------|--------------|-------|
| BCK (Bit Clock) | **4** | SCK | |
| WS (Word Select) | **5** | LCK | |
| DOUT (Data Out) | **6** | DIN | |
| – | GND | SCK | Tie to GND: BCK-derived system clock, no MCLK needed |
| – | GND | FMT | Standard I2S format |
| – | GND | FLT | Normal latency filter |
| – | GND | DEMP | De-emphasis off |
| – | 3.3 V via 10 kΩ | XSMT | Soft-unmute (or connect to GPIO for SW mute) |

**Note:** PCM5102A is a line-level DAC (~2.1 V RMS). Headphones (≥ 32 Ω) can be connected directly.
Speakers require an external amplifier (e.g. PAM8403 stereo 3 W).

## SPI – ILI9341 Display

Using ESP32-S3 SPI2 (FSPI) IOMUX pins for maximum throughput without GPIO-matrix overhead.

| Signal | ESP32-S3 GPIO | ILI9341 pin |
|--------|---------------|-------------|
| SCK | **12** | SCK |
| MOSI | **11** | SDI (MOSI) |
| CS | **10** | CS |
| DC | **9** | DC |
| RST | **8** | RESET |
| MISO | – | not connected (display-only, no readback needed) |

## ADC – TEMT6000 Light Sensor

| Signal | ESP32-S3 GPIO | Notes |
|--------|---------------|-------|
| AIN | **1** | ADC1_CH0 – ADC1 mandatory (ADC2 unavailable with WiFi active) |

## Relay – Switched Power Domain

| Signal | ESP32-S3 GPIO | Switches |
|--------|---------------|----------|
| OUT | **21** | VCC to Display + DAC (Switched Domain) |

## Reserved GPIO summary

| GPIO(s) | Reserved for |
|---------|---------------|
| 0 | Boot button (strapping) |
| 3, 45, 46 | Strapping pins |
| 19, 20 | USB D−/D+ (CDC) |
| 26–32 | Internal SPI Flash |
| 33–37 | OPI PSRAM (N16R8) |
| 43, 44 | UART0 TX/RX (debug fallback) |

## USB / Serial

- Built-in USB CDC (GPIO 19/20, no user assignment)
- Baud rate: 115200

## Available spare GPIOs

GPIO 2, 7, 13–18, 38–42, 47, 48
