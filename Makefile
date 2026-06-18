# The Packetfather helper targets.
#
# This Makefile is intentionally thin. ESP-IDF remains the source of truth for
# build configuration; these targets only wrap common commands.

PROJECT_DIR := $(CURDIR)
BUILD_DIR := $(PROJECT_DIR)/build

PORT ?= /dev/ttyACM0
BAUD ?= 115200
CHIP ?= esp32c3

APP_BIN := $(BUILD_DIR)/the-packetfather.bin
BOOT_BIN := $(BUILD_DIR)/bootloader/bootloader.bin
PART_BIN := $(BUILD_DIR)/partition_table/partition-table.bin

IDF_PY ?= idf.py
PYTHON ?= python
ESPTOOL ?=

.PHONY: all set-target build compile flash monitor flash-monitor check-bins clean size

all: build

set-target:
	$(IDF_PY) set-target $(CHIP)

build:
	$(IDF_PY) build

compile: build

check-bins:
	@test -f "$(BOOT_BIN)" || (echo "missing bootloader: $(BOOT_BIN)" && exit 1)
	@test -f "$(PART_BIN)" || (echo "missing partition table: $(PART_BIN)" && exit 1)
	@test -f "$(APP_BIN)" || (echo "missing app binary: $(APP_BIN)" && exit 1)

flash: build check-bins
ifneq ($(ESPTOOL),)
	$(PYTHON) "$(ESPTOOL)" \
		--chip $(CHIP) \
		-p $(PORT) \
		-b $(BAUD) \
		--no-stub \
		write_flash \
		--flash_mode dio \
		--flash_freq 80m \
		--flash_size 2MB \
		0x0 "$(BOOT_BIN)" \
		0x8000 "$(PART_BIN)" \
		0x10000 "$(APP_BIN)"
else
	$(IDF_PY) -p $(PORT) -b $(BAUD) flash
endif

monitor:
	$(IDF_PY) -p $(PORT) monitor

flash-monitor: flash monitor

size:
	$(IDF_PY) size

clean:
	$(IDF_PY) clean
