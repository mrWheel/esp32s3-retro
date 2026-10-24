#!/usr/bin/env python3
"""UCSD / Apple Pascal 1.3 hardware acceptance test driven over the USB console.

Usage: python3 tools/ucsd/hardwareTest.py [--port /dev/cu.usbmodemXXXX] [--no-write]

Close idf.py monitor / minicom first. The script never flashes the board, but it
RESETS the board (RTS) because the Apple II/UCSD emulator has no host-exit key.
Write tests create and remove HWTEST.DATA and FILLnn.CODE on WORK1: and WORK2: through the Filer.
"""
import argparse
import os
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "include"))
import hwCommon  # noqa: E402
import hwSuite  # noqa: E402


class PascalSession:
  def __init__(self, console):
    self.console = console
    self.mark = 0

  def text(self):
    screen = hwCommon.Screen()
    screen.feed(self.console.log[self.mark:])
    return "\n".join(line.rstrip() for line in screen.text().split("\n") if line.strip())

  def waitText(self, expect, timeout):
    if isinstance(expect, str):
      expect = [expect]
    deadline = time.time() + timeout
    while time.time() < deadline:
      self.console.settle(0.5)
      shown = self.text()
      for pattern in expect:
        if pattern in shown:
          return shown
    return None

  def keys(self, text, expect, timeout=30.0):
    #-- Send keystrokes from a fresh screen state and wait for `expect` (or any of a list) to appear.
    self.mark = len(self.console.log)
    self.console.send(text)
    out = self.waitText(expect, timeout)
    self.console.settle(1.0)
    return self.text() if out is not None else None

  def listing(self, volume):
    #-- Long directories page: when the screen stops changing without a summary line, press Return.
    self.keys("L", "what volume", 15)
    self.mark = len(self.console.log)
    self.console.send(volume + "\r")
    collected = ""
    previous = ""
    quiet = 0
    deadline = time.time() + 90
    while time.time() < deadline:
      self.console.settle(1.0)
      shown = self.text()
      if "files <listed/in dir>" in shown:
        self.console.settle(1.0)
        collected += "\n" + self.text()
        break
      quiet = quiet + 1 if shown == previous else 0
      previous = shown
      if quiet >= 4:
        collected += "\n" + shown
        self.mark = len(self.console.log)
        self.console.send("\r")
        quiet = 0
        previous = ""
    self.keys("\r", "Filer:", 10)
    return collected if "files <listed/in dir>" in collected else None


class Ucsd(hwSuite.Adapter):
  title = "UCSD Pascal"
  logName = "ucsdHardwareTest.log"
  promptName = "Pascal Command: prompt"
  volumes = ["PASCAL:", "BACKUP:", "WORK1:", "WORK2:"]
  rwVolumes = ["WORK1:", "WORK2:"]
  fillVolume = "WORK1:"
  roVolume = "BACKUP:"
  unusedPattern = r"(\d+) unused"

  def __init__(self, console):
    super().__init__(console)
    self.pascal = PascalSession(console)
    self.inFiler = False
    self.fillCount = 0
    self.lastText = ""

  def boot(self):
    self.inFiler = False
    self.pascal.mark = len(self.console.log)
    self.console.send("3\r")
    return self.pascal.waitText("Command:", 90) is not None

  def filer(self):
    if not self.inFiler:
      self.inFiler = self.pascal.keys("F", "Filer:", 15) is not None

  def basic(self):
    items = [("basic: Apple Pascal 1.3 banner shown", "Apple II Pascal 1.3" in self.pascal.text(), "")]
    out = self.pascal.keys("F", "Filer:", 15)
    self.inFiler = out is not None
    items.append(("basic: F enters the Filer", out is not None, ""))
    out = self.pascal.keys("V", "Prefix volume", 15)
    items.append(("basic: V(ols lists PASCAL: BACKUP: WORK1: WORK2:",
                  out is not None and all(name in out for name in self.volumes), ""))
    self.pascal.keys("\r", "Filer:", 10)
    return items

  def readVolume(self, volume):
    self.filer()
    start = time.time()
    out = self.pascal.listing(volume)
    return out is not None, "%.1f s" % (time.time() - start) if out else "no summary line"

  def transfer(self, source, target):
    self.filer()
    self.pascal.keys("T", "Transfer what file", 15)
    self.pascal.keys(source + "\r", "To where", 15)
    out = self.pascal.keys(target + "\r", ["-->", "full", "rror", "rotect"], 90)
    self.lastText = out or ""
    self.pascal.keys("\r", "Filer:", 10)
    return out

  def writeFile(self, volume):
    out = self.transfer("PASCAL:SYSTEM.MISCINFO", volume + "HWTEST.DATA")
    return out is not None and "-->" in out

  def fileExists(self, volume):
    self.filer()
    out = self.pascal.listing(volume)
    return out is not None and "HWTEST.DATA" in out

  def copyFile(self, source, target):
    out = self.transfer(source + "HWTEST.DATA", target + "HWTEST.DATA")
    return out is not None and "-->" in out

  def remove(self, name):
    self.filer()
    self.pascal.keys("R", "Remove what file", 15)
    self.pascal.keys(name + "\r", ["Update directory", "No such", "no file"], 60)
    self.pascal.keys("Y", "Filer:", 20)

  def deleteFile(self, volume):
    self.remove(volume + "HWTEST.DATA")

  def freeSpace(self, volume):
    self.filer()
    return hwSuite.freeFrom(self.unusedPattern, self.pascal.listing(volume))

  def fillUp(self):
    for index in range(1, self.fillLimit + 1):
      self.fillCount = index
      out = self.transfer("PASCAL:SYSTEM.COMPILER", "%sFILL%02d.CODE" % (self.fillVolume, index))
      if out is None:
        return False, index
      if "-->" not in out and "full" in out.lower():
        return True, index
    return False, self.fillCount

  def cleanupFill(self):
    self.remove(self.fillVolume + "FILL=")

  def roState(self):
    self.filer()
    out = self.pascal.listing(self.roVolume)
    return out is not None and "HWRO.DATA" in out, hwSuite.freeFrom(self.unusedPattern, out)

  def readOnlyWrite(self):
    out = self.transfer("PASCAL:SYSTEM.MISCINFO", self.roVolume + "HWRO.DATA")
    refused = out is None or "-->" not in out or "rotect" in out
    return refused, "" if refused else "transfer reported success"

  def recovered(self):
    return self.pascal.text().count("Filer:") > 0 or self.pascal.keys("\r", "Filer:", 10) is not None

  def timed(self):
    self.filer()
    start = time.time()
    out = self.pascal.keys("V", "Prefix volume", 30)
    elapsed = time.time() - start
    self.pascal.keys("\r", "Filer:", 10)
    return elapsed if out is not None else None

  def unconfigured(self):
    self.filer()
    self.pascal.keys("L", "what volume", 15)
    out = self.pascal.keys("NOSUCH:\r", ["No such", "no such", "Filer:"], 20)
    shown = out or ""
    ok = out is not None and "files <listed" not in shown
    self.pascal.keys("\r", "Filer:", 10)
    return [("unconfigured: missing volume is rejected cleanly", ok, shown[-80:].replace("\n", " | "))]

  def manual(self):
    return [("editor / compiler workflow", "manual: E(dit, C(omp, R(un a small program"),
            ("terminal escape sequences / cursor keys in the editor", "manual")]


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--port")
  parser.add_argument("--no-write", action="store_true")
  arguments = parser.parse_args()
  console = hwCommon.openConsole(arguments)
  return hwSuite.run(Ucsd(console), arguments, here)


if __name__ == "__main__":
  sys.exit(main())
