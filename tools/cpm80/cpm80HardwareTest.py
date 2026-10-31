#!/usr/bin/env python3
"""CP/M-80 hardware acceptance test driven over the USB console.

Usage: python3 tools/cpm80/hardwareTest.py [--port /dev/cu.usbmodemXXXX] [--no-write]

Close idf.py monitor / minicom first. The script never flashes the board, but it
RESETS the board (RTS) because CP/M-80 has no host-exit key.
Write tests create and erase HWTEST.COM and FILLnn.TMP files on B: (LARGE) and C: (BIG).
RUN BENCH80 from A: records the measured timer values in cpm80HardwareTest.log. The
suite waits for BENCH80 to finish (A> back, or a 600 s timeout) before continuing.
The user-area test creates, isolates, deletes and re-checks USER.TXT on the writable C:.
Every logged PASS/FAIL is preceded by the exact "Command:" that produced it, together
with the guest answer, and each test phase is logged as one block.
"""
import argparse
import os
import re
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
    #-- Every check records its result through self.expect() right after its own command,
    #-- so the PASS/FAIL line stays attached to its "Command:" and answer in the log.
    out = self.cmd("DIR", 40)
    self.expect("basic: DIR on A: lists PIP.COM and STAT.COM",
                out is not None and "PIP" in out and "STAT" in out)
    out = self.cmd("STAT", 40)
    self.expect("basic: STAT runs on A:", out is not None and "Space" in out)
    out = self.cmd("TYPE WELCOME.TXT", 20)
    self.expect("basic: TYPE WELCOME.TXT shows text",
                out is not None and len(out) > 40 and "Bdos" not in out)
    out = self.cmd("HELLO", 20)
    self.expect("basic: transient program HELLO.COM runs and warm-boots",
                out is not None and "Bdos" not in out and "?" not in out)
    self.userCommand()
    self.userFiles()
    self.bench80()
    return []

  def cmdOn(self, volume, line, timeout=40):
    #-- self.cmd waits for the A> prompt; use this helper while another drive is current.
    return self.console.command(line, volume[0] + ">", timeout)

  def userFiles(self):
    #-- Full user-area workflow on the writable C:: SAVE 0 creates an empty USER.TXT in
    #-- user area 3. The owner must see it before the isolation, deletion and user 0
    #-- re-checks are evaluated; later checks are skipped when the creation did not work.
    out = self.console.command("C:", "C>", 15)
    if out is None:
      self.console.ctrlC()
      self.cmd("A:", 10)
      self.expect("user files: switch to writable C:", False, "C: did not select (not mounted?)")
      return []
    self.expect("user files: switch to writable C:", True)
    results = self.console.results

    def listsUserTxt(out):
      #-- Match a directory entry named USER with extension TXT (e.g. "USER     TXT").
      return re.search(r"\bUSER\s+TXT\b", hwCpm.body(out).upper()) is not None

    def announceDir(label):
      #-- Head the listing; the "Command: DIR" line and its answer follow immediately below.
      if results:
        results.say("user files: DIR " + label + ":")

    self.cmdOn("C:", "USER 3", 20)
    self.cmdOn("C:", "ERA USER.TXT", 30)
    announceDir("on C: user 3 before the test")
    out = self.cmdOn("C:", "DIR", 40)
    self.expect("user files: user 3 area on C: starts without USER.TXT",
                out is not None and not listsUserTxt(out))
    out = self.cmdOn("C:", "SAVE 0 USER.TXT", 60)
    saved = out is not None and "Bdos" not in out and "?" not in out
    announceDir("on C: user 3 after SAVE")
    out = self.cmdOn("C:", "DIR", 40)
    created = saved and out is not None and listsUserTxt(out)
    self.expect("user files: USER.TXT created with SAVE 0 is visible for its owner user 3",
                created, "" if created else ("SAVE failed" if not saved else "not listed for user 3"))
    if not created:
      reason = "not evaluated: USER.TXT was not created"
      for name in ("user files: USER.TXT of user 3 stays hidden for user 4",
                   "user files: USER.TXT does not leak into the user 0 area",
                   "user files: USER.TXT is still visible for its owner user 3",
                   "user files: deleting USER.TXT as user 3 removes it for user 3",
                   "user files: USER.TXT is still gone after switching back to user 0"):
        self.expect(name, None, reason)
      self.cmdOn("C:", "USER 0", 20)
      self.cmd("A:", 10)
      return []
    self.cmdOn("C:", "USER 4", 20)
    announceDir("on C: user 4")
    out = self.cmdOn("C:", "DIR", 40)
    self.expect("user files: USER.TXT of user 3 stays hidden for user 4",
                out is not None and not listsUserTxt(out))
    self.cmdOn("C:", "USER 0", 20)
    announceDir("on C: user 0")
    out = self.cmdOn("C:", "DIR", 40)
    self.expect("user files: USER.TXT does not leak into the user 0 area",
                out is not None and not listsUserTxt(out))
    self.cmdOn("C:", "USER 3", 20)
    out = self.cmdOn("C:", "DIR", 40)
    self.expect("user files: USER.TXT is still visible for its owner user 3",
                out is not None and listsUserTxt(out))
    self.cmdOn("C:", "ERA USER.TXT", 30)
    out = self.cmdOn("C:", "DIR", 40)
    self.expect("user files: deleting USER.TXT as user 3 removes it for user 3",
                out is not None and not listsUserTxt(out))
    self.cmdOn("C:", "USER 0", 20)
    announceDir("on C: user 0 after the delete")
    out = self.cmdOn("C:", "DIR", 40)
    self.expect("user files: USER.TXT is still gone after switching back to user 0",
                out is not None and not listsUserTxt(out))
    self.cmd("A:", 10)
    return []

  def bench80(self):
    #-- RUN BENCH80 from A: (RETRO80_TIMER_B1); log every measured value in the report.
    #-- cmd() blocks on the "A>" prompt, so the suite waits here until BENCH80 has
    #-- finished (or the generous timeout expires) before continuing with the tests.
    results = self.console.results
    if results:
      results.say("bench80: starting BENCH80, waiting up to 600 s for it to finish")
    out = self.cmd("A:BENCH80", 600)
    if out is None:
      if results:
        results.say("bench80: BENCH80 did not return to A> within 600 s, continuing anyway")
    text = hwCpm.body(out)
    wanted = ("BENCH80 v2", "@BEGIN", "@END", "@DONE", "Elapsed:", "Index vs", "Timer unavailable")
    for line in text.splitlines():
      stripped = line.strip()
      if results and stripped.startswith(wanted):
        results.say("bench80: " + stripped)
    elapsed = re.findall(r"Elapsed:\s*(\d+)\s*ms", text)
    indexes = re.findall(r"Index vs 1MHz MODEL:\s*(\d+)", text)
    ends = [line.strip() for line in text.splitlines() if line.strip().startswith("@END")]
    self.expect("bench80: BENCH80 runs from A: and completes",
                out is not None and "@DONE PASS" in text and "Timer unavailable" not in text,
                "" if out is not None else "timeout while running BENCH80")
    self.expect("bench80: all benchmark sections report PASS",
                len(ends) >= 2 and all("PASS" in line for line in ends) and len(elapsed) >= len(ends),
                "%d sections, elapsed %s ms" % (len(ends), ",".join(elapsed)))
    self.expect("bench80: measured index values recorded",
                len(indexes) >= 1, "index vs 1MHz MODEL: " + (",".join(indexes) if indexes else "none"))
    return []

  def userCommand(self):
    #-- USER scopes file lookups to the active user area; STAT.COM lives in user area 0 only.
    out = self.cmd("DIR A:STAT.COM", 40)
    self.expect("user: DIR lists STAT.COM in user area 0",
                out is not None and "STAT" in hwCpm.body(out) and "NO FILE" not in hwCpm.body(out).upper())
    self.cmd("USER 3", 20)
    out = self.cmd("DIR A:STAT.COM", 40)
    self.expect("user: USER 3 moves STAT.COM out of the active user area",
                out is not None and "NO FILE" in hwCpm.body(out).upper())
    self.cmd("USER 0", 20)
    out = self.cmd("DIR A:STAT.COM", 40)
    self.expect("user: USER 0 restores STAT.COM to the active user area",
                out is not None and "STAT" in hwCpm.body(out) and "NO FILE" not in hwCpm.body(out).upper())
    return []

  def unconfigured(self):
    #-- Real CP/M waits for a key after a Select error and then returns to a prompt.
    #-- One repeated Select error per prompt cycle is normal CP/M behaviour (the CCP
    #-- re-selects the current drive after the warm boot); only a flood without any
    #-- prompt counts as an endless loop.
    console = self.console
    start = len(console.log)
    console.logCommand("F:")
    console.send("F:\r")
    console.drain(3)
    once = console.log[start:].count("Select") == 1
    start = len(console.log)
    console.logCommand("^C (Ctrl-C)")
    console.send("\x03")
    console.drain(4)
    segment = console.log[start:]
    count = segment.count("Bdos Err On F: Select")
    prompted = any(p in segment for p in ("A>", "B>", "C>", "D>", "E>", "F>"))
    ok = prompted and count <= 3
    detail = ""
    if not ok:
      detail = "%s; Bdos Err repeated %d times in 4 s" % ("prompt seen" if prompted else "no prompt", count)
    return [("unconfigured: drive F: reports Bdos Err Select once", once, ""),
            ("unconfigured: a key after the Select error returns to a prompt (no endless loop)", ok, detail)]


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--port")
  parser.add_argument("--no-write", action="store_true")
  arguments = parser.parse_args()
  console = hwCommon.openConsole(arguments)
  return hwSuite.run(Cpm80(console), arguments, here)


if __name__ == "__main__":
  sys.exit(main())
