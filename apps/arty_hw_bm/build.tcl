# build.tcl -- build the bare-metal app via xsct (Vitis Classic command line).
#
# Run from this directory:
#   source /tools/Xilinx/Vitis/2024.1/settings64.sh
#   xsct build.tcl
#
# Outputs:
#   vitis_ws/arty_hw_bm/Debug/arty_hw_bm.elf

set this_dir [file dirname [file normalize [info script]]]
set xsa_path [file normalize "$this_dir/../../vivado/arty_hw.xsa"]
set ws_path  [file normalize "$this_dir/vitis_ws"]
set platform arty_hw
set app      arty_hw_bm
set domain   standalone_domain
set cpu      ps7_cortexa9_0

puts "build.tcl: workspace = $ws_path"
puts "build.tcl: XSA       = $xsa_path"

if {![file exists $xsa_path]} {
    error "XSA not found at $xsa_path -- run 'make' in ../../vivado first"
}

file mkdir $ws_path
setws $ws_path

if {[catch {platform list} existing] || [lsearch $existing $platform] < 0} {
    puts "build.tcl: creating platform $platform"
    platform create -name $platform -hw $xsa_path -proc $cpu \
                    -os standalone -fsbl-target $cpu
    platform write
    platform generate
} else {
    puts "build.tcl: platform $platform already exists, skipping create"
    platform active $platform
}

if {[catch {app list} existing] || [lsearch $existing $app] < 0} {
    puts "build.tcl: creating app $app"
    app create -name $app -platform $platform -domain $domain \
               -template "Empty Application(C)" -lang C
    importsources -name $app -path $this_dir/main.c
} else {
    puts "build.tcl: app $app already exists, skipping create"
}

puts "build.tcl: building $app"
app build -name $app

set elf $ws_path/$app/Debug/$app.elf
if {[file exists $elf]} {
    puts "build.tcl: built $elf"
} else {
    error "build failed -- no $elf"
}
