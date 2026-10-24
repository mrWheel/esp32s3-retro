"""Shared helpers for the hardware acceptance scripts (USB console, reset, results).

The ESP32-S3 USB Serial/JTAG port resets the board when RTS is pulsed. CP/M-80,
Apple II and UCSD Pascal have no host-exit key, so these scripts return to the
menu by resetting the board. Never flashes anything. Close idf.py monitor,
minicom, etc. first: they steal the console output.
"""
import glob
import re
import sys
import time

import serial

menuPrompt = "Select system"


class Results:
  def __init__(self):
    self.items = []
    self.lines = []
    self.reportPath = None
    self.title = ""

  def say(self, line):
    self.lines.append(line)
    print(line, flush=True)
    self.flushReport()

  def flushReport(self):
    #-- Rewritten after every line, so a crash or lost port still leaves the results so far.
    if self.reportPath:
      with open(self.reportPath, "w") as handle:
        handle.write(self.title + " - " + time.strftime("%Y-%m-%d %H:%M:%S") + "\n\n")
        handle.write("\n".join(self.lines) + "\n")

  def startReport(self, path, title):
    self.reportPath = path
    self.title = title
    self.flushReport()

  def record(self, name, ok, detail=""):
    self.items.append((name, ok, detail))
    self.say(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail else ""))

  def skip(self, name, reason):
    self.items.append((name, None, reason))
    self.say("SKIP  " + name + "  -- " + reason)

  def summary(self):
    self.say("\n==== SUMMARY ====")
    failed = [item for item in self.items if item[1] is False]
    passed = sum(1 for item in self.items if item[1] is True)
    skipped = sum(1 for item in self.items if item[1] is None)
    self.say("passed %d, failed %d, skipped(manual) %d" % (passed, len(failed), skipped))
    for name, _, detail in failed:
      self.say("  FAILED: " + name + ((" -- " + detail) if detail else ""))
    return 1 if failed else 0


class Console:
  def __init__(self, port):
    self.portName = port
    self.port = serial.Serial(port, 115200, timeout=0.1)
    self.log = ""
    self.results = None

  def send(self, text):
    self.port.write(text.encode("latin-1"))

  def reconnect(self):
    #-- The USB Serial/JTAG port can re-enumerate (e.g. after a reset); retry for up to 15 s.
    try:
      self.port.close()
    except Exception:
      pass
    deadline = time.time() + 15
    while time.time() < deadline:
      try:
        self.port = serial.Serial(self.portName, 115200, timeout=0.1)
        self.port.dtr = False
        self.port.rts = False
        if self.results:
          self.results.say("<<<SERIAL PORT LOST, reconnected>>>")
        return
      except (serial.SerialException, OSError):
        time.sleep(0.5)
    if self.results:
      self.results.say("<<<SERIAL PORT LOST, reconnect failed>>>")
    raise serial.SerialException("serial port lost and could not be reopened")

  def pump(self):
    try:
      data = self.port.read(4096)
    except (serial.SerialException, OSError):
      self.reconnect()
      return False
    if data:
      self.log += data.decode("latin-1")
    return bool(data)

  def waitFor(self, patterns, timeout=20.0, since=None):
    if isinstance(patterns, str):
      patterns = [patterns]
    start = len(self.log) if since is None else since
    deadline = time.time() + timeout
    while time.time() < deadline:
      self.pump()
      segment = self.log[start:]
      for pattern in patterns:
        if pattern in segment:
          return segment
    return None

  def settle(self, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
      self.pump()

  def drain(self, seconds=0.5, maximum=10.0):
    #-- Wait until the console has been quiet for `seconds`, but never longer than `maximum`.
    hardStop = time.time() + maximum
    deadline = time.time() + seconds
    while time.time() < deadline and time.time() < hardStop:
      if self.pump():
        deadline = time.time() + seconds

  def command(self, line, prompt, timeout=30.0, eol="\r"):
    start = len(self.log)
    self.send(line + eol)
    got = self.waitFor(prompt, timeout, since=start)
    if got is None:
      return None
    self.drain(0.3)
    return self.log[start:]

  def reset(self):
    #-- RTS drives EN on the USB Serial/JTAG bridge; DTR low keeps IO0 high (normal boot).
    self.port.dtr = False
    self.port.rts = True
    time.sleep(0.1)
    self.port.rts = False
    self.log += "\n<<<RESET>>>\n"
    if self.results:
      self.results.say("<<<RESET>>>")
    return self.waitFor(menuPrompt, 20) is not None

  def toMenu(self):
    return self.reset()

  def writeLog(self, path):
    with open(path, "w") as handle:
      handle.write(self.log)


def openConsole(arguments):
  port = arguments.port
  if port is None:
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
      print("No /dev/cu.usbmodem* port found")
      sys.exit(2)
    port = ports[0]
  try:
    console = Console(port)
  except serial.SerialException as error:
    print("Cannot open " + port + ": " + str(error) + "\nClose other serial monitors first.")
    sys.exit(2)
  print("Using " + port)
  return console


class Screen:
  """Minimal ANSI terminal (cursor addressing, clear, erase) to read the Apple II text screen."""

  def __init__(self, rows=24, columns=80):
    self.rows = rows
    self.columns = columns
    self.reset()

  def reset(self):
    self.grid = [[" "] * self.columns for _ in range(self.rows)]
    self.row = 0
    self.column = 0

  def feed(self, text):
    index = 0
    while index < len(text):
      character = text[index]
      if character == "\x1b":
        match = re.match(r"\x1b\[([0-9;?]*)([A-Za-z])", text[index:])
        if match is None:
          index += 1
          continue
        parameters, final = match.group(1), match.group(2)
        index += match.end()
        numbers = [int(part) if part.isdigit() else 0 for part in parameters.replace("?", "").split(";")]
        if final == "J" and numbers[0] == 2:
          self.reset()
        elif final == "H":
          self.row = max(0, min(self.rows - 1, (numbers[0] or 1) - 1))
          self.column = max(0, min(self.columns - 1, (numbers[1] if len(numbers) > 1 and numbers[1] else 1) - 1))
        elif final == "K":
          for position in range(self.column, self.columns):
            self.grid[self.row][position] = " "
        continue
      index += 1
      if character == "\r":
        self.column = 0
      elif character == "\n":
        self.row = min(self.rows - 1, self.row + 1)
      elif character == "\b":
        self.column = max(0, self.column - 1)
      elif character >= " ":
        if self.column < self.columns:
          self.grid[self.row][self.column] = character
          self.column += 1

  def text(self):
    return "\n".join("".join(line).rstrip() for line in self.grid)
