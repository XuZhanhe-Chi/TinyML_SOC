proc getenv_or {name defaultValue} {
  if {[info exists ::env($name)] && $::env($name) ne ""} {
    return $::env($name)
  }
  return $defaultValue
}

proc find_repo_root {} {
  set dir [pwd]
  for {set i 0} {$i < 8} {incr i} {
    if {[file exists "$dir/fpga/gw5a/filelist.f"]} {
      return $dir
    }
    set parent [file dirname $dir]
    if {$parent eq $dir} { break }
    set dir $parent
  }
  return ""
}

set repo_root [getenv_or REPO_ROOT [find_repo_root]]
if {$repo_root eq ""} {
  puts stderr {[ERROR] REPO_ROOT not set and auto-detect failed.}
  exit 2
}

set proj_name [getenv_or PROJ_NAME "tinyml_soc_gw5a"]
set out_root [getenv_or OUT_ROOT "$repo_root/build/gowin"]
set top [getenv_or TOP_MODULE "Top"]
set part [getenv_or PART "GW5A-LV25MG121NC1/I0"]
set device_version [getenv_or DEVICE_VERSION "A"]
set filelist [getenv_or FILELIST "$repo_root/fpga/gw5a/filelist.f"]
set cst_file [getenv_or CST_FILE "$repo_root/fpga/gw5a/src/fpga_project.cst"]
set sdc_file [getenv_or SDC_FILE "$repo_root/fpga/gw5a/src/fpga_project.sdc"]

if {$part eq "GW5A-25A-MBGA121N-1"} {
  set part "GW5A-LV25MG121NC1/I0"
}
if {$part eq "GW5A-LV25MG121NC1/10"} {
  set part "GW5A-LV25MG121NC1/I0"
}

puts "=================================================================="
puts [format {[GOWIN] PROJ_NAME=%s} $proj_name]
puts [format {[GOWIN] OUT_ROOT=%s} $out_root]
puts [format {[GOWIN] TOP=%s} $top]
puts [format {[GOWIN] PART=%s DEVICE_VERSION=%s} $part $device_version]
puts [format {[GOWIN] FILELIST=%s} $filelist]
puts [format {[GOWIN] CST=%s} $cst_file]
puts [format {[GOWIN] SDC=%s} $sdc_file]
puts "=================================================================="

file mkdir $out_root
create_project -name $proj_name -dir $out_root -pn $part -device_version $device_version -force

set initmem_src_dir "$repo_root/build/fw"
set initmem_dst_dir "[pwd]/build/fw"
file mkdir $initmem_dst_dir
for {set lane 0} {$lane < 4} {incr lane} {
  set src "$initmem_src_dir/kws_xip_rt_boot_${lane}.memb"
  set dst "$initmem_dst_dir/kws_xip_rt_boot_${lane}.memb"
  if {![file exists $src]} {
    puts stderr [format {[ERROR] missing SRAM initmem lane: %s} $src]
    exit 2
  }
  file copy -force $src $dst
}

set_option -use_cpu_as_gpio 1
set_option -use_sspi_as_gpio 1
set_option -use_mspi_as_gpio 1
set_option -convert_sdp32_36_to_sdp16_18 [getenv_or CONVERT_SDP32_36_TO_SDP16_18 "0"]
set_option -top_module $top
set_option -output_base_name $proj_name

if {[info exists ::env(PLACE_OPTION)]} {
  set_option -place_option $::env(PLACE_OPTION)
}
if {[info exists ::env(ROUTE_OPTION)]} {
  set_option -route_option $::env(ROUTE_OPTION)
}

if {![file exists $filelist]} {
  puts stderr [format {[ERROR] filelist not found: %s} $filelist]
  exit 2
}

set fp [open $filelist r]
while {[gets $fp line] >= 0} {
  set line [string trim $line]
  if {$line eq ""} { continue }
  if {[string match "#*" $line]} { continue }
  if {[string index $line 0] ne "/"} {
    set line "$repo_root/$line"
  }
  if {![file exists $line]} {
    puts stderr [format {[ERROR] RTL file missing: %s} $line]
    exit 2
  }
  add_file $line
}
close $fp

if {![file exists $cst_file]} {
  puts stderr [format {[ERROR] CST not found: %s} $cst_file]
  exit 2
}
if {![file exists $sdc_file]} {
  puts stderr [format {[ERROR] SDC not found: %s} $sdc_file]
  exit 2
}
add_file $cst_file
add_file $sdc_file

run all

puts [format {[GOWIN] DONE. Project dir: %s} [pwd]]
exit
