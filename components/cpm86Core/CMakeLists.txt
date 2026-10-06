set(EMU86_DIR "third_party/emu86")

idf_component_register(
  SRCS
    "cpm86Core.c"
    "${EMU86_DIR}/emu-mem-io.c"
    "${EMU86_DIR}/emu-proc.c"
    "${EMU86_DIR}/op-class.c"
    "${EMU86_DIR}/op-common.c"
    "${EMU86_DIR}/op-exec.c"
    "${EMU86_DIR}/op-id-name.c"
    "${EMU86_DIR}/op-print-intel.c"
  INCLUDE_DIRS "include" "${EMU86_DIR}"
  REQUIRES hostCore
)
