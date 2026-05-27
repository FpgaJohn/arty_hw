# load.tcl -- program the FPGA via JTAG, run ps7_init, download + run the ELF.
#
# Prereq: Arty Z7-20 connected via USB (Digilent FTDI). Board powered on.
# UART comes out on the FTDI USB at 115200 8N1.

set this_dir [file dirname [file normalize [info script]]]
set ws_path  [file normalize "$this_dir/vitis_ws"]
set platform arty_hw
set app      arty_hw_rtos
set elf      $ws_path/$app/Debug/$app.elf

set bit      [file normalize "$this_dir/vitis_ws/$platform/hw/arty_hw.bit"]
set ps7_init_file [file normalize "$this_dir/vitis_ws/$platform/hw/ps7_init.tcl"]

foreach f [list $elf $bit $ps7_init_file] {
    if {![file exists $f]} { error "missing: $f" }
}

set arty_cable {jtag_cable_name =~ "Digilent*"}

connect

targets -set -filter "name =~ \"APU*\" && $arty_cable"
rst -system
after 1000

targets -set -filter "name =~ \"xc7z020*\" && $arty_cable"
fpga $bit
after 500

targets -set -filter "name =~ \"ARM*#0\" && $arty_cable"
source $ps7_init_file
ps7_init
ps7_post_config
after 200

rst -processor
after 200
dow $elf
con

puts "load.tcl: ELF running. UART output on ttyUSB at 115200 8N1."
