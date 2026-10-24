"""CP/M adapter shared by CP/M-80 and CP/M-86 (PIP / STAT / DIR on the console)."""
import time

import hwSuite


def body(out):
  #-- Drop the echoed command line so a file name typed by us is not seen as output.
  if out is None:
    return ""
  return out.split("\n", 1)[1] if "\n" in out else out


class CpmAdapter(hwSuite.Adapter):
  menuKey = ""
  promptName = "A> prompt"
  extension = ".COM"
  freePattern = r"(?:Remaining On [A-P]:|Free Space:)\s*(\d+)k"
  roVolume = "A:"

  def cmd(self, line, timeout=60.0):
    return self.console.command(line, "A>", timeout)

  def boot(self):
    self.console.send(self.menuKey + "\r")
    return self.console.waitFor("A>", 60) is not None

  def readVolume(self, volume):
    letter = volume[0]
    if volume != "A:":
      out = self.console.command(letter + ":", letter + ">", 15)
      if out is None:
        self.console.send("\x03")
        self.cmd("A:", 10)
        return False, "drive did not select"
    out = self.console.command("DIR", letter + ">" if volume != "A:" else "A>", 40)
    if volume != "A:":
      self.cmd("A:", 10)
    return out is not None and "Bdos" not in out, "" if out else "timeout"

  def file(self, volume):
    return volume + "HWTEST" + self.extension

  def writeFile(self, volume):
    self.cmd("ERA " + self.file(volume), 30)
    out = self.cmd("PIP %s=A:PIP%s[V]" % (self.file(volume), self.extension), 180)
    return out is not None and "ERROR" not in out.upper() and "Bdos" not in out

  def fileExists(self, volume):
    out = self.cmd("DIR " + self.file(volume), 40)
    text = body(out)
    return "HWTEST" in text and "NO FILE" not in text.upper()

  def copyFile(self, source, target):
    out = self.cmd("PIP %s=%s[V]" % (self.file(target), self.file(source)), 180)
    return out is not None and "ERROR" not in out.upper() and "Bdos" not in out

  def deleteFile(self, volume):
    self.cmd("ERA " + self.file(volume), 40)

  def freeSpace(self, volume):
    out = self.cmd("STAT " + volume, 30)
    return hwSuite.freeFrom(self.freePattern, out)

  def fillUp(self):
    drive = self.fillVolume
    count = 0
    for index in range(1, self.fillLimit + 1):
      out = self.cmd("PIP %sFILL%02d.TMP=A:PIP%s[V]" % (drive, index, self.extension), 180)
      count = index
      if out is None:
        self.console.send("\x03")
        return False, count
      if "ERROR" in out.upper() or "Bdos" in out:
        return True, count
    return False, count

  def cleanupFill(self):
    self.console.send("\x03")
    self.console.settle(1)
    self.cmd("ERA %sFILL*.TMP" % self.fillVolume, 120)
    self.cmd("ERA %s*.$$$" % self.fillVolume, 60)

  def roState(self):
    out = self.cmd("DIR A:HWRO*.*", 40)
    exists = "HWRO" in body(out) and "NO FILE" not in body(out).upper()
    return exists, self.freeSpace("A:")

  def readOnlyWrite(self):
    self.console.send("PIP A:HWRO%s=A:PIP%s\r" % (self.extension, self.extension))
    got = self.console.waitFor(["Bad Sector", "R/O"], 90)
    return got is not None, "" if got else "no Bdos error seen"

  def recovered(self):
    self.console.send("\x03")
    return self.console.waitFor("A>", 20) is not None

  def timed(self):
    start = time.time()
    out = self.cmd("DIR", 60)
    return time.time() - start if out is not None else None

  def manual(self):
    return [("editor / compiler workflow", "manual"),
            ("terminal escape sequences (full-screen editors)", "manual")]
