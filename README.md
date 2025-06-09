# MacOS env.
This is the documentation for Espressif IoT Development Framework (esp-idf). ESP-IDF is the official development framework for the ESP32, ESP32-S, ESP32-C, ESP32-H and ESP32-P Series SoCs.

This is not easy to start with, but it is worth it. The documentation is very good and the examples are well written.

You need to install the toolchain and set up the environment before you can start using it. The documentation provides detailed instructions on how to do this, and then you can start with the examples.
Such as the "Hello World" and "blink" examples, which are the first steps to get familiar with the framework.
When you have completed the setup, you will be able to do more complex projects.

## Tips
1. To remember the commands: I know there is a lot of commands that you may not familiar with, but you can use [Makefile](https://www.gnu.org/software/make/manual/make.html#Simple-Makefile) to automate the process. You can create a Makefile in the root directory of your project and add the commands you need to run. Then you can just run `make` to execute the commands.
   This is a good tool to automate everywhere, not just in this environment. _Or you can just use my [Makefile](Makefile) to get started._

## References
1. [Standard Toolchain Setup for Linux and *macOS*](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/linux-macos-setup.html)
2. [ESP WIFI](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_wifi.html)
3. [ESP LOG](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/log.html)
4. [ESP32 Internal Temperature Sensor with ESP-IDF](https://esp32tutorials.com/esp32-internal-temperature-sensor-esp-idf/)
5. [Unit Test](https://github.com/espressif/esp-idf/blob/master/examples/system/unit_test/README.md)
6. [MQ-2 Gas Sensor](https://www.nmking.io/index.php/2022/11/18/598/)