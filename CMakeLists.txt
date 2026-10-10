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
idf_build_get_property(pythonExecutable PYTHON)
file(GLOB apple2SystemDskSources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/bootDisks/apple2/systemDsk/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${apple2SystemDskSources}
  "${CMAKE_SOURCE_DIR}/bootDisks/apple2/prodosEmpty.po"
  "${CMAKE_SOURCE_DIR}/tools/diskImageApple2Prodos.py"
  "${CMAKE_SOURCE_DIR}/tools/createSystemDsk.py"
  "${CMAKE_SOURCE_DIR}/tools/apple2Basic.py")
execute_process(
  COMMAND ${pythonExecutable} "${CMAKE_SOURCE_DIR}/tools/createSystemDsk.py" --os apple2 --profile PRODOS
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  RESULT_VARIABLE apple2SystemDskResult
  OUTPUT_VARIABLE apple2SystemDskOutput
  ERROR_VARIABLE apple2SystemDskOutput)
if(NOT apple2SystemDskResult EQUAL 0)
  message(FATAL_ERROR "Creating littlefs/apple2/system.dsk failed:\n${apple2SystemDskOutput}")
endif()
littlefs_create_partition_image(bootfs littlefs FLASH_IN_PROJECT)
