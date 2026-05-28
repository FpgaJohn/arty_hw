SHELL      := /bin/bash

ARTY_HOST ?= arty
ARTY_USER ?= petalinux

MAKEFLAGS += --no-print-directory

.PHONY: help xsa xsa-clean bare-metal-build bare-metal-run bare-metal-clean rtos-build rtos-run rtos-clean deploy deploy-run tty fetch-board-parts

help:
	@echo "arty_hw — Arty Z7-20 hardware test suite"
	@echo ""
	@echo "Vivado:"
	@echo "  make xsa               Build Vivado project and export XSA (25-35 min)"
	@echo "  make xsa-clean         Remove Vivado project and build artifacts"
	@echo ""
	@echo "Bare-metal (JTAG):"
	@echo "  make bare-metal-build  Build standalone ELF (ps7_cortexa9_0, via xsct)"
	@echo "  make bare-metal-run    Program PL + run ELF via JTAG, capture UART"
	@echo "  make bare-metal-clean  Remove bare-metal Vitis workspace"
	@echo ""
	@echo "FreeRTOS (JTAG):"
	@echo "  make rtos-build        Build FreeRTOS ELF (ps7_cortexa9_0, via xsct)"
	@echo "  make rtos-run          Program PL + run ELF via JTAG, capture UART"
	@echo "  make rtos-clean        Remove FreeRTOS Vitis workspace"
	@echo ""
	@echo "Linux (PetaLinux board via SSH):"
	@echo "  make deploy            Cross-compile and scp test app to board"
	@echo "  make deploy-run        Deploy + run test app on board via SSH"
	@echo ""
	@echo "Setup:"
	@echo "  make fetch-board-parts Fetch Digilent board files into Vivado 2024.1 xhub"
	@echo ""
	@echo "Utilities:"
	@echo "  make tty               Open Arty Z7 PS-UART in screen (115200 8N1)"
	@echo "  make help              Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  ARTY_HOST=$(ARTY_HOST)          Board hostname/IP for SSH deploy"
	@echo "  ARTY_USER=$(ARTY_USER)      Board username"
	@echo "  ARTY_UART=$(ARTY_UART)  PS-UART device (auto-detected)"

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

VIVADO_SETTINGS ?= /tools/Xilinx/Vivado/2024.1/settings64.sh

# Refresh the xhub catalog and install the latest Digilent board files for Vivado 2024.1.
fetch-board-parts:
	@if [ ! -f "$(VIVADO_SETTINGS)" ]; then \
	    echo "error: Vivado not found at $(VIVADO_SETTINGS)" >&2; \
	    exit 1; \
	fi
	. $(VIVADO_SETTINGS) && vivado -mode batch -nojournal -nolog -source /dev/stdin <<'FETCH_TCL'
	xhub::refresh_catalog [xhub::get_xstores xilinx_board_store]
	set items [xhub::get_xitems *arty-z7-20*]
	if {[llength $items] == 0} {
	    puts "No arty-z7-20 board files found in xhub catalog"
	    exit 1
	}
	foreach item $items {
	    puts "Installing: $item"
	}
	xhub::install $items
	puts "Board files installed. Verify with: get_board_parts *arty*"
	FETCH_TCL

# [Linux] Deploy + run the userspace test app
deploy:
	$(MAKE) -C apps/arty_hw_test deploy

deploy-run:
	$(MAKE) -C apps/arty_hw_test run
