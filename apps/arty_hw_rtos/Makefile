# Build and run apps/arty_hw_rtos -- FreeRTOS arty_hw exerciser.
#
#   make build  Build the FreeRTOS ELF. Auto-builds XSA if missing.
#   make run    Program PL + download ELF + run via JTAG. Captures UART.
#   make clean  Remove vitis_ws/ and the local log.

SHELL           := /bin/bash

XSA             := ../../vivado/arty_hw.xsa
WS              := vitis_ws
ELF             := $(WS)/arty_hw_rtos/Debug/arty_hw_rtos.elf

VITIS_SETTINGS  ?= /tools/Xilinx/Vitis/2024.1/settings64.sh

UART_DEV ?= $(or $(shell for dev in /sys/class/tty/ttyUSB*; do \
    mfg=$$(cat "$$dev/device/../../manufacturer" 2>/dev/null); \
    intf=$$(cat "$$(readlink -f $$dev/device/..)/bInterfaceNumber" 2>/dev/null); \
    if [ "$$mfg" = "Digilent" ] && [ "$$intf" = "01" ]; then \
        echo "/dev/$$(basename $$dev)"; break; \
    fi; \
done),/dev/ttyUSB1)
LOG_FILE        ?= rtos.log
TIMEOUT_S       ?= 10

.PHONY: build run clean

build: $(ELF)

$(XSA):
	@echo "==> XSA not found at $@ -- building via vivado/Makefile"
	$(MAKE) -C ../../vivado xsa

$(ELF): $(XSA) main.c build.tcl
	@if [ ! -f "$(VITIS_SETTINGS)" ]; then \
	    echo "error: Vitis Classic not found at $(VITIS_SETTINGS)" >&2; \
	    exit 1; \
	fi
	rm -rf $(WS)
	source $(VITIS_SETTINGS) && xsct build.tcl

run: $(ELF)
	@if [ ! -e "$(UART_DEV)" ]; then \
	    echo "error: $(UART_DEV) does not exist" >&2; \
	    exit 1; \
	fi
	@if [ ! -r "$(UART_DEV)" ] || [ ! -w "$(UART_DEV)" ]; then \
	    echo "error: $(UART_DEV) not accessible -- add yourself to dialout group:" >&2; \
	    echo "  sudo usermod -aG dialout \$$USER && newgrp dialout" >&2; \
	    exit 1; \
	fi
	@if [ ! -f "$(VITIS_SETTINGS)" ]; then \
	    echo "error: Vitis Classic not found at $(VITIS_SETTINGS)" >&2; \
	    exit 1; \
	fi
	@for dev in /sys/bus/usb/devices/*; do \
	    if [ -f "$$dev/manufacturer" ] && [ "$$(cat $$dev/manufacturer 2>/dev/null)" = "Digilent" ] && \
	       [ "$$(cat $$dev/idProduct 2>/dev/null)" = "6010" ]; then \
	        intf="$$(basename $$dev):1.0"; \
	        if [ -e "/sys/bus/usb/drivers/ftdi_sio/$$intf" ]; then \
	            echo "==> Unbinding $$intf from ftdi_sio (JTAG channel)"; \
	            echo "$$intf" | sudo tee /sys/bus/usb/drivers/ftdi_sio/unbind > /dev/null; \
	        fi; \
	    fi; \
	done
	@echo "==> Configuring $(UART_DEV) (115200 8N1)"
	@stty -F $(UART_DEV) 115200 cs8 -cstopb -parenb -ixon raw -echo
	@rm -f $(LOG_FILE)
	@bash -c 'set -e; \
	    stdbuf -oL cat $(UART_DEV) > $(LOG_FILE) & \
	    CAT_PID=$$!; \
	    trap "kill $$CAT_PID 2>/dev/null || true" EXIT; \
	    sleep 0.2; \
	    echo "==> Programming PL + downloading ELF via xsct load.tcl"; \
	    source $(VITIS_SETTINGS) && xsct load.tcl; \
	    echo "==> ELF running -- capturing UART for $(TIMEOUT_S)s"; \
	    sleep $(TIMEOUT_S); \
	    kill $$CAT_PID 2>/dev/null || true; \
	    wait $$CAT_PID 2>/dev/null || true; \
	    echo; \
	    echo "==> Captured output (saved to $(LOG_FILE)):"; \
	    echo "------------------------------------------------"; \
	    cat $(LOG_FILE); \
	    echo "------------------------------------------------"'

clean:
	rm -rf $(WS) $(LOG_FILE)
