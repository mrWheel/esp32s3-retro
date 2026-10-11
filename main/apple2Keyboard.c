#include "apple2Keyboard.h"

static const uint8_t escapeKey = 0x1B;
static const uint8_t maxSequenceParameters = 8;

//— Applies the CR/LF/DEL normalisation to one ordinary terminal byte
static size_t normalizeByte(apple2Keyboard *keyboard, uint8_t terminalByte, uint8_t *keys)
{
  if (terminalByte == '\n' && keyboard->skipLineFeed)
  {
    keyboard->skipLineFeed = false;
    return 0;
  }
  keyboard->skipLineFeed = false;
  if (terminalByte == '\r')
  {
    keyboard->skipLineFeed = true;
    keys[0] = '\r';
    return 1;
  }
  if (terminalByte == '\n')
  {
    keys[0] = '\r';
    return 1;
  }
  if (terminalByte == 8 || terminalByte == 127)
  {
    keys[0] = 8;
    return 1;
  }
  if (terminalByte > 0x7F)
  {
    return 0;
  }
  keys[0] = terminalByte;
  return 1;
}

static size_t cursorKey(uint8_t finalByte, uint8_t *keys)
{
  switch (finalByte)
  {
    case 'A':
      keys[0] = apple2KeyUp;
      return 1;
    case 'B':
      keys[0] = apple2KeyDown;
      return 1;
    case 'C':
      keys[0] = apple2KeyRight;
      return 1;
    case 'D':
      keys[0] = apple2KeyLeft;
      return 1;
    default:
      return 0;
  }
}

void apple2KeyboardInit(apple2Keyboard *keyboard, bool skipLineFeed)
{
  keyboard->state = apple2KeyboardIdle;
  keyboard->skipLineFeed = skipLineFeed;
  keyboard->sequenceLength = 0;
}

size_t apple2KeyboardPush(apple2Keyboard *keyboard, uint8_t terminalByte, uint8_t *keys)
{
  switch (keyboard->state)
  {
    case apple2KeyboardIdle:
      if (terminalByte == escapeKey)
      {
        keyboard->state = apple2KeyboardEscape;
        return 0;
      }
      return normalizeByte(keyboard, terminalByte, keys);

    case apple2KeyboardEscape:
      //— ESC [ and ESC O start a cursor/function key sequence (normal and application cursor mode)
      if (terminalByte == '[' || terminalByte == 'O')
      {
        keyboard->state = apple2KeyboardSequence;
        keyboard->sequenceLength = 0;
        return 0;
      }
      //— ESC followed by an ordinary key is a guest keystroke pair (Apple II ESC editing)
      keys[0] = escapeKey;
      if (terminalByte == escapeKey)
      {
        return 1;
      }
      keyboard->state = apple2KeyboardIdle;
      return 1 + normalizeByte(keyboard, terminalByte, keys + 1);

    case apple2KeyboardSequence:
      if (terminalByte >= 0x40 && terminalByte <= 0x7E)
      {
        keyboard->state = apple2KeyboardIdle;
        return cursorKey(terminalByte, keys);
      }
      if (terminalByte >= 0x20 && terminalByte <= 0x3F && keyboard->sequenceLength < maxSequenceParameters)
      {
        ++keyboard->sequenceLength;
        return 0;
      }
      //— Malformed sequence: drop it
      keyboard->state = apple2KeyboardIdle;
      return 0;
  }
  return 0;
}

size_t apple2KeyboardFlush(apple2Keyboard *keyboard, uint8_t *keys)
{
  apple2KeyboardState state = keyboard->state;
  keyboard->state = apple2KeyboardIdle;
  if (state == apple2KeyboardEscape)
  {
    //— A lone ESC key press
    keys[0] = escapeKey;
    return 1;
  }
  return 0;
}

bool apple2KeyboardHasPending(const apple2Keyboard *keyboard)
{
  return keyboard->state != apple2KeyboardIdle;
}
