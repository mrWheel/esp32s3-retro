#!/usr/bin/env python3
"""CP/M-86 hardware acceptance test driven over the USB console.

Usage: python3 tools/cpm86/hardwareTest.py [--port /dev/cu.usbmodemXXXX] [--no-write]

Close any other serial monitor first. The script never flashes the board, but it
RESETS the board (RTS) for the persistence test.
Write tests create and erase HWTEST.CMD and FILLnn.TMP files on D: (work86.dsk) and B:.
RUN BENCH86 from A: records the measured values in cpm86HardwareTest.log. The suite waits
for BENCH86 to finish (A> back, or a 180 s timeout), echoes every measurement line with a
"bench86: " prefix and records each verdict immediately, so the call, the displays and the
results stay one block before the next test starts (same shape as the bench80 block).
"""
import argparse
import os
import re
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
  #-- D: (work86-2.dsk) is 8 MiB: 60 x ~7.5 KiB files can never fill it. C: (work86-1.dsk)
  #-- is the 516 KiB LARGE volume, fillable within the limit (measured 2026-11-01).
  fillVolume = "C:"
  fillLimit = 80

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

  def guestRun(self, command, waitText, keyDelay, timeout):
    #-- Run a console test program that prints @BEGIN..., optionally pauses for one host
    #-- keystroke (TIMER86 blocks in BDOS console input until we send it), prints
    #-- @DONE PASS and returns to the CCP prompt. Returns (output, error), error is "".
    self.console.logCommand(command)
    start = len(self.console.log)
    self.console.send(command + "\r")
    if self.console.waitFor(waitText, 30, since=start) is None:
      return None, "no " + waitText
    if keyDelay is not None:
      time.sleep(keyDelay)
      self.console.send("\r")
    out = self.console.waitFor("@DONE PASS", timeout, since=start)
    if out is None:
      return None, "no @DONE PASS"
    if self.console.waitFor("A>", 20, since=start) is None:
      return None, "no A> prompt after program"
    return out, ""

  def recoverToPrompt(self):
    #-- A program stuck in BDOS console input ignores Ctrl+C until it gets its key:
    #-- send one first, then Ctrl+C and wait for the CCP prompt.
    self.console.send("\r")
    self.console.settle(1)
    self.console.ctrlC()
    self.console.waitFor("A>", 20)

  def tailOfOutput(self):
    #-- Collapsed tail of the console log, used as the failure detail.
    return " ".join(self.console.log[-400:].split())[-160:]

  def timerTest(self):
    #-- Phase 86-07: TIMER86 measures F1h-F4h around a 1 s host-side key wait. The
    #-- verdict is recorded immediately (expect), so it stays attached to its own
    #-- "Command:" block like every other check in the report.
    name = "timer: guest delta of a 1 s host wait is 900-1100 ms"
    out, error = self.guestRun("TIMER86", "@BEGIN TIMER", 1.0, 30)
    if out is None:
      self.recoverToPrompt()
      self.expect(name, False, error + "; tail: " + self.tailOfOutput())
      return []
    delta = hwSuite.freeFrom(r"DELTA=(\d+)", out)
    if delta is None:
      self.expect(name, False, "no DELTA= value; tail: " + self.tailOfOutput())
      return []
    self.console.note("  measured delta %d ms" % delta)
    self.expect(name, 900 <= delta <= 1100, "%d ms" % delta)
    return []

  def benchTest(self):
    #-- BENCH86 v2 scale 1, mirroring cpm80's bench80() block: cmd() blocks on the "A>"
    #-- prompt with a generous timeout, so the suite waits until BENCH86 has finished
    #-- (or the timeout expires) before any later test runs. Every measurement line is
    #-- echoed with a "bench86: " prefix and each verdict is recorded immediately, so
    #-- call, displays and results stay one block in the report.
    results = self.console.results
    if results:
      results.say("bench86: starting BENCH86, waiting up to 180 s for it to finish")
    out = self.cmd("A:BENCH86", 180)
    if out is None:
      if results:
        results.say("bench86: BENCH86 did not return to A> within 180 s, continuing anyway")
    text = hwCpm.body(out)
    wanted = ("BENCH86 v2", "@BEGIN", "@END", "@DONE", "Elapsed:", "Index vs", "Timer unavailable")
    for line in text.splitlines():
      stripped = line.strip()
      if results and stripped.startswith(wanted):
        results.say("bench86: " + stripped)
    elapsed = re.findall(r"Elapsed:\s*(\d+) ms", text)
    indexes = re.findall(r"Index vs 5MHz MODEL:\s*(\d+)", text)
    ends = [line.strip() for line in text.splitlines() if line.strip().startswith("@END")]
    self.expect("bench86: BENCH86 runs from A: and completes",
                out is not None and "@DONE PASS" in text and "Timer unavailable" not in text,
                "" if out is not None else "timeout while running BENCH86")
    self.expect("bench86: all benchmark sections report PASS",
                len(ends) >= 2 and all("PASS" in line for line in ends) and len(elapsed) >= len(ends),
                "%d sections, elapsed %s ms" % (len(ends), ",".join(elapsed)))
    self.expect("bench86: measured index values recorded",
                len(indexes) >= 1, "index vs 5MHz MODEL: " + (",".join(indexes) if indexes else "none"))
    return []

  def extras(self):
    #-- timerTest/benchTest record their verdicts inline (expect), keeping each check
    #-- with its own command block; only the stop/launch cycles are collected here.
    items = []
    self.timerTest()
    self.benchTest()
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



def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--port")
  parser.add_argument("--no-write", action="store_true")
  arguments = parser.parse_args()
  console = hwCommon.openConsole(arguments)
  return hwSuite.run(Cpm86(console), arguments, here)


if __name__ == "__main__":
  sys.exit(main())
