idf_component_register(
  SRCS "apple2Disk.c" "apple2Core.c"
  INCLUDE_DIRS "include" "third_party/chips"
  REQUIRES hostCore
  LDFRAGMENTS "linker.lf"
)

#-- The 6502 core is the hot path: always optimise for speed, not size.
#-- ESP-IDF defaults to -fno-jump-tables; the 256-way opcode switch needs a jump table.
set_source_files_properties(apple2Core.c PROPERTIES COMPILE_OPTIONS
  "-O2;-fjump-tables;-ftree-switch-conversion;-fno-bit-tests;--param=jump-table-max-growth-ratio-for-speed=2000")
