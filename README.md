# ESP32 Sensor Hub Project

This project demonstrates a simple IoT sensor hub using an ESP32 and an Arduino Uno.
The ESP32 collects data from a variety of sensors and publishes the values to a HiveMQ MQTT broker. An Arduino Uno equipped with a MAX30102 sensor provides heart rate which is transmitted to the ESP32 via UART. A Cloudflare API is still used to register the device and sensors.

## Features

- Temperature and humidity monitoring using a DHT sensor
- Soil moisture sensing with automatic pump control
- Current measurement (ACS712)
- Light intensity and motion detection
- Heart rate from an Arduino Uno over UART
- Sensor data is validated and published to HiveMQ over MQTT
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

## MQTT Publishing

Sensor values are published to `mqtts://6bdeb9e091414b898b8a01d7ab63bcd2.s1.eu.hivemq.cloud:8883`.
Topics follow the pattern `iot/<sensor>` and each message is JSON encoded:
- `iot/temperature` – `{"temperature":25.4}`
- `iot/humidity` – `{"humidity":40.2}`
- `iot/soil` – `{"moisture":3500}`
- `iot/pump` – `{"pump_state":1}`
- `iot/light` – `{"light":3300}`
- `iot/motion` – `{"motion_detected":0}`
- `iot/current` – `{"current":0.42}`
- `iot/heart_rate` – `{"heart_rate":75}`

MQTT can be enabled or disabled via menuconfig. Run `make config` and look for the **MQTT** option.


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
   readings and publish the validated sensor data to the HiveMQ broker.
