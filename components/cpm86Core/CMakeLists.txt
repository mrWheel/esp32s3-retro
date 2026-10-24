idf_component_register(
  SRCS
    "cpm86Core.c"
    "tiny8086/tiny8086.c"
  INCLUDE_DIRS "include" "tiny8086"
  REQUIRES hostCore
  LDFRAGMENTS "linker.lf"
)

#-- The interpreter loop is the hot path: always optimise for speed, not size.
#-- ESP-IDF defaults to -fno-jump-tables; the 256-way opcode switch needs a jump table.
#-- With the noflash mapping the tables live in internal RAM, so this is cache-safe.
set_source_files_properties(tiny8086/tiny8086.c PROPERTIES COMPILE_OPTIONS
  "-O2;-fjump-tables;-ftree-switch-conversion;-fno-bit-tests;--param=jump-table-max-growth-ratio-for-speed=2000")
