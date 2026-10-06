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
  ../components/z80/z80.c
  ../main/cpm80DriveConfig.c
  ../main/imageFile.c
)
target_include_directories(hostTests PRIVATE ../components/hostCore/include ../components/cpm80Core/include
  ../components/z80 ../main)
target_compile_options(hostTests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_compile_definitions(hostTests PRIVATE CPM80_SYSTEM_IMAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/cpm80/system.dsk")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hostTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hostTests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME hostCore COMMAND hostTests)
