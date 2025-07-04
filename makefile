# 30/5/2025 Terry.He 230263367@stu.vtc.edu.hk
# Makefile for ESP32 workflow
# make init        # init IDF environment
# make port     # detect and save the first /dev/cu.* device to .port file
# make build       # build the project
# make flash       # flash to the detected port
# make monitor     # monitor the serial output
# make run         # default: get port then monitor

# Variable to store the selected port file path
PORT_FILE := .port

# Default shell
SHELL := /bin/zsh

default: m

# 1. Get and save first /dev/cu.* device to .port file
port:
	@echo "Detecting serial port..."
	@ls /dev/cu.usb* | head -n 1 > $(PORT_FILE)
	@echo "Saved port: $$(cat $(PORT_FILE))"

# 2. Build the project
# conflict with /build file , so need to use 'compile' instead of 'build'
compile:
	@echo "Building project..."
	@idf.py build
	@echo "Build complete."

b: compile

# 3. Flash the firmware to the detected port
flash:
	@if [ -f $(PORT_FILE) ]; then \
		PORT=$$(cat $(PORT_FILE)); \
		echo "Flashing to $$PORT..."; \
		idf.py -p $$PORT flash; \
	else \
		echo "Port not set. Run 'make port' first."; \
	fi
f: flash
# 4. Initialize ESP-IDF environment
init:
	. $(HOME)/esp/esp-idf-v5.2/export.sh; idf.py set-target esp32

i: init

# 5. Monitor the serial output
monitor:
	@if [ -f $(PORT_FILE) ]; then \
		PORT=$$(cat $(PORT_FILE)); \
		echo "Monitoring $$PORT..."; \
		idf.py -p $$PORT monitor; \
	else \
		echo "Port not set. Run 'make port' first."; \
	fi
m: monitor
# 6. Run: get port then monitor
run: port compile flash monitor


config:
	idf.py menuconfig
c: config