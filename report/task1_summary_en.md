# ESP32 Sensor Hub Program Overview

This project is built on the ESP-IDF framework. The ESP32 handles all sensing and networking, while an Arduino Uno forwards heart-rate data over UART. Key modules and features include:

## Sensors and Functions
- **DHT11** – Reads temperature and humidity via `dht_read_float_data()` in `main/main.c`【F:main/main.c†L1066-L1084】.
- **Soil moisture sensor** – Uses the ADC to measure and drive the pump relay. Logic is in `read_soil_sensor()` and subsequent control checks【F:main/main.c†L1090-L1159】.
- **ACS712 current sensor** – Averages voltage to get current and publishes via MQTT【F:main/main.c†L1043-L1056】【F:main/main.c†L1051-L1052】.
- **LDR (photocell)** – Reads light level and converts to voltage【F:main/main.c†L1003-L1010】.
- **RCWL-0516 radar** – Samples five times to decide if motion is detected【F:main/main.c†L1016-L1035】.
- **Heart rate** – The Arduino Uno uses a MAX30102 and sends JSON over UART, parsed in `process_arduino_data()`【F:main/main.c†L1174-L1194】.

## Data Transmission and Tasks
- HTTP requests are queued by `http_request_queue` and processed in `http_request_task()` to upload to the Cloudflare API【F:main/main.c†L168-L213】.
- MQTT client is started in `second_loop_task()` for real-time data such as current【F:main/main.c†L960-L971】【F:main/main.c†L1051-L1052】.
- Main tasks:
  - `main_loop_task()` – Waits for Wi-Fi and handles cloud commands【F:main/main.c†L864-L933】.
  - `second_loop_task()` – Periodically reads sensors and sends data【F:main/main.c†L958-L1171】.
  - `button_task()` – Handles the test button interrupt and queue post【F:main/main.c†L746-L809】.

## ESP-IDF and Arduino
- The ESP32 code is entirely ESP‑IDF, including FreeRTOS tasks and networking.
- The Arduino Uno uses the Arduino framework in `eee4464-uno.ino` for heart-rate measurement and LED control. Because the IR receiver uses Timer2, some PWM pins can only be controlled with `digitalWrite`【F:sub_device/eee4464-uno/eee4464-uno.ino†L229-L233】.

