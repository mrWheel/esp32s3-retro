idf_component_register(
  SRCS "cpmCpu.c" "cpmGuest.c"
  INCLUDE_DIRS "include"
  REQUIRES z80 hostCore
)
