#!/usr/bin/env python3

# Copyright (C) 2026 Willem Aandewiel
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

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
  #-- is the 516 KiB CPM86_516K volume, fillable within the limit (measured 2026-11-01).
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

  def ensureGuest(self):
    #-- A guest fault stops the machine ("... Press ENTER to return to the main menu."),
    #-- and every later command then times out against the host menu (CPM86-VERIFY-054:
    #-- one HELLO crash cascaded into four unrelated failures). Between phases make sure
    #-- we really sit at the CCP prompt, otherwise relaunch.
    start = len(self.console.log)
    self.console.send("\r")
    if self.console.waitFor("A>", 4, since=start) is not None:
      return True
    if self.console.waitFor(["Select system", "Press ENTER"], 3, since=start) is not None:
      self.console.send("\r")
      self.console.waitFor("Select system", 8)
      return self.boot()
    if self.stop():
      return self.boot()
    return False

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

  def drive(self, letter):
    #-- Switch the CCP to a drive (the prompt becomes <letter>); cmd() only waits on "A>".
    return self.console.command(letter + ":", letter + ">", 15)

  def driveCmd(self, line, letter, timeout=40.0):
    return self.console.command(line, letter + ">", timeout)

  #-- HELLO.A86 source typed through ED (phase 86-08). It copies the proven HELP.A86
  #-- small-model pattern exactly: code in CSEG at CS:0, init MOV AX,DS / MOV SS,AX /
  #-- MOV SP,OFFSET STACKTOP (DS comes from the CCP; CP/M-86 preserves DS/SS, SYS
  #-- protects ES - no CS/ES moves and no CLI/STI/CLD), data in DSEG after ORG 100H,
  #-- reserved stack as a bare RB 512 closed by one initialized byte (that final DB is
  #-- what makes GENCMD emit the reserved storage). Two earlier attempts crashed the
  #-- guest in the BDOS call: without any init (push on the CCP's leftover SS) and with
  #-- DS/ES-from-CS plus CLI/STI/CLD (ASM86 reported "NUMBER OF ERRORS: 1", leaving the
  #-- init incomplete - physical A0000 faults, CPM86-VERIFY-054).
  helloSource = [
    "; HELLO - native ASM86 workflow proof (86-08)",
    "        CSEG",
    "        ORG 0",
    "START:  MOV AX,DS",
    "        MOV SS,AX",
    "        MOV SP,OFFSET STACKTOP",
    "        MOV CL,9",
    "        MOV DX,OFFSET MSG",
    "        INT 0E0H",
    "        MOV CL,0",
    "        INT 0E0H",
    "        DSEG",
    "        ORG 100H",
    "MSG     DB 'HELLO FROM NATIVE ASM86',13,10,'$'",
    "        RB 512",
    "STACKTOP DB 0",
    "        END",
  ]

  def dirSyntaxTest(self):
    #-- Characterization (CCP 1.1, probed 2026-11-01): a named DIR lookup WITHOUT a file
    #-- type reports "NO FILE" even for files that exist and load (the CCP fills the type
    #-- field with the $DIR/$SYS markers, which never match status 00h entries). With an
    #-- explicit type the same lookup lists the file.
    out = self.cmd("DIR A:STAT", 30)
    ok = out is not None and "NO FILE" in hwCpm.body(out).upper()
    self.expect("dir: named DIR without a type reports NO FILE (CCP $DIR/$SYS default)",
                ok, "" if ok else "tail " + self.tailOfOutput())
    out = self.cmd("DIR A:STAT.CMD", 30)
    ok = out is not None and "STAT" in hwCpm.body(out) and "NO FILE" not in hwCpm.body(out).upper()
    self.expect("dir: named DIR with explicit type lists the file", ok,
                "" if ok else "tail " + self.tailOfOutput())

  def typeViaEd(self, name, lines, attempts=4):
    #-- ED typing over the console loses characters at speed (probes 2026-11-01: one
    #-- run lost a single line, another everything after "DSEG"), so send one line per
    #-- short settle, save, and verify the saved bytes with TYPE; on mismatch erase and
    #-- retype (up to 4 attempts). The ED command flow itself is per the User's Guide
    #-- Dec81 section 5: "I"+CR, text lines, ESC, then "E"+CR ALONE on the line.
    want = " ".join(" ".join(lines).upper().split())
    for _ in range(attempts):
      self.cmd("ERA " + name, 20)
      self.console.logCommand("ED " + name)
      start = len(self.console.log)
      self.console.send("ED " + name + "\r")
      if self.console.waitFor("*", 15, since=start) is None:
        self.recoverToPrompt()
        continue
      self.console.send("I\r")
      self.console.settle(0.3)
      for line in lines:
        self.console.send(line + "\r")
        self.console.settle(0.08)
      self.console.settle(0.5)
      self.console.send("\x1b")
      self.console.settle(0.5)
      start = len(self.console.log)
      self.console.send("E\r")
      if self.console.waitFor("A>", 20, since=start) is None:
        self.recoverToPrompt()
        continue
      out = self.cmd("TYPE " + name, 30)
      if out is not None and want in " ".join(hwCpm.body(out).upper().split()):
        return True
    return False

  def editorTest(self):
    #-- Phase 86-08 (closes the manual editor/compiler workflow). Hardware probes
    #-- (2026-11-01) established the two lookup rules that shape this test:
    #--   * transient tools are looked up in the CURRENT drive and CURRENT user area - a
    #--     "PIP?" style error is the CCP not finding the tool there, not PIP objecting;
    #--     the tools therefore run from A> (where they live) with drive-qualified file
    #--     names, and HELLO runs from the drive that holds it;
    #--   * ED (User's Guide Dec81 section 5): "I"+CR enters insert mode, ESC ends text
    #--     input back to the command prompt, and "E"+CR (ALONE on the line) writes the
    #--     buffer and leaves ED; Ctrl-C at * aborts without saving (Table 5-3). Fast
    #--     typing loses lines, hence typeViaEd's TYPE verification and retries;
    #--   * packaged HELP.CMD observed RUNNING in-guest (banner + file list + HELP>
    #--     prompt, probe 2026-11-01) is the witness that the HELP.A86 source pattern
    #--     (CSEG code, DSEG data, own SS:SP, RB stack) assembles and loads correctly.
    self.ensureGuest()
    for name in ("C:HELLO.A86", "C:HELLO.H86", "C:HELLO.LST", "C:HELLO.SYM", "C:HELLO.CMD",
                 "B:HELLO.CMD"):
      self.cmd("ERA " + name, 20)
    ok = self.typeViaEd("C:HELLO.A86", self.helloSource)
    self.expect("workflow: ED insert + E writes C:HELLO.A86 and returns to A>", ok,
                "" if ok else "TYPE never matched the source; tail " + self.tailOfOutput())
    if not ok:
      self.recoverToPrompt()
      return
    out = self.cmd("DIR C:HELLO.A86", 40)
    text = hwCpm.body(out) if out is not None else ""
    ok = out is not None and "A86" in text and "NO FILE" not in text.upper()
    self.expect("workflow: HELLO.A86 is listed on C:", ok, "" if ok else "tail " + self.tailOfOutput())
    out = self.cmd("A:ASM86 C:HELLO", 90)
    #-- ASM86 pads the counter ("NUMBER OF ERRORS:   0."), so match with flexible spacing.
    text = hwCpm.body(out).upper() if out is not None else ""
    ok = re.search(r"NUMBER OF ERRORS:\s*0\s*\.", text) is not None
    self.expect("workflow: A:ASM86 C:HELLO assembles with NUMBER OF ERRORS: 0", ok,
                " ".join(text.split())[-160:] if text else "tail " + self.tailOfOutput())
    out = self.cmd("DIR C:HELLO.H86", 40)
    text = hwCpm.body(out) if out is not None else ""
    ok = out is not None and "H86" in text and "NO FILE" not in text.upper()
    self.expect("workflow: ASM86 produced HELLO.H86", ok, "" if ok else "tail " + self.tailOfOutput())
    out = self.cmd("A:GENCMD C:HELLO", 90)
    self.expect("workflow: A:GENCMD C:HELLO returns to A>", out is not None,
                "" if out is not None else "tail " + self.tailOfOutput())
    out = self.cmd("DIR C:HELLO.CMD", 40)
    ok = out is not None and re.search(r"HELLO\s+CMD", hwCpm.body(out)) is not None
    self.expect("workflow: GENCMD linked HELLO.CMD", ok, "" if ok else "tail " + self.tailOfOutput())
    self.drive("C")
    out = self.driveCmd("HELLO", "C", 30)
    ok = out is not None and "HELLO FROM NATIVE ASM86" in hwCpm.body(out)
    self.expect("workflow: the produced HELLO runs from C:", ok, "" if ok else "tail " + self.tailOfOutput())
    self.drive("A")
    out = self.cmd("PIP B:HELLO.CMD=C:HELLO.CMD", 90)
    ok = out is not None and hwCpm.pipOk(out) and "PIP?" not in hwCpm.body(out)
    self.expect("workflow: PIP copies HELLO.CMD from C: to B:", ok,
                "" if ok else "tail " + self.tailOfOutput())
    self.drive("B")
    out = self.driveCmd("HELLO", "B", 30)
    ok = out is not None and "HELLO FROM NATIVE ASM86" in hwCpm.body(out)
    self.expect("workflow: the PIP-copied HELLO runs from B:", ok, "" if ok else "tail " + self.tailOfOutput())
    self.drive("A")
    for name in ("C:HELLO.A86", "C:HELLO.H86", "C:HELLO.LST", "C:HELLO.SYM", "C:HELLO.CMD",
                 "B:HELLO.CMD"):
      self.cmd("ERA " + name, 20)

  def terminalTest(self):
    #-- Phase 86-08 plain-terminal checks (escape-sequence/full-screen behavior stays
    #-- manual): CR/LF pairs on output, Ctrl-H erase on the CCP line, Ctrl-C re-prompt.
    self.ensureGuest()
    out = self.cmd("DIR", 40)
    self.expect("terminal: console output uses CR/LF pairs",
                out is not None and "\r\n" in out, "" if out is not None else "timeout on DIR")
    #-- The BIOS CONIN handles 0x08/0x7F as erase (probed 2026-11-01): "DIRZ" plus ONE
    #-- Ctrl-H becomes a plain DIR and lists the disk.
    self.console.logCommand("DIRZ + Ctrl-H + CR")
    start = len(self.console.log)
    self.console.send("DIRZ")
    self.console.settle(0.5)
    self.console.send("\x08")
    self.console.settle(0.3)
    self.console.send("\r")
    out = self.console.waitFor("A>", 30, since=start)
    ok = out is not None and "ASM86" in out and "NO FILE" not in out.upper()
    self.expect("terminal: Ctrl-H erases the last character on the CCP line", ok,
                "" if ok else "tail " + self.tailOfOutput())
    #-- Characterization (CP/M-86 1.1 has no CCP line abort; probed 2026-11-01): Ctrl-C is
    #-- passed through by the BIOS and the CCP EXECUTES the typed line. The check asserts
    #-- only that the guest stays usable; the missing line-abort is recorded in ISSUE-008.
    self.console.logCommand("DIR + Ctrl-C + CR")
    start = len(self.console.log)
    self.console.send("DIR")
    self.console.settle(0.5)
    self.console.ctrlC()
    self.console.settle(0.5)
    self.console.send("\r")
    out = self.console.waitFor("A>", 20, since=start)
    self.expect("terminal: Ctrl-C leaves the CCP usable (1.1 has no line abort; the typed "
                "line runs)", out is not None,
                "" if out is not None else "no prompt after Ctrl-C; tail " + self.tailOfOutput())

  def warmBootTest(self):
    #-- Phase 86-09: WBOOT86 prints its banner then calls BDOS function 0. The BIOS WBOOT
    #-- must re-enter the CCP (prompt A>, CURRENTDRIVE forced to 0 by the BIOS WBOOT itself)
    #-- WITHOUT the host menu. The observed USER value after the warm boot is recorded
    #-- (state-survival documentation). The transient is launched while USER 0 is active.
    name = "wboot: BDOS function 0 re-enters the CCP without the host menu"
    self.ensureGuest()
    self.drive("A")
    #-- USER scopes file lookups (see userCommand): a transient in user area 0 cannot be
    #-- loaded while USER 3 is active ("WBOOT86?", probed 2026-11-01), so set 3, verify
    #-- it stuck, then return to user 0 before launching the warm boot itself.
    self.cmd("USER 3", 20)
    out = self.cmd("USER", 20)
    stuck3 = out is not None and hwCpm.body(out).lstrip().startswith("3")
    self.cmd("USER 0", 20)
    self.console.logCommand("WBOOT86")
    start = len(self.console.log)
    self.console.send("WBOOT86\r")
    if self.console.waitFor("@BEGIN WBOOT", 30, since=start) is None:
      self.expect(name, False, "no @BEGIN WBOOT; tail " + self.tailOfOutput())
      self.recoverToPrompt()
      return
    #-- Both waits anchor at "start" (the moment before the WBOOT86 send): the echoed
    #-- command contains no "A>", and anchoring after the @BEGIN match raced with the
    #-- CCP prompt the warm boot prints microseconds later (observed false failure).
    gotMenu = self.console.waitFor("Select system", 5, since=start) is not None
    gotPrompt = False if gotMenu else self.console.waitFor("A>", 20, since=start) is not None
    ok = gotPrompt and not gotMenu
    detail = "menuSeen=%s" % gotMenu
    if ok:
      out = self.cmd("USER", 20)
      digits = re.search(r"\d+", hwCpm.body(out)) if out is not None else None
      detail += ", USER after warm boot=%s" % (digits.group(0) if digits else "unknown")
    else:
      detail += "; tail " + self.tailOfOutput()
    self.expect(name, ok, detail)
    if not ok:
      if not self.boot():
        self.stop()
        self.boot()
      return
    self.drive("A")
    self.cmd("USER 0", 20)

  def faultTest(self):
    #-- Phase 86-09: BAD86 prints its banner then executes the invalid opcode FF/7. The
    #-- firmware dumps the registers and asks "Press ENTER to return to the main menu."
    #-- (probed 2026-11-01); ENTER must reach the host menu WITHOUT a host reset, and a
    #-- relaunch must work afterwards.
    name = "fault: invalid opcode returns to the host menu without host reset"
    self.ensureGuest()
    self.drive("A")
    self.console.logCommand("BAD86")
    start = len(self.console.log)
    self.console.send("BAD86\r")
    gotBegin = self.console.waitFor("@BEGIN BAD86", 30, since=start) is not None
    gotEnter = self.console.waitFor("Press ENTER to return to the main menu", 20,
                                    since=start) is not None
    if gotEnter:
      self.console.send("\r")
    gotMenu = self.console.waitFor("Select system", 15, since=start) is not None
    relaunched = self.boot() if gotMenu else False
    ok = gotBegin and gotEnter and gotMenu and relaunched
    self.expect(name, ok,
                "begin=%s enterPrompt=%s menu=%s relaunch=%s%s"
                % (gotBegin, gotEnter, gotMenu, relaunched,
                   "" if ok else "; tail " + self.tailOfOutput()))
    if not ok:
      self.stop()
      self.boot()

  def interruptedWriteTest(self):
    #-- Phase 86-10: interrupt a multi-record PIP write on D: (A:BENCH86.CMD) once by
    #-- Ctrl+] and once by RTS reset mid-copy. The volume must stay usable afterwards:
    #-- after erasing the half-written file the full copy must run as a program from D:.
    #-- Deviation from the plan (recorded): the SD card is not host-accessible on this
    #-- board, so "reopen the image host-side" is replaced by this guest-side
    #-- full-copy-and-run check. The copy runs from A> in full-name form because PIP only
    #-- accepts its command tail while the current drive is A: (probed 2026-11-01). The
    #-- host polls Ctrl+] only when the guest touches the console, so the stop may lag
    #-- until the copy finishes; the wait covers that (documented under ISSUE-010).
    for mode in ("Ctrl+]", "RTS reset"):
      name = "interrupted write: D: stays usable after %s mid-PIP" % mode
      self.drive("A")
      self.cmd("ERA D:BENCH86.CMD", 30)
      start = len(self.console.log)
      self.console.logCommand("PIP D:BENCH86.CMD=A:BENCH86.CMD (interrupted)")
      self.console.send("PIP D:BENCH86.CMD=A:BENCH86.CMD\r")
      self.console.settle(1.0)
      if mode == "Ctrl+]":
        self.console.send("\x1d")
        stopped = self.console.waitFor(["CP/M-86 stopped by Ctrl+]", "Select system"],
                                       180, since=start) is not None
        self.console.send("\r")
        stopped = stopped and self.console.waitFor("Select system", 15, since=start) is not None
      else:
        stopped = self.console.reset()
      launched = self.boot()
      if not launched:
        self.expect(name, False, "no relaunch after %s" % mode)
        continue
      self.drive("A")
      out = self.cmd("DIR D:BENCH86.CMD", 40)
      body = hwCpm.body(out) if out is not None else ""
      partial = out is not None and "BENCH86" in body and "NO FILE" not in body.upper()
      self.cmd("ERA D:BENCH86.CMD", 30)
      out = self.cmd("PIP D:BENCH86.CMD=A:BENCH86.CMD", 180)
      copied = out is not None and hwCpm.pipOk(out) and "PIP?" not in hwCpm.body(out)
      self.drive("D")
      out = self.driveCmd("BENCH86", "D", 180)
      ran = out is not None and "@DONE PASS" in hwCpm.body(out)
      self.drive("A")
      self.cmd("ERA D:BENCH86.CMD", 30)
      self.expect(name, stopped and launched and copied and ran,
                  "stopped=%s relaunched=%s partialAfterInterrupt=%s recopy=%s runFromD=%s"
                  % (stopped, launched, partial, copied, ran))

  def manual(self):
    #-- 86-08 automated the editor/compiler workflow; only full-screen terminal behavior
    #-- (escape sequences) stays manual.
    return [("terminal escape sequences (full-screen editors)", "manual")]

  def extras(self):
    #-- timerTest/benchTest record their verdicts inline (expect), keeping each check
    #-- with its own command block; only the stop/launch cycles are collected here.
    items = []
    self.timerTest()
    self.benchTest()
    self.dirSyntaxTest()
    self.editorTest()
    self.terminalTest()
    self.warmBootTest()
    self.interruptedWriteTest()
    self.faultTest()
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
