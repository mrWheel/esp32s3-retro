idf_component_register(
  SRCS "cpm80Cpu.c" "cpm80Guest.c"
  INCLUDE_DIRS "include"
  REQUIRES z80 hostCore
)
