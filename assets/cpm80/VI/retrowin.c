/* RETRO80 serial console: CP/M BDOS + VT100/ANSI, no PC BIOS. */
#include <stdio.h>
#include <stdlib.h>
#include "stevie.h"
#include "keyboard.h"
#include "gitver.h"
#include <cpm.h>
char* viversion(void)
{
  return "VI RETRO80 VT100/ANSI " GIT_VERSION;
}
void windusage(void)
{
  fputs("Usage: VI [-xo] [user/][drive:]file\n", stderr);
}
void windputc(int c)
{
  bdos(6, c & 255);
}
void windstr(char* s)
{
  while (*s)
    windputc((unsigned char)*s++);
}
void windinit(void)
{
  vi_rows = 24;
  vi_columns = 80;
  vi_key_init(1);
  /* Absolute positioning with wrapping off avoids bottom-right scrolling. */
  windstr("\033[0m\033[r\033[?7l\033[?1l\033[?2004l\033[?25h");
}
void windgoto(int r, int c)
{
  char b[24];
  sprintf(b, "\033[%d;%dH", r + 1, c + 1);
  windstr(b);
}
void windclear(void)
{
  windstr("\033[2J\033[H");
}
void windcursor(int on)
{
  windstr(on ? "\033[?25h" : "\033[?25l");
}
void windcolor(int fg)
{
  (void)fg;
  windstr("\033[7m");
}
void windcolorreset(void)
{
  windstr("\033[0m");
}
void windrefresh(void)
{
}
void windrefreshcursor(void)
{
}
void beep(void)
{
  windputc(7);
}
void windexit(int r)
{
  vi_key_exit();
  exit(r);
}
