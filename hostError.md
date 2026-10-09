# HOST utility: error log and attempt register

Purpose of this document: one place that records what the problem is, what has
already been tried, what the result was, and where to look next. Every new
attempt MUST be appended to the "Attempt register" before or right after it is
made, so that we do not repeat work. Never delete entries; mark them
superseded instead.

Do not record hash codes (SHA-256 etc.) of binaries or images here.

## 1. Problem statement

The guest utility HOST (CP/M-80 `HOST.COM`, CP/M-86 `HOST.CMD`) transfers files
between the CP/M drive and the exchange directory on the SD card
(`/microSD/retro/exchange/<os>`), using ports F8h (data) and F9h (abort).

Reported symptoms on the real device (CP/M-86, run from drive E:):

| # | Command | Observed |
|---|---------|----------|
| S1 | `host put *.cmd` | `PUT ASM86.CMD: .... transfer failed (I/O or checksum error).` |
| S2 | same run | `PUT ED.CMD: .... complete (no valid HST metadata; complete records sent).` (works) |
| S3 | same run | `PUT GENCMD.CMD:  ..` and then nothing more (hang or stall). Not explained yet. |
| S4 | `host put host.lst` | `PUT HOST.LST: ...... transfer failed (I/O or checksum error).` |
| S5 | `host put host.lst` | The RGB LED blinks red for minutes before the first dot appears. Not explained yet. |

Drive E: listing at the time (from `STAT *.*`): ASM86.CMD 205 recs / 2 FCBs,
HOST.LST 869 recs / 7 FCBs, HOST.A86 436 recs / 4 FCBs; ED.CMD 74 recs / 1 FCB,
GENCMD.CMD 45 recs / 1 FCB, PIP.CMD 59 recs / 1 FCB. Observation by the user:
files that use more than one FCB (directory extent) fail.

## 2. Facts established

- The message "transfer failed (I/O or checksum error)" is `fileError = 6`. In
  the PUT path it is set when a BDOS sequential read fails, when `sendByte` or
  `readByte` is aborted by the console poll, or in `putTransferFailed`. It does
  NOT mean that a CRC mismatch was detected: a CRC mismatch is reported by the
  host as a status byte and shown as "exchange I/O or checksum error"
  (`fileError = 14`).
- Historical note: the fixture in `tests/hostTests.c` (`testCpm86Boot`) used to
  run the INSTALLED `A:HOST.CMD` from a separate small-image fixture, not the
  freshly assembled one. The test now uses the existing
  `littlefs/cpm86/system.dsk` image; no separate small image is required.
- Desktop reproduction (2026-10-08): copy ASM86.CMD, ED.CMD, GENCMD.CMD,
  PIP.CMD to E: with `A:PIP E:=A:<name>`, then `A:HOST PUT *.CMD O`. ASM86.CMD
  failed after 4 dots exactly like on the device; ED, GENCMD, PIP and HOST.CMD
  (single extent) succeeded. GENCMD did NOT stall on the desktop.
- With temporary instrumentation (read failure reported as fileError 7) the
  desktop failure of ASM86.CMD became "local CP/M-86 file I/O failed", i.e. the
  BDOS read failed, not the CRC and not the console poll.

## 3. Root cause found for S1 and S4 (desktop-confirmed)

`scanSource` reads the whole file to compute the CRCs and closes it. At that
point `workFcb` still holds the extent number (EX, offset 12) of the LAST
extent. `putAccepted` then re-opens the same FCB (BDOS 15) without clearing
EX/S2, so the file is opened at its last extent and the transfer starts with
the wrong records. The read loop runs out of records of that extent (the first
read past it fails) long before `remaining` reaches zero, so the BDOS read
returns an error and the transfer is aborted. Single-extent files are not
affected because their only extent is extent 0. The failing point (between
records 64 and 79 for ASM86.CMD) matches the number of records in the second
extent.

Fix applied in `guest/cpm86/host/HOST.A86` (label `putAccepted`): clear
`workFcb+12` (EX) and `workFcb+14` (S2) before the open. Result on the desktop:
`PUT ASM86.CMD: ............ complete (...)` (12 dots) and the bytes in the
exchange directory are identical to `bootDisks/cpm86/systemDsk/*.CMD`.

Additional change in the same file: a BDOS read failure during PUT is now
reported as "local CP/M-86 file I/O failed" (fileError 7) instead of the
generic "transfer failed" message; `putFailed` keeps an already set error code.

## 4. Open points (NOT solved or NOT verified)

- O1. Hardware verification of the EX fix: nothing has been flashed or tested
  on the device (the user uploads the LittleFS image manually). S1 and S4 must
  be re-tested there with the new `HOST.CMD`.
- O2. S3 (GENCMD.CMD stalls after 2 dots): GENCMD.CMD is a single-extent file
  (45 records), so the EX bug does not explain it. It may be a consequence of the
  previous failed file (state left behind after the aborted ASM86 transfer,
  for example an open BDOS file or host-side state after the abort on F9h), or a
  device-only effect (SD write latency, task watchdog, exchange timeout). Retest
  after the EX fix first; if it still stalls, investigate the host side
  (`components/hostCore/hostExchange.c`) and the device exchange timing.
- O3. S5 (red LED for minutes before the first dot): the first dot is printed
  only after 16 records have been SENT; before that `scanSource` reads the
  complete file once to compute both CRCs, and nothing is printed during that
  pass. For HOST.LST (869 records) that is a long silent read pass on the SD
  based image. Candidates: print progress during the scan pass; make the scan
  cheaper; check whether the red colour means read or write in
  `diskActivity*` (red may be shared for read and write; the BDOS close after a
  read may also cause a write). Not investigated yet.
- O4. The same extent/FCB reuse pattern must be checked in the CP/M-80 HOST
  (`guest/cpm80/host/HOST.ASM`). The desktop CP/M-80 round trip of 70000 bytes
  passed, but that test does not re-open a file after a scan pass at a later
  extent; verify explicitly.
- O5. Test coverage: the regression block for `PUT *.CMD` (real multi-extent
  binaries, byte comparison) was written in `tests/hostTests.c` but needs two
  clean-ups before it is final: remove the leftover `fprintf` of the output,
  and unlink the extra exchange file `HOST.CMD` (and any `.HST`) before the
  final `rmdir`, otherwise the fixture asserts at the end. Also a test with a
  file of 4+ extents (like HOST.LST, 869 records) is still missing. The prompt
  numbers of the later tests in the fixture were shifted (15/16 became 20/21);
  check this edit when the test is finalised.

## 5. How to reproduce and verify on the desktop

```sh
# rebuild HOST.CMD from guest/cpm86/host/HOST.A86 with ASM86+GENCMD in the
# fixture, install it in bootDisks and regenerate the images
bash /tmp/rebuild86.sh
# full CP/M-86 fixture (tail N lines)
bash /tmp/runCpm86Full.sh 40
```

(These scripts live in /tmp and may be gone; they do: create build/cpm86-host,
copy HOST.A86 with CRLF, create `hosttest.dsk` (LARGE) with HOST.A86, run
hostTests with `CPM86_HOST_COMPILE_ONLY=1`, extract HOST.CMD, copy it to
`bootDisks/cpm86/systemDsk`, run `tools/createSystemDsk.py --os cpm86 --profile
LARGE`, then run `hostTests` with `CPM86_HOST_BUILD_DISK` and
`CPM86_HOST_SYSTEM_DISK`. The host test uses the standard `system.dsk` image;
do not generate a separate small-image fixture.)

## 6. Attempt register

Format: date, what was tried, result, conclusion.

| # | Date | Attempt | Result | Conclusion |
|---|------|---------|--------|------------|
| A1 | 2026-10-08 | Earlier fix: PUT sends at most one 128-byte record per BDOS read (the loop walked past the DMA buffer on larger files). | Fixed the crash on larger single-extent files; small and medium files pass. | Necessary but not sufficient. |
| A2 | 2026-10-08 | Dots, `O` option, precise disk-full/directory-full messages, CRLF 8080 source for ASM+LOAD, ASM86 source with CRLF. | Desktop tests passed. | Did not touch the extent problem. |
| A3 | 2026-10-08 | Added desktop regression: `PIP` the real .CMD files to E: and run `A:HOST PUT *.CMD O`. | Reproduced S1 (ASM86.CMD fails after 4 dots). GENCMD did not stall on the desktop. | Bug is reproducible; only multi-extent files fail. |
| A4 | 2026-10-08 | Instrumented HOST.A86 with distinct error codes but ran the fixture without reinstalling HOST.CMD. | Output unchanged ("transfer failed"). | Wasted attempt: the fixture uses the installed `A:HOST.CMD`, rebuild and reinstall first (section 2). |
| A5 | 2026-10-08 | Rebuilt/installed the instrumented HOST.CMD. | ASM86 reported "local CP/M-86 file I/O failed": the BDOS read fails. Not the CRC, not the console poll. | Points to the file open/read position, not to the host exchange. |
| A6 | 2026-10-08 | Clear EX (`workFcb+12`) and S2 (`workFcb+14`) before the re-open in `putAccepted`; rebuilt and reinstalled HOST.CMD and regenerated the cpm86 images. | Desktop: ASM86.CMD now completes with 12 dots and the bytes match; the other .CMD files still pass. | Root cause of S1/S4 on the desktop. Needs hardware verification (O1). |
| A7 | 2026-10-08 | Clean-up of the new test (remove `fprintf`, unlink HOST.CMD before `rmdir`) | Not applied (the edit was interrupted). The fixture currently asserts at the final `rmdir` because extra files remain in the exchange directory. | Pending (O5). |

## 7. Suggested next steps (in order)

1. Finish O5 (test clean-up, add a 4+ extent test file, e.g. 869 records) so
   the desktop suite is green again and covers HOST.LST-like files.
2. Check CP/M-80 HOST for the same re-open pattern (O4) and add a test.
3. User uploads the regenerated LittleFS image and repeats on the device:
   `host put asm86.cmd`, `host put host.lst`, `host put *.cmd`. Record the
   outcome in the register (A8...).
4. If S3 still happens: capture the exact output, then investigate host-side
   state after an aborted PUT and device timing (O2).
5. For S5: decide on progress output during the CRC pre-scan (O3).
