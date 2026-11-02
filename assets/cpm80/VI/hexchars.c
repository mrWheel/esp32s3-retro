/* Mutable character-display table; never modify pooled string literals. */
#include <stddef.h>
#include "stevie.h"
struct charinfo chars[256];
static char displays[159][6];
static void setup(int base)
{
  int c, n = 0;
  char* p;
  for (c = 0; c < 256; c++)
  {
    chars[c].ch_size = 1;
    chars[c].ch_str = NULL;
    if ((c >= 32 && c <= 126) || c == 10 || c == 13)
      continue;
    p = displays[n++];
    p[0] = '[';
    p[4] = ']';
    p[5] = 0;
    if (base == 16)
    {
      p[1] = 'x';
      p[2] = "0123456789abcdef"[c >> 4];
      p[3] = "0123456789abcdef"[c & 15];
    }
    else if (base == 8)
    {
      p[1] = '0' + (c >> 6);
      p[2] = '0' + ((c >> 3) & 7);
      p[3] = '0' + (c & 7);
    }
    else
    {
      p[1] = c >= 100 ? '0' + c / 100 : ' ';
      p[2] = c >= 10 ? '0' + c / 10 % 10 : ' ';
      p[3] = '0' + c % 10;
    }
    chars[c].ch_size = 5;
    chars[c].ch_str = p;
  }
}
void octchars(void)
{
  setup(8);
}
void hexchars(void)
{
  setup(16);
}
void decchars(void)
{
  setup(10);
}
int hextoint(int c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
