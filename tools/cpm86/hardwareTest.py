#!/usr/bin/env python3
"""CP/M-86 hardware acceptance test driven over the USB console.

Usage: python3 tools/cpm86/hardwareTest.py [--port /dev/cu.usbmodemXXXX] [--no-write]

Close any other serial monitor first. The script never flashes the board.
Write tests create and erase one small file on drive D: (work86.dsk).
"""
import argparse
import glob
import sys
import time

import serial

results = []


def record(name, ok, detail=""):
  results.append((name, ok, detail))
  print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail else ""), flush=True)


def skip(name, reason):
  results.append((name, None, reason))
  print("SKIP  " + name + "  -- " + reason, flush=True)


class Console:
  def __init__(self, port):
    self.port = serial.Serial(port, 115200, timeout=0.1)
    self.log = ""

  def send(self, text):
    self.port.write(text.encode("latin-1"))

  def waitFor(self, patterns, timeout=20.0, count=1):
    if isinstance(patterns, str):
      patterns = [patterns]
    start = len(self.log)
    deadline = time.time() + timeout
    while time.time() < deadline:
      data = self.port.read(4096)
      if data:
        self.log += data.decode("latin-1")
      segment = self.log[start:]
      for pattern in patterns:
        if segment.count(pattern) >= count:
          return segment
    return None

  def drain(self, seconds=0.5):
    deadline = time.time() + seconds
    while time.time() < deadline:
      data = self.port.read(4096)
      if data:
        self.log += data.decode("latin-1")
        deadline = time.time() + seconds

  def command(self, line, prompt, timeout=30.0):
    start = len(self.log)
    self.send(line + "\r")
    got = self.waitFor(prompt, timeout)
    if got is None:
      return None
    self.drain(0.3)
    return self.log[start:]


def toMenu(console):
  console.send("\x1d")
  time.sleep(0.5)
  console.send("\r")
  console.send("\r")
  return console.waitFor("Select system", 10) is not None


def launch(console):
  console.send("2\r")
  return console.waitFor("A>", 60) is not None


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--port")
  parser.add_argument("--no-write", action="store_true")
  arguments = parser.parse_args()
  port = arguments.port
  if port is None:
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
      print("No /dev/cu.usbmodem* port found")
      return 2
    port = ports[0]
  console = Console(port)
  print("Using " + port)

  #-- Bring the host to a known state: menu.
  console.send("\x1d")
  time.sleep(0.5)
  console.send("\r\r")
  if console.waitFor("Select system", 10) is None:
    print("Host menu not reached. Press the reset button on the board and retry.")
    return 2
  record("host menu reachable", True)

  #-- Cold boot and basic commands.
  ok = launch(console)
  record("cold boot reaches A> prompt", ok)
  if not ok:
    print(console.log[-1500:])
    return 1

  out = console.command("DIR", "A>")
  record("DIR on A: lists ASM86 and PIP", out is not None and "ASM86" in out and "PIP" in out)

  out = console.command("STAT", "A>")
  record("STAT runs on A:", out is not None and "A>" in out and "Bad" not in out)

  #-- Every configured drive.
  for letter in "BCD":
    out = console.command(letter + ":", letter + ">", 15)
    if out is None:
      record("select drive " + letter + ":", False, "no prompt (image missing or invalid?)")
      console.send("A:\r")
      console.waitFor("A>", 10)
      continue
    out = console.command("DIR", letter + ">", 30)
    record("DIR on " + letter + ":", out is not None and "Bdos Err" not in out and "No file" not in out.replace("NO FILE", "No file") or (out is not None and "Bdos Err" not in out), "" if out else "timeout")
  for letter in "EF":
    out = console.command(letter + ":", "D>", 10)
    record("unconfigured drive " + letter + ": is rejected cleanly", out is not None and "?" in out)

  console.command("A:", "A>", 10)

  #-- Writable media (disposable work86.dsk on D:).
  if arguments.no_write:
    skip("write, readback, erase on D:", "--no-write given")
    skip("persistence after close/reopen", "--no-write given")
  else:
    console.command("ERA D:HWTEST.CMD", "A>", 20)
    out = console.command("PIP D:HWTEST.CMD=A:PIP.CMD", "A>", 90)
    record("PIP copy A:PIP.CMD to D:", out is not None and "ERROR" not in out.upper().replace("ERROR:", "ERROR") or (out is not None and "ERROR" not in out.upper()))
    out = console.command("DIR D:HWTEST.CMD", "A>", 20)
    record("copied file visible on D:", out is not None and "HWTEST" in out)
    out = console.command("PIP B:NUL.TMP=D:HWTEST.CMD", "A>", 90)
    ok = out is not None and "ERROR" not in out.upper()
    console.command("ERA B:NUL.TMP", "A>", 20)
    record("copy back D: -> B: (read + write on two drives)", ok)
    console.command("ERA B:NUL.TMP", "A>", 20)

    #-- Persistence across guest stop/start.
    record("Ctrl+] returns to host menu", toMenu(console))
    record("relaunch after Ctrl+]", launch(console))
    out = console.command("DIR D:HWTEST.CMD", "A>", 30)
    record("file persists after close/reopen", out is not None and "HWTEST" in out)
    out = console.command("ERA D:HWTEST.CMD", "A>", 30)
    out = console.command("DIR D:HWTEST.CMD", "A>", 30)
    record("ERA removes file", out is not None and "HWTEST" not in out.replace("D:HWTEST.CMD", "").replace("NO FILE", ""))

  #-- Repeated launches without reset.
  failures = 0
  for index in range(3):
    if not toMenu(console) or not launch(console):
      failures += 1
  record("3 repeated stop/launch cycles", failures == 0, str(failures) + " failures" if failures else "")

  #-- Read-only enforcement on A:.
  console.command("A:", "A>", 10)
  console.send("PIP A:HWRO.TMP=A:PIP.CMD\r")
  refused = console.waitFor("Bad Sector", 60) is not None
  console.send("\x03")
  recovered = console.waitFor("A>", 20) is not None
  record("write to read-only A: is refused (Bdos Err)", refused)
  record("recovery after Bdos error (Ctrl-C returns to A>)", recovered)
  console.command("DIR A:HWRO.TMP", "A>", 20)

  #-- Responsiveness.
  start = time.time()
  out = console.command("DIR", "A>", 60)
  record("DIR completes in < 10 s", out is not None and time.time() - start < 10, "%.1f s" % (time.time() - start))

  toMenu(console)

  #-- Items that cannot be checked over the console.
  skip("PSRAM size / guest memory at boot", "visible only in the boot log; reset the board and read the log")
  skip("millisecond timer F1h-F4h 900-1100 ms", "needs a guest 8086 test program")
  skip("editor/assembler/compiler workflow", "run manually (ASM86, ED, TurboP)")
  skip("terminal escape/cursor behaviour", "run an editor manually")

  print("\n==== SUMMARY ====")
  passed = sum(1 for r in results if r[1] is True)
  failed = [r for r in results if r[1] is False]
  skipped = [r for r in results if r[1] is None]
  print("passed %d, failed %d, skipped(manual) %d" % (passed, len(failed), len(skipped)))
  for name, _, detail in failed:
    print("  FAILED: " + name + ((" -- " + detail) if detail else ""))
  with open("cpm86HardwareTest.log", "w") as handle:
    handle.write(console.log)
  print("Raw console log: cpm86HardwareTest.log")
  return 1 if failed else 0


if __name__ == "__main__":
  sys.exit(main())
