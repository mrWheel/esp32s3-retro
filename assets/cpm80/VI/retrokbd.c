/* Decode terminal bytes before they reach vi's command/insert parsers. */
#include "keyboard.h"
#include <cpm.h>
static int pending;
static int pollkey(void)
{
  return bdos(6, 255) & 255;
}
static int rawkey(void)
{
  int c;
  if (pending)
  {
    c = pending;
    pending = 0;
    return c;
  }
  while (!(c = pollkey()))
    ;
  return c;
}
static int continuation(void)
{
  int n, c;
  /* No wall-clock service required; bounded so lone ESC always returns. */
  for (n = 0; n < 1500; n++)
    if ((c = pollkey()) != 0)
      return c;
  return -1;
}
void vi_key_init(int ansi)
{
  (void)ansi;
  pending = 0;
}
void vi_key_exit(void)
{
  const char* p = "\033[0m\033[r\033[?7h\033[?25h\033[24;1H\033[K\r\n";
  while (*p)
    bdos(6, *p++);
  pending = 0;
}
int vi_key_concurrent(void)
{
  return 1;
} /* always use local BDOS path */
void vi_key_unread(int c)
{
  pending = c;
}
int vi_key_read(void)
{
  int c, first, n, value, have;
  for (;;)
  {
    c = rawkey();
    if (c != 27)
      return c;
    c = continuation();
    if (c < 0)
      return 27;
    if (c != '[' && c != 'O')
    {
      pending = c;
      return 27;
    }
    first = c;
    value = 0;
    have = 0;
    for (n = 0; n < 24; n++)
    {
      c = continuation();
      if (c < 0)
        break;
      if (c >= 0x40 && c <= 0x7e)
      {
        switch (c)
        {
        case 'A':
          return VI_UP;
        case 'B':
          return VI_DOWN;
        case 'C':
          return VI_RIGHT;
        case 'D':
          return VI_LEFT;
        case 'H':
          return VI_HOME;
        case 'F':
          return VI_END;
        case 'I':
          if (first == '[')
            return VI_PGUP;
          break;
        case '~':
          switch (value)
          {
          case 1:
          case 7:
            return VI_HOME;
          case 4:
          case 8:
            return VI_END;
          case 2:
            return VI_INSERT;
          case 3:
            return VI_DELETE;
          case 5:
            return VI_PGUP;
          case 6:
            return VI_PGDOWN;
          }
        }
        break; /* unknown sequences are consumed, never inserted */
      }
      if (c == ';')
        have = 1; /* modifiers do not change the base key */
      else if (!have && c >= '0' && c <= '9')
      {
        if (value < 1000)
          value = value * 10 + c - '0';
      }
    }
  }
}
