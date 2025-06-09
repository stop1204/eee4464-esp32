# ESP32 Sensor Hub Project

This project demonstrates a simple IoT sensor hub using an ESP32 and an Arduino Uno.
The ESP32 collects data from a variety of sensors and pushes the values to a
Cloudflare based API. An Arduino Uno equipped with a MAX30102 sensor provides
heart rate and blood oxygen (SpO2) measurements which are transmitted to the
ESP32 via UART.

## Features

- Temperature and humidity monitoring using a DHT sensor
- Soil moisture sensing with automatic pump control
- Current measurement (ACS712)
- Light intensity and motion detection
- Heart rate and SpO2 collection from an Arduino Uno over UART
- Sensor data is validated and sent to the cloud through HTTP requests
- Device and sensors are registered automatically on boot

## Requirements

- [ESP-IDF](https://github.com/espressif/esp-idf) toolchain
- A serial connection to the ESP32 board
- An Arduino Uno with a MAX30102 sensor connected via I2C

## Building and Flashing

The provided `makefile` wraps common `idf.py` commands. Typical workflow:

```bash
make port       # detect serial port
make compile    # build the firmware
make flash      # flash to the detected port
make monitor    # view serial output
```

For configuration run `make config` which invokes `idf.py menuconfig`.

## Arduino Sketch

The sketch located at `sub_device/eee4464-uno/eee4464-uno.ino` reads heart rate
and SpO2 from the MAX30102 using the reference algorithm. Results are printed as
JSON strings, e.g. `{"hr":75,"spo2":98}`, which are received by the ESP32.
Upload this sketch with the standard Arduino IDE.

## Running

1. Flash the ESP32 firmware using the steps above.
2. Upload the Arduino sketch to your Uno and connect its TX/RX pins (through a
   level shifter) to GPIO21/GPIO22 on the ESP32.
3. Once both devices are running, the ESP32 will log incoming heart rate and SpO2
   readings and forward all validated sensor data to the cloud API.

*** End of File ***
