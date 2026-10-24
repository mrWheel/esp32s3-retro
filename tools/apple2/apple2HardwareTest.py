#!/usr/bin/env python3
"""Apple II (ProDOS / Applesoft) hardware acceptance test driven over the USB console.

Usage: python3 tools/apple2/hardwareTest.py [--port /dev/cu.usbmodemXXXX] [--no-write]

Close idf.py monitor / minicom first. The script never flashes the board, but it
RESETS the board (RTS) because the Apple II emulator has no host-exit key.
Write tests create and delete HWTEST and FILLnn on /TEST1 and /DATA1.
"""
import argparse
import os
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "include"))
import hwCommon  # noqa: E402
import hwSuite  # noqa: E402


class AppleSession:
  def __init__(self, console):
    self.console = console
    self.mark = 0
    self.screen = hwCommon.Screen()

  def text(self):
    self.screen.reset()
    self.screen.feed(self.console.log[self.mark:])
    return "\n".join(line.rstrip() for line in self.screen.text().split("\n") if line.strip())

  def output(self, line):
    #-- The screen scrolls, so keep only what follows the last echo of `line`.
    shown = self.text()
    index = shown.rfind("]" + line)
    return shown[index:] if index >= 0 else shown

  def boot(self):
    self.mark = len(self.console.log)
    self.console.send("4\r")
    deadline = time.time() + 60
    while time.time() < deadline:
      self.console.settle(0.4)
      lines = self.text().split("\n")
      if lines and lines[-1].strip() == "]":
        return True
    return False

  def run(self, line, timeout=20.0):
    #-- Type a line and wait until the screen ends at a clean `]` prompt.
    self.mark = len(self.console.log)
    self.console.send(line + "\r")
    deadline = time.time() + timeout
    while time.time() < deadline:
      self.console.settle(0.5)
      shown = self.output(line)
      lines = shown.split("\n")
      if len(lines) > 1 and lines[-1].strip() == "]":
        return shown
    return None


class Apple2(hwSuite.Adapter):
  title = "Apple II"
  logName = "apple2HardwareTest.log"
  promptName = "ProDOS BASIC ] prompt"
  volumes = ["/SYSTEM", "/TEST1", "/DATA1", "/DATA2"]
  rwVolumes = ["/TEST1", "/DATA1"]
  fillVolume = "/TEST1"
  roVolume = "/SYSTEM"
  freePattern = r"BLOCKS FREE:\s*(\d+)"

  def __init__(self, console):
    super().__init__(console)
    self.apple = AppleSession(console)
    self.fillCount = 0

  def boot(self):
    return self.apple.boot()

  def basic(self):
    banner = "HELLO, APPLE II" in self.apple.text()
    out = self.apple.run("PRINT 2+3")
    return [("basic: STARTUP banner shown (80 columns)", banner, ""),
            ("basic: Applesoft evaluates PRINT 2+3", out is not None and "\n5" in out, "")]

  def readVolume(self, volume):
    out = self.apple.run("CAT " + volume)
    return out is not None and "BLOCKS FREE" in out, "" if out else "timeout"

  def writeFile(self, volume):
    self.apple.run("NEW")
    self.apple.run('10 PRINT "HWOK"')
    out = self.apple.run("SAVE %s/HWTEST" % volume)
    return out is not None and "ERROR" not in out.upper()

  def fileExists(self, volume):
    out = self.apple.run("CAT " + volume)
    return out is not None and "HWTEST" in out

  def copyFile(self, source, target):
    self.apple.run("LOAD %s/HWTEST" % source)
    out = self.apple.run("SAVE %s/HWTEST" % target)
    return out is not None and "ERROR" not in out.upper()

  def deleteFile(self, volume):
    self.apple.run("DELETE %s/HWTEST" % volume)

  def freeSpace(self, volume):
    return hwSuite.freeFrom(self.freePattern, self.apple.run("CAT " + volume))

  def fillUp(self):
    #-- 16K dummy files until ProDOS reports DISK FULL.
    for index in range(1, self.fillLimit + 1):
      self.fillCount = index
      out = self.apple.run("BSAVE %s/FILL%02d,A$800,L$4000" % (self.fillVolume, index), 40)
      if out is None:
        return False, index
      if "DISK FULL" in out.upper():
        return True, index
    return False, self.fillCount

  def cleanupFill(self):
    for index in range(1, self.fillCount + 1):
      self.apple.run("DELETE %s/FILL%02d" % (self.fillVolume, index))

  def roState(self):
    out = self.apple.run("CAT /SYSTEM")
    return out is not None and "HWRO" in out, hwSuite.freeFrom(self.freePattern, out)

  def readOnlyWrite(self):
    self.apple.run("NEW")
    self.apple.run('10 PRINT "RO"')
    out = self.apple.run("SAVE /SYSTEM/HWRO")
    refused = out is not None and ("WRITE PROTECTED" in out.upper() or "I/O ERROR" in out.upper())
    return refused, "" if refused else (out or "no prompt")[-200:]

  def recovered(self):
    return self.apple.run("PRINT 1+1") is not None

  def timed(self):
    start = time.time()
    out = self.apple.run("CAT /DATA2", 60)
    return time.time() - start if out is not None else None

  def unconfigured(self):
    out = self.apple.run("CAT /NOSUCH")
    return [("unconfigured: missing volume gives PATH NOT FOUND", out is not None and "PATH NOT FOUND" in out.upper(), "")]

  def manual(self):
    return [("editor / compiler workflow", "manual: write and run a larger BASIC program"),
            ("terminal escape sequences / keyboard (Ctrl-C, Ctrl-Reset, arrows)", "manual"),
            ("apple2: graphics / lo-res / hi-res output", "manual: run a graphics BASIC program")]


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--port")
  parser.add_argument("--no-write", action="store_true")
  arguments = parser.parse_args()
  console = hwCommon.openConsole(arguments)
  return hwSuite.run(Apple2(console), arguments, here)


if __name__ == "__main__":
  sys.exit(main())
