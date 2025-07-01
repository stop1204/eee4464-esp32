# ESP32 Sensor Hub

A demonstration IoT sensor hub built around the ESP32 and an Arduino Uno. It collects data from multiple sensors and publishes them to a HiveMQ MQTT broker. Sensor and device registration is performed automatically using a Cloudflare API.

## Features
- Temperature and humidity monitoring with a DHT sensor
- Soil moisture sensing and automatic pump control
- Current measurement using an ACS712
- Light intensity and motion detection
- Heart rate readings from an Arduino Uno over UART
- Sensor values validated and published to HiveMQ via MQTT
- Automatic device and sensor registration at boot

## Directory Structure
```
cloudflare_api/   # Cloudflare registration helpers
components/       # Reusable components for sensors and MQTT
main/             # Application entry point
sub_device/       # Arduino sketch for MAX30102 heart rate sensor
tests/            # Unit tests
```

## Requirements
- [ESP-IDF](https://github.com/espressif/esp-idf)
- Serial connection to the ESP32 board
- Arduino Uno with a MAX30102 sensor

## Quick Start
1. Install the ESP-IDF toolchain and run `make init` to set the target.
2. Run `make port` to detect the serial port.
3. Build the project with `make compile`.
4. Flash the firmware using `make flash`.
5. View serial output via `make monitor`.

## Usage
Configuration options are available through `make config` which opens the `idf.py menuconfig` interface. MQTT publishing can be toggled here. During runtime sensor readings are pushed to topics of the form `iot/<sensor>` with JSON payloads such as:
```json
{"temperature":25.4}
```

## Examples
The Arduino sketch in `sub_device/eee4464-uno/eee4464-uno.ino` reads heart rate and SpO2 values. It prints JSON strings (e.g. `{"hr":75,"spo2":98}`) that are consumed by the ESP32 and forwarded to the broker.

## FAQ
**How do I change the MQTT broker address?**
Edit the broker URL in `main/main.c` and rebuild the project.

**The serial port is not detected. What should I do?**
Ensure the ESP32 is connected and try running `make port` again or specify the port manually in `.port`.

## License
This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Contact
Terry He & Karen - <230263367@stu.vtc.edu.hk>
