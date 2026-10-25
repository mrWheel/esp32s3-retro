cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(retroHost VERSION 0.1.0)
if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" STREQUAL "6.0.2")
  message(FATAL_ERROR "Use native ESP-IDF 6.0.2 for this project")
endif()
if(NOT IDF_TARGET STREQUAL "esp32s3")
  message(FATAL_ERROR "Use idf.py set-target esp32s3")
endif()
#-- The system disks in littlefs/ are regenerated at configure time from bootDisks/<os>/systemDsk/.
#-- A change in those folders (or in the generator) triggers a new configure, so `idf.py build` is enough.
if(NOT DEFINED PYTHON)
  set(PYTHON python3)
endif()
set(APPLE2_SYSTEM_DISK_PROFILE "PRODOS_640K" CACHE STRING "Apple II system disk profile (PRODOS, PRODOS_640K, PRODOS_800K)")
set(systemDiskJobs
  "apple2|${APPLE2_SYSTEM_DISK_PROFILE}|system.dsk"
  "cpm80|LARGE|system.dsk"
  "cpm86|LARGE|system.dsk"
  "ucsd|PASCAL_800K|system.po")
foreach(job IN LISTS systemDiskJobs)
  string(REPLACE "|" ";" jobParts "${job}")
  list(GET jobParts 0 diskOs)
  list(GET jobParts 1 diskProfile)
  list(GET jobParts 2 diskFile)
  file(GLOB diskSources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/bootDisks/${diskOs}/systemDsk/*")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${diskSources})
  execute_process(
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/createSystemDsk.py --os ${diskOs} --profile ${diskProfile}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE diskResult
    OUTPUT_QUIET)
  if(NOT diskResult EQUAL 0)
    message(WARNING "createSystemDsk.py failed for ${diskOs}; littlefs/${diskOs}/${diskFile} was not updated")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/tools/createSystemDsk.py"
  "${CMAKE_SOURCE_DIR}/bootDisks/apple2/prodosEmpty.po"
  "${CMAKE_SOURCE_DIR}/assets/ucsd/ucsdDiskImages/Apple Pascal 1.3 3.5.po")
littlefs_create_partition_image(bootfs littlefs FLASH_IN_PROJECT)
