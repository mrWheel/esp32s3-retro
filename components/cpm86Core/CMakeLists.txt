idf_component_register(
  SRCS
    "cpm86Core.c"
    "tiny8086/tiny8086.c"
  INCLUDE_DIRS "include" "tiny8086"
  REQUIRES hostCore
)

#-- The interpreter loop is the hot path: always optimise for speed, not size.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
