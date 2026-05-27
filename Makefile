SHELL      := /bin/bash

ARTY_HOST ?= arty
ARTY_USER ?= petalinux

MAKEFLAGS += --no-print-directory

.PHONY: xsa xsa-clean bare-metal-build bare-metal-run bare-metal-clean rtos-build rtos-run rtos-clean deploy deploy-run tty

# Step 1 - Build Vivado Project and Export XSA
xsa:
	$(MAKE) -C vivado xsa

xsa-clean:
	$(MAKE) -C vivado clean

# [BARE-METAL] Step 2
bare-metal-build:
	$(MAKE) -C apps/arty_hw_bm build

bare-metal-run:
	$(MAKE) -C apps/arty_hw_bm run

bare-metal-clean:
	$(MAKE) -C apps/arty_hw_bm clean

# [FreeRTOS] Step 2
rtos-build:
	$(MAKE) -C apps/arty_hw_rtos build

rtos-run:
	$(MAKE) -C apps/arty_hw_rtos run

rtos-clean:
	$(MAKE) -C apps/arty_hw_rtos clean

VITIS_SETTINGS  ?= /tools/Xilinx/Vitis/2024.1/settings64.sh

# Auto-detect Arty Z7 PS-UART (Digilent FT2232H, interface 01).
ARTY_UART ?= $(or $(shell for dev in /sys/class/tty/ttyUSB*; do \
    mfg=$$(cat "$$dev/device/../../manufacturer" 2>/dev/null); \
    intf=$$(cat "$$(readlink -f $$dev/device/..)/bInterfaceNumber" 2>/dev/null); \
    if [ "$$mfg" = "Digilent" ] && [ "$$intf" = "01" ]; then \
        echo "/dev/$$(basename $$dev)"; break; \
    fi; \
done),/dev/ttyUSB1)

# Open Arty Z7 PS-UART in screen. Ctrl-a k to quit.
tty:
	@echo "==> Arty Z7 UART: $(ARTY_UART) (115200 8N1)"
	screen $(ARTY_UART) 115200

# [Linux] Deploy + run the userspace test app
deploy:
	$(MAKE) -C apps/arty_hw_test deploy

deploy-run:
	$(MAKE) -C apps/arty_hw_test run
