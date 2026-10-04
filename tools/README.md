# Host tools

`prepareSd.py` (Python 3.9+) adds the documented layout to an already formatted/mounted FAT32 card. It does not format, delete, make disk images or rewrite an incompatible layout marker. Run with the card mount root as its sole argument.

`transferTest.py` exercises an actual running device using the session URL displayed on USB. It creates uniquely named files in exchange/common and removes only those files. Do not run it during manual transfers. Its 8 MiB payload intentionally exceeds normal ESP32-S3 internal RAM; the test PC can hold it in memory, while the device must stream.

Portable parser/path/image tests are in `tests/`, built separately with CMake. They do not establish physical SD/USB/WiFi correctness.
