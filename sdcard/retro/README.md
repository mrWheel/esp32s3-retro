# Retro SD-card workspace

This folder groups the SD-card storage used by the retro environments. It provides a common location for persistent guest disks while keeping each environment’s storage separate.

Use [images](images/README.md) for disks intended to be opened by the emulators and [backups](backups/README.md) for manually retained recovery copies. A disk becomes a guest drive only when the relevant drive configuration maps it.

The card-preparation tool also prepares `backup` (singular) and exchange directories required by the firmware’s layout checks. The existing `backups` collection is separate and does not replace that required `backup` directory.
