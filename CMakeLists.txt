cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(retroHost VERSION 0.1.0)
if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" STREQUAL "6.0.2")
  message(FATAL_ERROR "Use native ESP-IDF 6.0.2 for this project")
endif()
if(NOT IDF_TARGET STREQUAL "esp32s3")
  message(FATAL_ERROR "Use idf.py set-target esp32s3")
endif()
#-- littlefs/apple2/system.dsk is rebuilt from bootDisks/apple2/systemDsk whenever a source file, the builder or the base image changes.
#-- The Apple II system disk is a ProDOS 8 boot volume.
set(APPLE2_SYSTEM_DISK_PROFILE "PRODOS" CACHE STRING "Apple II LittleFS system disk profile")
set_property(CACHE APPLE2_SYSTEM_DISK_PROFILE PROPERTY STRINGS PRODOS PRODOS_640K PRODOS_800K)
set(apple2SystemDiskProfiles PRODOS PRODOS_640K PRODOS_800K)
if(NOT APPLE2_SYSTEM_DISK_PROFILE IN_LIST apple2SystemDiskProfiles)
  message(FATAL_ERROR
    "APPLE2_SYSTEM_DISK_PROFILE must be one of: ${apple2SystemDiskProfiles}; got '${APPLE2_SYSTEM_DISK_PROFILE}'")
endif()
idf_build_get_property(pythonExecutable PYTHON)
file(GLOB apple2SystemDskSources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/bootDisks/apple2/systemDsk/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${apple2SystemDskSources}
  "${CMAKE_SOURCE_DIR}/bootDisks/apple2/prodosEmpty.po"
  "${CMAKE_SOURCE_DIR}/tools/include/apple2Basic.py"
  "${CMAKE_SOURCE_DIR}/tools/include/diskImageApple2Prodos.py"
  "${CMAKE_SOURCE_DIR}/tools/include/diskImageCommon.py"
  "${CMAKE_SOURCE_DIR}/tools/include/diskImageCpm.py"
  "${CMAKE_SOURCE_DIR}/tools/include/diskImageCpm80.py"
  "${CMAKE_SOURCE_DIR}/tools/include/diskImageCpm86.py"
  "${CMAKE_SOURCE_DIR}/tools/include/buildDiskImageApple2.py"
  "${CMAKE_SOURCE_DIR}/tools/include/buildDiskImageCpm80.py"
  "${CMAKE_SOURCE_DIR}/tools/createSystemDsk.py")
execute_process(
  COMMAND ${pythonExecutable} "${CMAKE_SOURCE_DIR}/tools/createSystemDsk.py"
    --os apple2 --profile ${APPLE2_SYSTEM_DISK_PROFILE}
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  RESULT_VARIABLE apple2SystemDskResult
  OUTPUT_VARIABLE apple2SystemDskOutput
  ERROR_VARIABLE apple2SystemDskOutput)
if(NOT apple2SystemDskResult EQUAL 0)
  message(FATAL_ERROR "Creating littlefs/apple2/system.dsk failed:\n${apple2SystemDskOutput}")
endif()
littlefs_create_partition_image(bootfs littlefs FLASH_IN_PROJECT)
