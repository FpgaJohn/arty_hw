# load.tcl -- program the FPGA via JTAG, run ps7_init, download + run the ELF.
#
# Prereq: Arty Z7-20 connected via USB (Digilent FTDI). Board powered on.
#
# Handles multiple JTAG cables: filters for the Arty Z7 (Digilent) so it
# works even with other boards (e.g. KR260) connected simultaneously.
#
# Run from this directory:
#   source /tools/Xilinx/Vitis/2024.1/settings64.sh
#   xsct load.tcl
#
# UART comes out on the FTDI USB at 115200 8N1 (ttyUSB1 on a single-board host).

set this_dir [file dirname [file normalize [info script]]]
set ws_path  [file normalize "$this_dir/vitis_ws"]
set platform arty_hw
set app      arty_hw_bm
set elf      $ws_path/$app/Debug/$app.elf

set bit      [file normalize "$this_dir/vitis_ws/$platform/hw/arty_hw.bit"]
set ps7_init_file [file normalize "$this_dir/vitis_ws/$platform/hw/ps7_init.tcl"]

foreach f [list $elf $bit $ps7_init_file] {
    if {![file exists $f]} { error "missing: $f" }
}

set arty_cable {jtag_cable_name =~ "Digilent*"}

connect

# Target the ARM DAP to program PL
targets -set -filter "name =~ \"APU*\" && $arty_cable"
rst -system
after 1000

# Program PL bitstream
targets -set -filter "name =~ \"xc7z020*\" && $arty_cable"
fpga $bit
after 500

# Initialize the PS
targets -set -filter "name =~ \"ARM*#0\" && $arty_cable"
stop
after 200
source $ps7_init_file
ps7_init
ps7_post_config
after 200

# Download and run
rst -processor
after 200
dow $elf
con

puts "load.tcl: ELF running. UART output on ttyUSB at 115200 8N1."
