# The Packetfather helper targets.
#
# This Makefile is intentionally thin. ESP-IDF remains the source of truth for
# build configuration; these targets only wrap common commands.

SHELL := /bin/bash

PROJECT_DIR := $(CURDIR)
BUILD_DIR := $(PROJECT_DIR)/build

PORT ?= /dev/ttyACM0
BAUD ?= 115200
CHIP ?= esp32c3
ESP_IDF_DIR ?= $(HOME)/esp/esp-idf-v5.0.1

APP_BIN := $(BUILD_DIR)/the-packetfather.bin
BOOT_BIN := $(BUILD_DIR)/bootloader/bootloader.bin
PART_BIN := $(BUILD_DIR)/partition_table/partition-table.bin

IDF_PY ?= idf.py
PYTHON ?= python3
ESPTOOL ?= $(IDF_PATH)/components/esptool_py/esptool/esptool.py

define IDF_ENV
if ! command -v $(IDF_PY) >/dev/null 2>&1; then \
	echo "idf.py not found; loading ESP-IDF from $(ESP_IDF_DIR)"; \
	test -f "$(ESP_IDF_DIR)/export.sh" || (echo "missing ESP-IDF export script: $(ESP_IDF_DIR)/export.sh" && exit 1); \
	. "$(ESP_IDF_DIR)/export.sh" >/dev/null; \
fi
endef

define WAIT_FOR_ESP
echo "Connect the ESP32-C3, then press SPACE to flash."; \
while true; do \
	IFS= read -r -n 1 key; \
	if [ "$$key" = " " ]; then echo; break; fi; \
done
endef

.PHONY: all set-target build compile flash flash-idf monitor flash-monitor check-bins clean size

all: build

set-target:
	@$(IDF_ENV); $(IDF_PY) set-target $(CHIP)

build:
	@$(IDF_ENV); $(IDF_PY) build

compile: build

check-bins:
	@test -f "$(BOOT_BIN)" || (echo "missing bootloader: $(BOOT_BIN)" && exit 1)
	@test -f "$(PART_BIN)" || (echo "missing partition table: $(PART_BIN)" && exit 1)
	@test -f "$(APP_BIN)" || (echo "missing app binary: $(APP_BIN)" && exit 1)

flash: build check-bins
	@$(IDF_ENV); \
	$(WAIT_FOR_ESP); \
	test -n "$$IDF_PATH" || (echo "IDF_PATH is not set after ESP-IDF export." && exit 1); \
	ESPTOOL="$$IDF_PATH/components/esptool_py/esptool/esptool.py"; \
	test -f "$$ESPTOOL" || (echo "missing esptool.py: $$ESPTOOL" && exit 1); \
	$(PYTHON) "$$ESPTOOL" \
		--chip $(CHIP) \
		-p $(PORT) \
		-b $(BAUD) \
		--before default_reset \
		--after hard_reset \
		--no-stub \
		write_flash \
		--flash_mode dio \
		--flash_freq 80m \
		--flash_size 2MB \
		0x0 "$(BOOT_BIN)" \
		0x8000 "$(PART_BIN)" \
		0x10000 "$(APP_BIN)"

flash-idf: build
	@$(IDF_ENV); $(WAIT_FOR_ESP); $(IDF_PY) -p $(PORT) -b $(BAUD) flash

monitor:
	@$(IDF_ENV); $(IDF_PY) -p $(PORT) monitor

flash-monitor: flash monitor

size:
	@$(IDF_ENV); $(IDF_PY) size

clean:
	@if [ -f "$(BUILD_DIR)/build.ninja" ]; then \
		$(IDF_ENV); $(IDF_PY) clean; \
	else \
		echo "removing incomplete build directory: $(BUILD_DIR)"; \
		rm -rf "$(BUILD_DIR)"; \
	fi

