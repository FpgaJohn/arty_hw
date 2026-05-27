SHELL           := /bin/bash
VIVADO_SETTINGS ?= /tools/Xilinx/Vivado/2024.1/settings64.sh
VIVADO          ?= vivado
JOBS            ?= 1

PROJ_NAME := arty_hw
PROJ_DIR  := $(PROJ_NAME)
XSA       := $(PROJ_NAME).xsa

VIVADO_FLAGS := -mode batch -nojournal -log build.log

.PHONY: all xsa clean

all: xsa

xsa:
	. $(VIVADO_SETTINGS) && JOBS=$(JOBS) $(VIVADO) $(VIVADO_FLAGS) -source build.tcl

clean:
	rm -rf $(PROJ_DIR) .Xil
	rm -f build.log vivado*.jou vivado*.log
	rm -f arty_hw.xsa
