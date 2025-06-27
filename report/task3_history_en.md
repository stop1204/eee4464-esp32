# Development History and Future Plans

## Key Pull Requests
- **#10 README update for MQTT**: Merged as `2270718`, added instructions for publishing sensor data to HiveMQ【F:README.md†L27-L40】.
- **#7 Imgbot image optimization**: Automatically compressed images in the repository to reduce size【F:logs/codex/1.png】.
- **#6 Added pulse oximeter and UART integration**: Integrated the MAX30102 module on the Arduino, sending data to the ESP32【F:sub_device/eee4464-uno/eee4464-uno.ino†L50-L110】.
- **#5 HTTP queue test**: Introduced `test_http_queue.c` to verify that high-priority requests are still sent when the queue is full【F:tests/test_http_queue.c†L28-L65】.
- **#4 Cloudflare API log fixes**: Adjusted log messages inside `cloudflare_api` for accuracy【F:cloudflare_api/cloudflare_api.c†L130-L140】.
- **#3 Makefile run target fix**: Added a `compile` target to avoid clashes with the `build` directory【F:makefile†L21-L23】.
- **#2 README environment variable fix**: Corrected an incorrect environment variable name【commit a0a0e77】.

## Issues and Resolutions
- Early versions had DHT timing errors and data offsets; PR #1 fixed the logic in `dht_read_raw`【F:components/dht/dht.c†L8-L32】.
- The HTTP queue previously lost data when full; later updates added high-priority handling and unit tests for reliability【F:main/main.c†L160-L213】【F:tests/test_http_queue.c†L28-L65】.
- On the Uno, using `analogWrite` on pins 3 or 11 conflicts with IR reception, so the code notes that these pins must use `digitalWrite` only【F:sub_device/eee4464-uno/eee4464-uno.ino†L229-L233】.

## Future Improvements
- **Modular sensor code**: Split sensors into separate files like `sensor_xxx.c` for reuse and unit testing.
- **Stronger retry and queue monitoring**: Add more sophisticated retry strategies and watchdog resets.
- **Explore a full MQTT architecture**: For more realtime data, shift to an MQTT-centric cloud with TLS verification and configuration files.

