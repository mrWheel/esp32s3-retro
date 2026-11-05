# Build log for ESP-IDF

Generated: 2026-11-05T15:53:32

$ /Users/willema/.espressif/v6.0.2/esp-idf/tools/idf.py build
Executing action: all (aliases: build)
Running ninja in directory /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build
Executing "ninja all"...
[0/2] Re-checking globbed directories...
[1/6] cd /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build && /Users/willema/.espressif/tools/python/v6.0.2/venv/bin/python /Users/willema/.espressif/v6.0.2/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 partition --type app /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build/partition_table/partition-table.bin /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build/retroHost.bin
retroHost.bin binary size 0x147610 bytes. Smallest app partition is 0x300000 bytes. 0x1b89f0 bytes (57%) free.
[2/6] Performing build step for 'bootloader'
[1/1] cd /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build/bootloader && /Users/willema/.espressif/tools/python/v6.0.2/venv/bin/python /Users/willema/.espressif/v6.0.2/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 bootloader 0x0 /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build/bootloader/bootloader.bin
Bootloader binary size 0x5260 bytes. 0x2da0 bytes (36%) free.
[3/6] No install step for 'bootloader'
[4/6] Completed 'bootloader'
[5/6] cd /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build && /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build/littlefs_py_venv/bin/littlefs-python create /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/littlefs /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build/bootfs.bin -v --fs-size=0x800000 --name-max=64 --block-size=4096
LittleFS Configuration:
  Block Size:       4096  /  0x1000
  Image Size:    8388608  /  0x800000
  Block Count:      2048
  Name Max:           64
  Image:       /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build/bootfs.bin
Adding Directory: ucsd
Adding File:      .DS_Store
Adding Directory: swtpc
Adding Directory: cpm86
Adding Directory: cpm80
Adding Directory: help
Adding Directory: apple2
Adding File:      ucsd/drives.cfg
Adding File:      ucsd/system.po
Adding File:      ucsd/README.txt
Adding File:      swtpc/README.txt
Adding File:      cpm86/cpm.sys
Adding File:      cpm86/drives.cfg
Adding File:      cpm86/README.txt
Adding File:      cpm86/retro86bios.h86
Adding File:      cpm86/system.dsk
Adding File:      cpm80/drives.cfg
Adding File:      cpm80/README.txt
Adding File:      cpm80/system.dsk
Adding File:      help/computer-guest-upload.html
Adding File:      help/manage-images.html
Adding File:      help/home.html
Adding File:      help/computer-image-upload.html
Adding File:      help/file-guest-copy.html
Adding File:      help/image-computer-download.html
Adding File:      help/guest-image-copy.html
Adding File:      help/guest-computer-download.html
Adding File:      help/image-image-copy.html
Adding File:      help/guest-file-manage.html
Adding File:      apple2/apple2e.rom
Adding File:      apple2/drives.cfg
Adding File:      apple2/README.txt
Adding File:      apple2/system.dsk
Adding File:      apple2/apple2.rom

Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool --chip esp32s3 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/retroHost.bin 0x310000 build/bootfs.bin
or from the "/Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build" directory
 python -m esptool --chip esp32s3 -b 460800 --before default-reset --after hard-reset write-flash "@flash_args"
Warning: Failed to load entry point extension "ota_manager_ext": No module named 'ota_manager_tools'

Source manifest: /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/build/flasher_args.json
