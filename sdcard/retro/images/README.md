# Active virtual-disk storage

This folder is the SD-card location for virtual disks used by the emulators and managed through File Transfer. It gives guest systems persistent storage while keeping each environment’s disks in its own subfolder.

Place a disk directly in the appropriate system subfolder and assign it through that system’s drive configuration. Copying an image here alone does not mount it. Guest writes to a writable mounted image change this working copy, so retain a separate recovery copy before experimenting.
