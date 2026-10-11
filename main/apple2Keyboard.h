#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//— Longest output of one apple2KeyboardPush or apple2KeyboardFlush call
#define apple2KeyboardMaxOutput 2

//— Apple II key codes produced for terminal cursor keys
#define apple2KeyLeft 0x08
#define apple2KeyRight 0x15
#define apple2KeyUp 0x0B
#define apple2KeyDown 0x0A

typedef enum
{
  apple2KeyboardIdle,
  apple2KeyboardEscape,
  apple2KeyboardSequence
} apple2KeyboardState;

typedef struct
{
  apple2KeyboardState state;
  bool skipLineFeed;
  uint8_t sequenceLength;
} apple2Keyboard;

void apple2KeyboardInit(apple2Keyboard *keyboard, bool skipLineFeed);
size_t apple2KeyboardPush(apple2Keyboard *keyboard, uint8_t terminalByte, uint8_t *keys);
size_t apple2KeyboardFlush(apple2Keyboard *keyboard, uint8_t *keys);
bool apple2KeyboardHasPending(const apple2Keyboard *keyboard);
