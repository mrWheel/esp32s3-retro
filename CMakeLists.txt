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
set(CPM86_SYSTEM_DISK_PROFILE "CPM86_1440K" CACHE STRING "CP/M-86 system disk profile (CPM86_160K, CPM86_516K, CPM86_1440K, CPM86_8196K); keep it equal to the A: identifier in littlefs/cpm86/drives.cfg")
set(CPM80_SYSTEM_DISK_PROFILE "CPM80_1440K" CACHE STRING "CP/M-80 system disk profile (CPM80_256K, CPM80_512K, CPM80_1440K); keep it equal to the A: identifier in littlefs/cpm80/drives.cfg")
#-- Stale build-tree caches from before the drives.cfg identifier rename still hold the
#-- retired SMALL/LARGE/BIG names; those cache entries are replaced by the current defaults.
set(LEGACY_CPM80_SYSTEM_PROFILES SMALL LARGE)
set(LEGACY_CPM86_SYSTEM_PROFILES SMALL LARGE BIG)
if(CPM80_SYSTEM_DISK_PROFILE IN_LIST LEGACY_CPM80_SYSTEM_PROFILES)
  message(STATUS "Ignoring stale CPM80_SYSTEM_DISK_PROFILE cache value '${CPM80_SYSTEM_DISK_PROFILE}'; using CPM80_1440K")
  set(CPM80_SYSTEM_DISK_PROFILE "CPM80_1440K" CACHE STRING "CP/M-80 system disk profile" FORCE)
endif()
if(CPM86_SYSTEM_DISK_PROFILE IN_LIST LEGACY_CPM86_SYSTEM_PROFILES)
  message(STATUS "Ignoring stale CPM86_SYSTEM_DISK_PROFILE cache value '${CPM86_SYSTEM_DISK_PROFILE}'; using CPM86_1440K")
  set(CPM86_SYSTEM_DISK_PROFILE "CPM86_1440K" CACHE STRING "CP/M-86 system disk profile" FORCE)
endif()
if(NOT CPM80_SYSTEM_DISK_PROFILE MATCHES "^CPM80_(256K|512K|1440K)$")
  message(FATAL_ERROR "CPM80_SYSTEM_DISK_PROFILE='${CPM80_SYSTEM_DISK_PROFILE}' is not a drives.cfg identifier; configure with -DCPM80_SYSTEM_DISK_PROFILE=CPM80_1440K")
endif()
if(NOT CPM86_SYSTEM_DISK_PROFILE MATCHES "^CPM86_(160K|516K|1440K|8196K)$")
  message(FATAL_ERROR "CPM86_SYSTEM_DISK_PROFILE='${CPM86_SYSTEM_DISK_PROFILE}' is not a drives.cfg identifier; configure with -DCPM86_SYSTEM_DISK_PROFILE=CPM86_1440K")
endif()
set(systemDiskJobs
  "apple2|${APPLE2_SYSTEM_DISK_PROFILE}|system.dsk"
  "cpm80|${CPM80_SYSTEM_DISK_PROFILE}|system.dsk"
  "cpm86|${CPM86_SYSTEM_DISK_PROFILE}|system.dsk"
  "ucsd|PASCAL_800K|system.po")
foreach(job IN LISTS systemDiskJobs)
  string(REPLACE "|" ";" jobParts "${job}")
  list(GET jobParts 0 diskOs)
  list(GET jobParts 1 diskProfile)
  list(GET jobParts 2 diskFile)
  file(GLOB diskSources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/bootDisks/${diskOs}/systemDsk/*")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${diskSources})
  execute_process(
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/diskManager.py --type SYSTEMDSK --yes --os ${diskOs} --profile ${diskProfile}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE diskResult
    OUTPUT_QUIET)
  if(NOT diskResult EQUAL 0)
    message(WARNING "diskManager.py failed for ${diskOs}; littlefs/${diskOs}/${diskFile} was not updated")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/tools/diskManager.py"
  "${CMAKE_SOURCE_DIR}/tools/include/systemDisk.py"
  "${CMAKE_SOURCE_DIR}/bootDisks/apple2/prodosEmpty.po"
  "${CMAKE_SOURCE_DIR}/assets/ucsd/ucsdDiskImages/Apple Pascal 1.3 3.5.po")
littlefs_create_partition_image(bootfs littlefs FLASH_IN_PROJECT)
