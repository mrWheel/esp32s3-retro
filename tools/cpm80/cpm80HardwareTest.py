#!/usr/bin/env python3
"""CP/M-80 hardware acceptance test driven over the USB console.

Usage: python3 tools/cpm80/hardwareTest.py [--port /dev/cu.usbmodemXXXX] [--no-write]

Close idf.py monitor / minicom first. The script never flashes the board, but it
RESETS the board (RTS) because CP/M-80 has no host-exit key.
Write tests create and erase HWTEST.COM and FILLnn.TMP files on B: (LARGE) and C: (BIG).
"""
import argparse
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "include"))
import hwCommon  # noqa: E402
import hwCpm  # noqa: E402
import hwSuite  # noqa: E402


class Cpm80(hwCpm.CpmAdapter):
  title = "CP/M-80"
  logName = "cpm80HardwareTest.log"
  menuKey = "1"
  extension = ".COM"
  volumes = ["A:", "B:", "C:", "D:", "E:"]
  rwVolumes = ["B:", "C:"]
  fillVolume = "B:"

  def basic(self):
    out = self.cmd("DIR", 40)
    items = [("basic: DIR on A: lists PIP.COM and STAT.COM", out is not None and "PIP" in out and "STAT" in out, "")]
    out = self.cmd("STAT", 40)
    items.append(("basic: STAT runs on A:", out is not None and "Space" in out, ""))
    out = self.cmd("TYPE WELCOME.TXT", 20)
    items.append(("basic: TYPE WELCOME.TXT shows text", out is not None and len(out) > 40 and "Bdos" not in out, ""))
    out = self.cmd("HELLO", 20)
    items.append(("basic: transient program HELLO.COM runs and warm-boots",
                  out is not None and "Bdos" not in out and "?" not in out, ""))
    items += self.userCommand()
    return items

  def userCommand(self):
    #-- USER scopes file lookups to the active user area; STAT.COM lives in user area 0 only.
    out = self.cmd("DIR A:STAT.COM", 40)
    items = [("user: DIR lists STAT.COM in user area 0",
              out is not None and "STAT" in hwCpm.body(out) and "NO FILE" not in hwCpm.body(out).upper(), "")]
    self.cmd("USER 3", 20)
    out = self.cmd("DIR A:STAT.COM", 40)
    items.append(("user: USER 3 moves STAT.COM out of the active user area",
                  out is not None and "NO FILE" in hwCpm.body(out).upper(), ""))
    self.cmd("USER 0", 20)
    out = self.cmd("DIR A:STAT.COM", 40)
    items.append(("user: USER 0 restores STAT.COM to the active user area",
                  out is not None and "STAT" in hwCpm.body(out) and "NO FILE" not in hwCpm.body(out).upper(), ""))
    return items

  def unconfigured(self):
    #-- Real CP/M waits for a key after a Select error and then returns to a prompt.
    console = self.console
    start = len(console.log)
    console.send("F:\r")
    console.drain(3)
    once = console.log[start:].count("Select") == 1
    start = len(console.log)
    console.send("\x03")
    console.drain(4)
    segment = console.log[start:]
    count = segment.count("Bdos Err On F: Select")
    return [("unconfigured: drive F: reports Bdos Err Select once", once, ""),
            ("unconfigured: a key after the Select error returns to a prompt (no endless loop)",
             "A>" in segment and count == 0,
             "Bdos Err repeated %d times in 4 s; reset needed" % count if count else "")]


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--port")
  parser.add_argument("--no-write", action="store_true")
  arguments = parser.parse_args()
  console = hwCommon.openConsole(arguments)
  return hwSuite.run(Cpm80(console), arguments, here)


if __name__ == "__main__":
  sys.exit(main())
