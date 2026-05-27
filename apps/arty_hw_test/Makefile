SHELL := /bin/bash

CC        ?= arm-linux-gnueabihf-gcc
CFLAGS    := -Wall -Wextra -O2
TARGET    := arty_hw_test

ARTY_HOST ?= arty
ARTY_USER ?= petalinux

.PHONY: all deploy run clean

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $@ $<

deploy: $(TARGET)
	scp $(TARGET) $(ARTY_USER)@$(ARTY_HOST):/tmp/

run: deploy
	ssh $(ARTY_USER)@$(ARTY_HOST) 'sudo /tmp/$(TARGET)'

clean:
	rm -f $(TARGET)
