# CLAUDE.md

## Project Overview

Vivado 2024.1 FPGA hardware design + test suite for the **Digilent Arty Z7-20** (Xilinx Zynq-7000 XC7Z020 SoC). Three test modes: Linux UIO (PetaLinux), bare-metal (JTAG), and FreeRTOS (JTAG). Exercises GPIO accumulator, AXI Stream FIFO loopback, and AXI DMA echo.

The FPGA design originates from `~/work/artyz7/arty_petalinux/` and is packaged here for standalone hardware testing.

### Repository layout

```
vivado/                  Vivado project: TCL source + build system
  arty_hw.tcl            Block design TCL (adapted from arty_petalinux.tcl)
  build.tcl              Batch orchestrator: synth -> impl -> XSA
  ip/my_state.v          Custom 64-bit accumulator module
apps/
  arty_hw_test/          Linux UIO test (arm32, cross-compiled)
  arty_hw_bm/            Bare-metal test (ps7_cortexa9_0, standalone)
  arty_hw_rtos/          FreeRTOS test (ps7_cortexa9_0, freertos10_xilinx)
scripts/                 Board-side helpers
```

## Build Environment

```bash
# Vivado (XSA build)
. /tools/Xilinx/Vivado/2024.1/settings64.sh

# Vitis Classic (bare-metal / FreeRTOS builds)
. /tools/Xilinx/Vitis/2024.1/settings64.sh
```

## Common Commands

```bash
# Build XSA (25-35 min)
make xsa

# Bare-metal: build + run via JTAG
make bare-metal-build
make bare-metal-run

# FreeRTOS: build + run via JTAG
make rtos-build
make rtos-run

# Linux: cross-compile + deploy + run on board
make deploy-run    # requires ARTY_HOST set to board IP

# UART console
make tty
```

## Architecture

### Target Platform
- **Board**: Digilent Arty Z7-20
- **SoC**: Zynq-7000 XC7Z020 (dual Cortex-A9 @ 650 MHz, 512 MB DDR3)
- **FPGA Part**: xc7z020clg400-1
- **Toolchain**: Vivado 2024.1, Vitis Classic 2024.1, PetaLinux 2024.1

### FPGA Peripherals (AXI-mapped)

| Peripheral | Address | Description |
|---|---|---|
| `axi_gpio_control` | `0x41220000` | Dual-ch: ch1=control[1:0], ch2=value[31:0] |
| `axi_gpio_values` | `0x41230000` | Dual-ch: ch1=sum[31:0], ch2=carry[31:0] |
| `axi_fifo_mm_s_0` | `0x43C00000` | AXI Stream FIFO (PG080), TX->RX loopback |
| `axi_fifo_mm_s_1` | `0x43C10000` | AXI Stream FIFO (PG080), TX->RX loopback |
| `axi_dma_0` | `0x40400000` | AXI DMA v7.1, MM2S/S2MM via HP0, no SG |
| `axi_dma_1` | `0x40410000` | AXI DMA v7.1, MM2S/S2MM via HP0, no SG |
| `axi_gpio_0` | `0x41200000` | RGB LED + 2-bit switches |
| `axi_gpio_1` | `0x41210000` | 4 buttons + 4 LEDs |

Tests exercise one FIFO (`_0`) and one DMA (`_0`).

### Custom IP: my_state (64-bit accumulator)
- **Inputs**: `control[1:0]`, `value[31:0]`, `clock`, `reset`
- **Outputs**: `sum[31:0]`, `carry[31:0]`
- Edge-triggered: 0->1 = add, 0->2 = reset
- Driven by `axi_gpio_control`, read via `axi_gpio_values`

### DMA Cache Coherency

`S_AXI_HP0` is **not coherent** with CPU caches.
- **Linux**: `posix_memalign` + `mlock` + `/proc/self/pagemap` to get physical addresses, then re-map through `/dev/mem` with `O_SYNC` for uncached access.
- **Bare-metal/FreeRTOS**: `Xil_DCacheFlushRange` / `Xil_DCacheInvalidateRange` around DMA transfers.

### AXI Stream FIFO (PG080) Register Map

Uses Xilinx `axi_fifo_mm_s` IP (not the custom `simple_fifo.v` from kr260_hw).
Key registers: ISR (0x00), TDFV (0x0C), TDFD (0x10), TLR (0x14), RDFO (0x1C), RDFD (0x20), RLR (0x24), SRR (0x28).

### JTAG

Arty Z7-20 uses a Digilent FT2232H (USB VID:PID 0403:6010):
- Channel A: JTAG
- Channel B: UART (115200 8N1)

Cable filter in load.tcl: `jtag_cable_name =~ "Digilent*"`
JTAG targets: `xc7z020` (PL), `ARM Cortex-A9 MPCore #0` (CPU)

### Linux Test Deployment

Board must be running PetaLinux (from `arty_petalinux` project) with the matching bitstream loaded at boot via BOOT.BIN. The test app is cross-compiled with `arm-linux-gnueabihf-gcc` and deployed via SCP.
