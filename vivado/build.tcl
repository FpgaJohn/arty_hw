# build.tcl — drive a full Vivado run from arty_hw.tcl.
#
# Invoked by the Makefile in this directory:
#   vivado -mode batch -source build.tcl
#
# Assumes CWD is ./vivado/ relative to the repo root, so the project lands
# in ./vivado/arty_hw/ and source paths still resolve via origin_dir.

set origin_dir_loc "."

source arty_hw.tcl

set_param general.maxThreads 1

set jobs 4
if { [info exists ::env(JOBS)] } { set jobs $::env(JOBS) }

launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
if { [get_property PROGRESS [get_runs synth_1]] ne "100%" } {
    error "synth_1 did not complete: status=[get_property STATUS [get_runs synth_1]]"
}

launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
if { [get_property PROGRESS [get_runs impl_1]] ne "100%" } {
    error "impl_1 did not complete: status=[get_property STATUS [get_runs impl_1]]"
}

open_run impl_1
write_hw_platform -fixed -include_bit -force arty_hw.xsa

puts "----"
puts "Wrote XSA: [file normalize arty_hw.xsa]"
