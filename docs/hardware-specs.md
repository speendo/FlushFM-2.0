# Hardware Specifications

## Microcontroller

- **SoC:** ESP32-S3 (dual-core Xtensa LX7, up to 240 MHz)
- **Flash:** 16 MB (QIO)
- **PSRAM:** 8 MB OPI
- **Connectivity:** 2.4 GHz WiFi 802.11 b/g/n, Bluetooth 5 LE

## Audio Output

- **DAC:** PCM5102A
- **Interface:** I2S

## Display

- **Controller:** ILI9341
- **Size:** 3.5", 240×320 pixels, no touch
- **Interface:** SPI

## Light Sensor

- **Sensor:** TEMT6000 ambient light sensor
- **Interface:** ADC (analog)

## Power Management

- A relay switches power to non-essential components (display, audio DAC) off during idle/sleep to minimize current draw.

## Serial / Programming

- USB CDC via the ESP32-S3's built-in USB peripheral (no separate USB-UART chip required)
- Baud rate: 115200
