cmake_minimum_required(VERSION 3.16)
project(retroHostTests C)
set(CMAKE_C_STANDARD 11)
enable_testing()
add_executable(
  hostTests
  hostTests.c
  ../components/hostCore/hostCore.c
  ../components/hostCore/hostExchange.c
  ../components/cpm80Core/cpm80Cpu.c
  ../components/cpm80Core/cpm80Guest.c
  ../components/cpm86Core/cpm86Core.c
  ../components/cpm86Core/third_party/emu86/emu-mem-io.c
  ../components/cpm86Core/third_party/emu86/emu-proc.c
  ../components/cpm86Core/third_party/emu86/op-class.c
  ../components/cpm86Core/third_party/emu86/op-common.c
  ../components/cpm86Core/third_party/emu86/op-exec.c
  ../components/cpm86Core/third_party/emu86/op-id-name.c
  ../components/cpm86Core/third_party/emu86/op-print-intel.c
  ../components/z80/z80.c
  ../main/cpm80DriveConfig.c
  ../main/cpm86DriveConfig.c
  ../main/cpm86BiosOverlay.c
  ../main/storagePath.c
  ../main/imageFile.c
)
target_include_directories(hostTests PRIVATE ../components/hostCore/include ../components/cpm80Core/include
  ../components/cpm86Core/include ../components/cpm86Core/third_party/emu86 ../components/z80 ../main)
target_compile_options(hostTests PRIVATE -Wall -Wextra -Werror -UNDEBUG -funsigned-char)
target_compile_definitions(
  hostTests PRIVATE PROJECT_ROOT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/.."
  CPM80_SYSTEM_IMAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/cpm80/system.dsk"
  CPM86_SYSTEM_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/cpm86/cpm.sys"
  CPM86_SYSTEM_DISK_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/cpm86/system.dsk"
  CPM86_BIOS_OVERLAY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/cpm86/retro86bios.h86")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hostTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hostTests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME hostCore COMMAND hostTests)
