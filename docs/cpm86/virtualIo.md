# RETRO86_V1 CP/M-86 virtual I/O

## Scope

This is the byte-port contract used by `components/cpm86Core/bios/retro86bios.a86`
and `main/cpm86Machine.c`. Port numbers are 16-bit 8086 I/O addresses; the BIOS
uses byte `IN` and `OUT` instructions. Disk operations complete synchronously.
There is no interrupt-driven or asynchronous device completion.

The F8h/F9h host-exchange ports are reserved by the CPU adapter and are not part
of this BIOS contract. Access to any unmapped port is a guest I/O error.

## Console

| Port | Direction | Contract |
|---|---|---|
| E0h | IN | Returns FFh when a console byte is available; otherwise 00h. Does not consume input. |
| E1h | IN | Consumes and returns one byte. The BIOS reads this only after E0h reports input. |
| E2h | OUT | Writes the low byte to the shared host console. |

The guest BIOS polls E0h while waiting for input. The host scheduler regains
control after each bounded CPU instruction batch.

## Drive A: disk

The current implementation exposes one 160 KiB raw image as drive A:. Disk
records are 128 bytes; each track has 32 records. Values are assembled
little-endian from the low/high port pairs.

| Port | Direction | Contract |
|---|---|---|
| E8h | OUT | Select drive number. Only 0 (A:) is valid. |
| E9h / EAh | OUT | Set track number, low byte then high byte. |
| EBh / ECh | OUT | Set 128-byte record number within the track, low byte then high byte; valid range 0–31. |
| EDh | OUT | Start transfer: 00h reads one record; 01h writes one record. Other values fail. |
| EDh | IN | Returns 00h on success or 01h on failure. |
| EEh | IN | Reads the next byte of a successful read transfer. |
| EEh | OUT | Supplies the next byte of a write transfer. Exactly 128 bytes commit the record. |

Every transfer validates drive A:, raw track 1–39, record 0–31 and the complete
record range against the image size. Invalid coordinates, short I/O, and
flush/sync failures set the transfer status to 01h. Reads and writes transfer
exactly 128 bytes; accessing EEh outside an active transfer is an unmapped-I/O
error.

The raw image has 40 tracks, eight 512-byte physical sectors per track, and
163,840 bytes total. Raw track 0 is reserved. CP/M-86's DPB uses OFF=1, so the
BIOS sends the guest track unchanged; adding another track would skip the
filesystem's first track. Record byte offset is:

```text
((track * 32) + record) * 128
```

The image is opened read/write. Completed record writes are flushed and synced
before reporting successful completion.

## Reset and ownership

Each machine initialization clears drive, track, record and transfer state.
The machine owns its open disk handle and closes it when guest execution stops.
There is no virtual-device reset command and no promise of crash-atomic
multi-record filesystem updates. Physical reset or power loss can interrupt a
guest metadata update.
