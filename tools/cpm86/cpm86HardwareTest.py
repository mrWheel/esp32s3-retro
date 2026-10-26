#!/usr/bin/env python3
"""CP/M-86 hardware acceptance test driven over the USB console.

Usage: python3 tools/cpm86/hardwareTest.py [--port /dev/cu.usbmodemXXXX] [--no-write]

Close any other serial monitor first. The script never flashes the board, but it
RESETS the board (RTS) for the persistence test.
Write tests create and erase HWTEST.CMD and FILLnn.TMP files on D: (work86.dsk) and B:.
"""
import argparse
import os
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "include"))
import hwCommon  # noqa: E402
import hwCpm  # noqa: E402
import hwSuite  # noqa: E402


class Cpm86(hwCpm.CpmAdapter):
  title = "CP/M-86"
  logName = "cpm86HardwareTest.log"
  menuKey = "2"
  extension = ".CMD"
  volumes = ["A:", "B:", "C:", "D:"]
  rwVolumes = ["D:", "B:"]
  fillVolume = "D:"

  def basic(self):
    out = self.cmd("DIR", 40)
    items = [("basic: DIR on A: lists ASM86 and PIP", out is not None and "ASM86" in out and "PIP" in out, "")]
    out = self.cmd("STAT", 40)
    items.append(("basic: STAT runs on A:", out is not None and "Bad" not in out, ""))
    items += self.userCommand()
    return items

  def userCommand(self):
    #-- USER scopes file lookups to the active user area; STAT.CMD lives in user area 0 only,
    #-- and a bare USER displays the current user number.
    out = self.cmd("DIR A:STAT.CMD", 40)
    items = [("user: DIR lists STAT.CMD in user area 0",
              out is not None and "STAT" in hwCpm.body(out) and "NO FILE" not in hwCpm.body(out).upper(), "")]
    self.cmd("USER 3", 20)
    out = self.cmd("USER", 20)
    items.append(("user: USER displays the current user number",
                  out is not None and hwCpm.body(out).lstrip().startswith("3"), ""))
    out = self.cmd("DIR A:STAT.CMD", 40)
    items.append(("user: USER 3 moves STAT.CMD out of the active user area",
                  out is not None and "NO FILE" in hwCpm.body(out).upper(), ""))
    self.cmd("USER 0", 20)
    out = self.cmd("DIR A:STAT.CMD", 40)
    items.append(("user: USER 0 restores STAT.CMD to the active user area",
                  out is not None and "STAT" in hwCpm.body(out) and "NO FILE" not in hwCpm.body(out).upper(), ""))
    return items

  def unconfigured(self):
    out = self.cmd("E:", 10)
    return [("unconfigured: drive E: is rejected cleanly", out is not None and "?" in out, "")]

  def stop(self):
    #-- Ctrl+] is the CP/M-86 host-exit key.
    self.console.send("\x1d")
    time.sleep(0.5)
    self.console.send("\r\r")
    return self.console.waitFor("Select system", 10) is not None

  def extras(self):
    items = []
    ok = self.stop()
    items.append(("cpm86: Ctrl+] returns to host menu", ok, ""))
    items.append(("cpm86: relaunch after Ctrl+]", self.boot(), ""))
    failures = 0
    for _ in range(3):
      if not self.stop() or not self.boot():
        failures += 1
    items.append(("cpm86: 3 repeated stop/launch cycles", failures == 0, "%d failures" % failures if failures else ""))
    self.stop()
    return items

  def manual(self):
    return super().manual() + [("cpm86: millisecond timer F1h-F4h 900-1100 ms", "needs a guest 8086 test program")]


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--port")
  parser.add_argument("--no-write", action="store_true")
  arguments = parser.parse_args()
  console = hwCommon.openConsole(arguments)
  return hwSuite.run(Cpm86(console), arguments, here)


if __name__ == "__main__":
  sys.exit(main())
