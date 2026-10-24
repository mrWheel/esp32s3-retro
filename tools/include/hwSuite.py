"""Common hardware test sequence shared by cpm80, cpm86, ucsd and apple2.

Every system runs the same steps with the same result names. A system supplies an
Adapter that knows the system specific commands (see tools/<system>/hardwareTest.py).
"""
import os
import re

import hwCommon


class Adapter:
  title = ""
  logName = ""
  promptName = "prompt"
  volumes = []
  rwVolumes = []
  fillVolume = ""
  roVolume = ""
  fillLimit = 60
  timeLimit = 10.0

  def __init__(self, console):
    self.console = console

  def boot(self):
    raise NotImplementedError

  def basic(self):
    return []

  def readVolume(self, volume):
    raise NotImplementedError

  def writeFile(self, volume):
    raise NotImplementedError

  def fileExists(self, volume):
    raise NotImplementedError

  def copyFile(self, source, target):
    raise NotImplementedError

  def deleteFile(self, volume):
    raise NotImplementedError

  def freeSpace(self, volume):
    raise NotImplementedError

  def fillUp(self):
    raise NotImplementedError

  def cleanupFill(self):
    raise NotImplementedError

  def roState(self):
    raise NotImplementedError

  def readOnlyWrite(self):
    raise NotImplementedError

  def recovered(self):
    raise NotImplementedError

  def timed(self):
    raise NotImplementedError

  def unconfigured(self):
    return []

  def extras(self):
    return []

  def manual(self):
    return []


def small(before, after):
  return before is not None and after is not None and after <= max(2, before * 0.05)


def run(adapter, arguments, directory):
  console = adapter.console
  results = hwCommon.Results()
  record = results.record
  results.startReport(os.path.join(directory, adapter.logName), adapter.title + " hardware test")
  console.results = results

  ok = console.reset()
  record("board reset reaches host menu", ok)
  if not ok:
    print("Menu not reached; press the reset button and retry.")
    return 2
  record("menu reports PSRAM line", "PSRAM:" in console.log)

  ok = adapter.boot()
  record("cold boot reaches " + adapter.promptName, ok)
  if not ok:
    print(console.log[-1500:])
    return 1
  for item in adapter.basic():
    record(*item)

  for volume in adapter.volumes:
    ok, detail = adapter.readVolume(volume)
    record("read: list volume " + volume, ok, detail)

  first, second = adapter.rwVolumes[0], adapter.rwVolumes[1]
  if arguments.no_write:
    results.skip("write / persistence / disk full", "--no-write given")
  else:
    adapter.deleteFile(first)
    adapter.deleteFile(second)
    record("write: create file on " + first, adapter.writeFile(first))
    record("write: file visible on " + first, adapter.fileExists(first))
    record("write: copy " + first + " -> " + second, adapter.copyFile(first, second))
    record("write: file visible on " + second, adapter.fileExists(second))

    record("persistence: board reset reaches host menu", console.reset())
    record("persistence: cold boot after reset", adapter.boot())
    record("persistence: file survives reset on " + first, adapter.fileExists(first))
    record("persistence: file survives reset on " + second, adapter.fileExists(second))
    adapter.deleteFile(first)
    adapter.deleteFile(second)
    record("delete: file removed from " + first, not adapter.fileExists(first))
    record("delete: file removed from " + second, not adapter.fileExists(second))

    #-- Fill the smallest RW volume with dummy files until the system reports it is full.
    volume = adapter.fillVolume
    before = adapter.freeSpace(volume)
    full, count = adapter.fillUp()
    record("disk full: filling " + volume + " ends with a disk-full error", full, "after %d files" % count)
    whenFull = adapter.freeSpace(volume)
    record("disk full: volume reports (almost) no free space", small(before, whenFull),
           "free before %s, when full %s" % (before, whenFull))
    adapter.cleanupFill()
    after = adapter.freeSpace(volume)
    record("disk full: deleting the fill files restores all free space", before is not None and before == after,
           "before %s, after %s" % (before, after))
    ok = adapter.writeFile(volume)
    record("disk full: writing works again", ok)
    adapter.deleteFile(volume)

  stateBefore = adapter.roState()
  refused, detail = adapter.readOnlyWrite()
  record("read-only: write to " + adapter.roVolume + " is refused", refused, detail)
  record("read-only: system recovers to a prompt", adapter.recovered())
  stateAfter = adapter.roState()
  record("read-only: no file created on " + adapter.roVolume, not stateAfter[0])
  unchanged = stateBefore[1] is not None and stateBefore[1] == stateAfter[1]
  record("read-only: free space on " + adapter.roVolume + (" unchanged" if unchanged else " CHANGED"),
         unchanged, "before %s, after %s" % (stateBefore[1], stateAfter[1]))

  seconds = adapter.timed()
  record("timing: volume listing completes in < %d s" % adapter.timeLimit,
         seconds is not None and seconds < adapter.timeLimit,
         "%.1f s" % seconds if seconds is not None else "timeout")

  for item in adapter.unconfigured():
    record(*item)
  for item in adapter.extras():
    record(*item)

  record("final reset returns to menu", console.reset())

  for name, reason in adapter.manual():
    results.skip(name, reason)
  code = results.summary()
  print("Report: " + os.path.join(directory, adapter.logName))
  return code


def freeFrom(pattern, text):
  found = re.search(pattern, text or "")
  return int(found.group(1)) if found else None
