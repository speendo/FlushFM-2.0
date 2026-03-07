# Pinout and Hardware Configuration

> **Note:** GPIO assignments are not yet finalized. The values below are placeholder defaults used during development (US-0001) and will be confirmed when the PCM5102A hardware is physically connected.

## I2S – PCM5102A Audio DAC

| Signal | ESP32-S3 GPIO | PCM5102A pin |
|--------|-------------|---------------|
| BCK (Bit Clock) | 14 | SCK |
| WS (Word Select) | 15 | LCK |
| DOUT (Data Out) | 13 | DIN |

## Planned assignments (not yet implemented)

| Peripheral | Interface | GPIOs |
|------------|-----------|-------|
| ILI9341 (Display) | SPI | TBD |
| TEMT6000 (Light sensor) | ADC | TBD |
| Relay (Power control) | GPIO output | TBD |

## USB / Serial

- Built-in USB CDC, no GPIO assignment needed
- Baud rate: 115200
