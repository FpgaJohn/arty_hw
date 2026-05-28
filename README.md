# arty_hw

## Description

Vivado 2024.1 FPGA hardware design and test suite for the **Digilent Arty Z7-20**
(Xilinx Zynq-7000 XC7Z020 SoC). The same FPGA design is exercised three ways:

- **Bare-metal** (`apps/arty_hw_bm/`) — standalone ELF on `ps7_cortexa9_0`, loaded via JTAG.
- **FreeRTOS** (`apps/arty_hw_rtos/`) — FreeRTOS ELF on `ps7_cortexa9_0`, loaded via JTAG.
- **Linux** (`apps/arty_hw_test/`) — userspace UIO app cross-compiled for arm32 and run on a PetaLinux board over SSH.

Each app exercises the GPIO accumulator (`my_state`), AXI Stream FIFO loopback,
and AXI DMA echo paths defined by the block design.

## Requirements

- Digilent Arty Z7-20 board, powered, USB connected (Digilent FT2232H — channel A = JTAG, channel B = UART @ 115200 8N1).
- Linux host with:
  - **Vivado 2024.1** at `/tools/Xilinx/Vivado/2024.1/` (XSA build).
  - **Vitis Classic 2024.1** at `/tools/Xilinx/Vitis/2024.1/` (bare-metal + FreeRTOS).
  - `arm-linux-gnueabihf-gcc` cross toolchain (Linux app build).
  - Xilinx cable drivers installed.
  - Current user in the `dialout` group (`sudo usermod -aG dialout $USER`).
- Digilent board files installed in Vivado (one-time): `make fetch-board-parts`.
- For Linux runs: an Arty Z7 booting PetaLinux from SD (built out of the `arty_petalinux` project) with the matching bitstream loaded at boot, reachable via SSH.

> **All three run modes require the XSA.** Build it once with `make xsa` (25–35 min) before running any of the sections below. The bare-metal and FreeRTOS app Makefiles will auto-build the XSA on demand if it is missing.

## Instructions

### Bare-metal (JTAG)

1. Set boot-mode jumper **JP4 to JTAG** (pins 1–2) and power-cycle the board, so the CPU comes up halted with the MMU off.
2. From the repo root:
   ```bash
   make xsa             # one-time, if not already built
   make bare-metal-build
   make bare-metal-run
   ```
   `bare-metal-run` programs the PL, runs `ps7_init`, downloads the ELF, and captures UART output to `apps/arty_hw_bm/bm.log`.
3. `make bare-metal-clean` removes the bare-metal Vitis workspace.

### FreeRTOS (JTAG)

1. Set boot-mode jumper **JP4 to JTAG** (pins 1–2) and power-cycle the board.
2. From the repo root:
   ```bash
   make xsa             # one-time, if not already built
   make rtos-build
   make rtos-run
   ```
   `rtos-run` programs the PL, runs `ps7_init`, downloads the FreeRTOS ELF, and captures UART.
3. `make rtos-clean` removes the FreeRTOS Vitis workspace.

### Linux (PetaLinux board over SSH)

1. Set boot-mode jumper **JP4 to SD** and boot the board into PetaLinux from the SD card built by the `arty_petalinux` project. The SD image must contain the matching `BOOT.BIN` so the bitstream is loaded at boot.
2. From the repo root:
   ```bash
   make xsa             # one-time, if not already built
   make deploy-run ARTY_HOST=<board-ip-or-hostname> ARTY_USER=petalinux
   ```
   `deploy-run` cross-compiles `apps/arty_hw_test/`, scp's it to `/tmp/` on the board, and runs it under `sudo` over SSH.
3. To view the board UART independently: `make tty`.
