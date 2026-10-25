cmake_minimum_required(VERSION 3.16)
project(retroHostTests C)
set(CMAKE_C_STANDARD 11)
enable_testing()
add_executable(
  hostTests
  hostTests.c
  ../components/hostCore/hostCore.c
  ../components/cpm80Core/cpm80Cpu.c
  ../components/cpm80Core/cpm80Guest.c
  ../components/cpm86Core/cpm86Core.c
  ../components/cpm86Core/tiny8086/tiny8086.c
  ../components/apple2Core/apple2Core.c
  ../components/apple2Core/apple2Disk.c
  ../components/z80/z80.c
  ../main/cpm80DriveConfig.c
  ../main/cpm86DriveConfig.c
  ../main/cpm86BiosOverlay.c
  ../main/storagePath.c
  ../main/imageFile.c
  ../main/apple2Keyboard.c
)
target_include_directories(hostTests PRIVATE ../components/hostCore/include ../components/cpm80Core/include
  ../components/cpm86Core/include ../components/cpm86Core/tiny8086 ../components/apple2Core/include
  ../components/apple2Core ../components/apple2Core/third_party/chips ../components/z80 ../main)
target_compile_options(hostTests PRIVATE -Wall -Wextra -Werror -UNDEBUG -funsigned-char)
target_compile_definitions(
  hostTests PRIVATE PROJECT_ROOT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/.."
  CPM80_SYSTEM_IMAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/cpm80/system.dsk"
  CPM86_SYSTEM_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/cpm86/cpm.sys"
  CPM86_SYSTEM_DISK_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/cpm86/system.dsk"
  CPM86_BIOS_OVERLAY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/cpm86/retro86bios.h86"
  APPLE2_TEST_ROM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/apple2-diagnostic.rom"
  APPLE2_SYSTEM_ROM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../assets/apple2/apple2_roms/apple.rom")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hostTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hostTests PRIVATE -fsanitize=address,undefined)
endif()
add_executable(
  apple2DiskTests
  apple2DiskTests.c
  ../components/hostCore/hostCore.c
  ../components/apple2Core/apple2Core.c
  ../components/apple2Core/apple2Disk.c
  ../main/apple2DiskImage.c
  ../main/imageFile.c
)
target_include_directories(apple2DiskTests PRIVATE ../components/hostCore/include ../components/apple2Core/include ../components/apple2Core
  ../components/apple2Core/third_party/chips ../main)
target_compile_options(apple2DiskTests PRIVATE -Wall -Wextra -Werror -UNDEBUG -funsigned-char)
target_compile_definitions(
  apple2DiskTests PRIVATE APPLE2_SYSTEM_ROM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../assets/apple2/apple2_roms/apple.rom"
  APPLE2_SYSTEM_DISK_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/apple2/system.dsk")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(apple2DiskTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(apple2DiskTests PRIVATE -fsanitize=address,undefined)
endif()
add_executable(
  apple2MultiDiskTests
  apple2MultiDiskTests.c
  ../components/apple2Core/apple2Core.c
  ../components/apple2Core/apple2Disk.c
  ../main/apple2DiskImage.c
  ../main/apple2DriveConfig.c
  ../main/storagePath.c
  ../main/imageFile.c
)
target_include_directories(apple2MultiDiskTests PRIVATE ../components/hostCore/include ../components/apple2Core/include ../components/apple2Core
  ../components/apple2Core/third_party/chips ../main)
target_compile_options(apple2MultiDiskTests PRIVATE -Wall -Wextra -Werror -UNDEBUG -funsigned-char)
target_compile_definitions(apple2MultiDiskTests PRIVATE PROJECT_ROOT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/..")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(apple2MultiDiskTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(apple2MultiDiskTests PRIVATE -fsanitize=address,undefined)
endif()
add_executable(
  apple2Fast6502Tests
  apple2Fast6502Tests.c
  ../components/hostCore/hostCore.c
  ../components/apple2Core/apple2Disk.c
)
target_include_directories(apple2Fast6502Tests PRIVATE ../components/hostCore/include ../components/apple2Core/include ../components/apple2Core
  ../components/apple2Core/third_party/chips ../main)
target_compile_options(apple2Fast6502Tests PRIVATE -Wall -Wextra -Werror -UNDEBUG -funsigned-char)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(apple2Fast6502Tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(apple2Fast6502Tests PRIVATE -fsanitize=address,undefined)
endif()
add_executable(cpm80TimerTests cpm80TimerTests.c ../components/cpm80Core/cpm80Cpu.c ../components/cpm80Core/cpm80Guest.c
  ../components/z80/z80.c)
target_include_directories(cpm80TimerTests PRIVATE ../components/hostCore/include ../components/cpm80Core/include ../components/z80)
target_compile_options(cpm80TimerTests PRIVATE -Wall -Wextra -Werror -UNDEBUG -funsigned-char)
target_compile_definitions(cpm80TimerTests PRIVATE BENCH80_COM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../assets/cpm80/BENCH80/BENCH80.COM")
add_test(NAME apple2Fast6502 COMMAND apple2Fast6502Tests)
add_test(NAME cpm80Timer COMMAND cpm80TimerTests)
add_test(NAME hostCore COMMAND hostTests)
add_test(NAME apple2Core COMMAND hostTests --apple2-core)
add_test(NAME apple2LanguageCard COMMAND hostTests --apple2-language-card)
add_test(NAME apple2VidexCard COMMAND hostTests --apple2-videx-card)
add_test(NAME apple2SystemRom COMMAND hostTests --apple2-system-rom)
add_test(NAME apple2Disk COMMAND apple2DiskTests)
add_test(NAME apple2DiskStream COMMAND apple2DiskTests --disk-stream)
add_test(NAME apple2MultiDisk COMMAND apple2MultiDiskTests)
add_test(NAME ucsdPascalBoot COMMAND apple2DiskTests --pascal-boot
  "${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/ucsd/ApplePascal1-680-0004-01.dsk")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/ucsd/ApplePascal1-140K.dsk"
  AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/apple2/apple2e.rom"
  AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sdcard/retro/images/ucsd/pascalWork2.po"
  AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sdcard/retro/images/ucsd/pascalWork1.po"
  AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sdcard/retro/images/ucsd/ApplePascal2-640K.po")
  add_test(NAME ucsdPascalIIeTextOpen COMMAND apple2DiskTests --pascal-iie-editor-open
    "${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/ucsd/ApplePascal1-140K.dsk"
    "${CMAKE_CURRENT_SOURCE_DIR}/../littlefs/apple2/apple2e.rom"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sdcard/retro/images/ucsd/pascalWork2.po"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sdcard/retro/images/ucsd/pascalWork1.po"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sdcard/retro/images/ucsd/ApplePascal2-640K.po")
endif()
add_test(NAME apple2ProdosSystemImage COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/createSystemDsk.py --os apple2
  --profile PRODOS --output ${CMAKE_CURRENT_BINARY_DIR}/prodosSystem.dsk)
add_test(NAME apple2ProdosDataImage COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/buildDiskImage.py
  --os apple2 --profile 800K --name DATA800 --output ${CMAKE_CURRENT_BINARY_DIR}/prodosData800.po)
set_tests_properties(apple2ProdosSystemImage apple2ProdosDataImage PROPERTIES FIXTURES_SETUP prodosImages)
add_test(NAME apple2ProdosAutostart COMMAND apple2DiskTests --prodos-autostart-boot
  ${CMAKE_CURRENT_BINARY_DIR}/prodosSystem.dsk ${CMAKE_CURRENT_BINARY_DIR}/prodosData800.po)
add_test(NAME apple2ProdosAutostart80Columns COMMAND apple2DiskTests --prodos-80col-boot
  ${CMAKE_CURRENT_BINARY_DIR}/prodosSystem.dsk ${CMAKE_CURRENT_BINARY_DIR}/prodosData800.po)
add_test(NAME apple2ProdosSmartPort COMMAND apple2DiskTests --prodos-smartport-boot
  "${CMAKE_CURRENT_SOURCE_DIR}/../assets/apple2Downloads/appleDisks/ProDOS 2.4.2.dsk" ${CMAKE_CURRENT_BINARY_DIR}/prodosData800.po)
set_tests_properties(apple2ProdosAutostart apple2ProdosAutostart80Columns apple2ProdosSmartPort PROPERTIES FIXTURES_REQUIRED prodosImages)
set(guestFsSources
  ../components/guestFs/guestFs.c
  ../components/guestFs/guestFsCpm.c
  ../components/guestFs/guestFsApple.c
  ../components/guestFs/guestFsProdos.c
  ../components/guestFs/guestFsPascal.c
  ../components/guestFs/guestFsDos33.c
  ../components/guestFs/guestFsConvert.c
  ../main/guestFsImage.c
  ../main/imageFile.c
)
function(addGuestFsExecutable targetName sourceFile)
  add_executable(${targetName} ${sourceFile} ${guestFsSources})
  target_include_directories(${targetName} PRIVATE ../components/guestFs/include ../components/guestFs ../main)
  target_compile_options(${targetName} PRIVATE -Wall -Wextra -Werror -UNDEBUG -funsigned-char)
  target_compile_definitions(${targetName} PRIVATE _DEFAULT_SOURCE GUESTFS_FAULT_INJECTION)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${targetName} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${targetName} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
set(guestFsWork ${CMAKE_CURRENT_BINARY_DIR}/guestFsWork)
#-- Registers <prefix>Fixtures (python builds images), <prefix> (C test) and <prefix>PythonVerify (python checks C output).
function(addGuestFsSuite prefix targetName fixtureScript)
  set(work ${CMAKE_CURRENT_BINARY_DIR}/${prefix}Work)
  add_test(NAME ${prefix}Fixtures COMMAND sh -c
    "rm -rf '${work}' && mkdir -p '${work}/fixtures' '${work}/out' && python3 '${CMAKE_CURRENT_SOURCE_DIR}/${fixtureScript}' build '${work}/fixtures' && python3 '${CMAKE_CURRENT_SOURCE_DIR}/${fixtureScript}' real '${work}/fixtures'")
  set_tests_properties(${prefix}Fixtures PROPERTIES FIXTURES_SETUP ${prefix}Fixtures)
  add_test(NAME ${prefix} COMMAND ${targetName} ${work}/fixtures ${work}/out ${CMAKE_CURRENT_SOURCE_DIR}/..)
  set_tests_properties(${prefix} PROPERTIES FIXTURES_REQUIRED ${prefix}Fixtures FIXTURES_SETUP ${prefix}Output)
  add_test(NAME ${prefix}PythonVerify COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/${fixtureScript} verify ${work}/out)
  set_tests_properties(${prefix}PythonVerify PROPERTIES FIXTURES_REQUIRED ${prefix}Output)
endfunction()
addGuestFsExecutable(guestFsTests guestFsTests.c)
add_test(NAME guestFsFixtures COMMAND sh -c
  "rm -rf '${guestFsWork}' && mkdir -p '${guestFsWork}/fixtures' '${guestFsWork}/out' && python3 '${CMAKE_CURRENT_SOURCE_DIR}/guestFsFixture.py' build '${guestFsWork}/fixtures' && python3 '${CMAKE_CURRENT_SOURCE_DIR}/guestFsFixture.py' real '${guestFsWork}/fixtures'")
set_tests_properties(guestFsFixtures PROPERTIES FIXTURES_SETUP guestFsFixtures)
add_test(NAME guestFsCpm COMMAND guestFsTests ${guestFsWork}/fixtures ${guestFsWork}/out ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_tests_properties(guestFsCpm PROPERTIES FIXTURES_REQUIRED guestFsFixtures FIXTURES_SETUP guestFsOutput)
add_test(NAME guestFsPythonVerify COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/guestFsFixture.py verify ${guestFsWork}/out)
set_tests_properties(guestFsPythonVerify PROPERTIES FIXTURES_REQUIRED guestFsOutput)
addGuestFsExecutable(guestFsProdosTests guestFsProdosTests.c)
addGuestFsSuite(guestFsProdos guestFsProdosTests guestFsProdosFixture.py)
addGuestFsExecutable(guestFsPascalTests guestFsPascalTests.c)
addGuestFsSuite(guestFsPascal guestFsPascalTests guestFsPascalFixture.py)
addGuestFsExecutable(guestFsDos33Tests guestFsDos33Tests.c)
addGuestFsSuite(guestFsDos33 guestFsDos33Tests guestFsDos33Fixture.py)
add_executable(guestFsAppleOrderTests guestFsAppleOrderTests.c ${guestFsSources} ../main/apple2DiskImage.c)
target_include_directories(guestFsAppleOrderTests PRIVATE ../components/guestFs/include ../components/guestFs ../main)
target_compile_options(guestFsAppleOrderTests PRIVATE -Wall -Wextra -Werror -UNDEBUG -funsigned-char)
target_compile_definitions(guestFsAppleOrderTests PRIVATE _DEFAULT_SOURCE)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(guestFsAppleOrderTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(guestFsAppleOrderTests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME guestFsAppleOrder COMMAND guestFsAppleOrderTests ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_executable(guestFsConvertTests guestFsConvertTests.c ../components/guestFs/guestFsConvert.c)
target_include_directories(guestFsConvertTests PRIVATE ../components/guestFs/include)
target_compile_options(guestFsConvertTests PRIVATE -Wall -Wextra -Werror -UNDEBUG -funsigned-char)
add_test(NAME guestFsConvert COMMAND guestFsConvertTests)
