/* CP/M-80 2.2 FCB I/O. Only standard BDOS calls; no DOS/CPM86 runtime. */
#include <cpm.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "stevie.h"
#include "save.h"
static unsigned char original[36], temporary[36], backup[36], renbuf[36];
static unsigned char record[128];
static char tempname[13], backname[13], errorbuf[100];
static int olduser;
static int call(int fn, void* p)
{
  return bdos(fn, (int)p) & 255;
}
static void finish(void)
{
  bdos(26, 128);
  bdos(32, olduser);
}
static int legal(int c)
{
  return c > 32 && c < 127 && strchr("\"*+,./:;<=>?[\\]|", c) == NULL;
}
static int parse(const char* name, unsigned char* f)
{
  int user = -1, i, n = 0, c;
  const char* p = name;
  memset(f, 0, 36);
  memset(f + 1, ' ', 11);
  while (*p >= '0' && *p <= '9')
  {
    n = n * 10 + *p++ - '0';
    if (n > 15)
      break;
  }
  if (p != name && *p == '/')
  {
    if (n > 15)
      return -2;
    user = n;
    name = p + 1;
  }
  p = name;
  if (p[0] && p[1] == ':')
  {
    c = toupper((unsigned char)*p) - 'A' + 1;
    if (c < 1 || c > 16)
      return -2;
    f[0] = c;
    p += 2;
  }
  for (i = 1; *p && *p != '.'; p++, i++)
  {
    if (i > 8 || !legal((unsigned char)*p))
      return -2;
    f[i] = toupper((unsigned char)*p);
  }
  if (i == 1)
    return -2;
  if (*p == '.')
    p++;
  for (i = 9; *p; p++, i++)
  {
    if (i > 11 || !legal((unsigned char)*p))
      return -2;
    f[i] = toupper((unsigned char)*p);
  }
  return user;
}
static int begin(const char* name)
{
  int user = parse(name, original);
  if (user == -2)
    return 0;
  olduser = bdos(32, 255) & 255;
  if (user >= 0)
    bdos(32, user);
  bdos(26, (int)record);
  return 1;
}
static void resetfcb(unsigned char* f)
{
  memset(f + 12, 0, 24);
}
static int renamef(unsigned char* from, unsigned char* to)
{
  memset(renbuf, 0, 36);
  memcpy(renbuf, from, 12);
  memcpy(renbuf + 16, to, 12);
  return call(23, renbuf) != 255;
}
static int unique(unsigned char* f, const char* ext, char* name, int reserve)
{
  unsigned n;
  for (n = 0; n < 100; n++)
  {
    memset(f, 0, 36);
    f[0] = original[0];
    sprintf(name, "VI%06u.%s", n, ext);
    memcpy(f + 1, name, 8);
    memcpy(f + 9, ext, 3);
    if (call(15, f) != 255)
    {
      call(16, f);
      continue;
    }
    resetfcb(f);
    if (!reserve || call(22, f) != 255)
      return 1;
    return 0;
  }
  return 0;
}
int vi_read_file(char* name, char* at, int nochangename)
{
  int code = 0, i, n = 0, done = 0, count = 0;
  char* dest;
  if (!begin(name))
  {
    message("Invalid CP/M filename!");
    return 2;
  }
  if (!nochangename)
    vi_file_name = strsave(name);
  if (call(15, original) == 255)
  {
    finish();
    return 1;
  }
  /* Count first: no existing text is moved until the input fits. */
  while (!done && (code = call(20, original)) == 0)
  {
    for (i = 0; i < 128; i++)
    {
      int c = record[i];
      if (!vi_binary && c == 26)
      {
        done = 1;
        break;
      }
      if (!vi_binary && c == 13)
        continue;
      if (++n > vi_file_max - vi_file_end - 1)
      {
        call(16, original);
        finish();
        message("File too long; buffer not changed.");
        return 2;
      }
    }
  }
  call(16, original);
  if (!done && code != 1)
  {
    finish();
    message("Read error; buffer not changed.");
    return 2;
  }
  resetfcb(original);
  if (call(15, original) == 255)
  {
    finish();
    message("Unable to reopen file!");
    return 2;
  }
  memmove(at + n, at, vi_file_end - at);
  dest = at;
  done = 0;
  while (!done && (code = call(20, original)) == 0)
  {
    for (i = 0; i < 128; i++)
    {
      int c = record[i];
      if (!vi_binary && c == 26)
      {
        done = 1;
        break;
      }
      if (!vi_binary && c == 13)
        continue;
      if (count >= n)
      {
        code = 2;
        done = 0;
        goto rollback;
      }
      *dest++ = c;
      count++;
    }
  }
rollback:
  if ((!done && code != 1) || count != n)
  {
    memmove(at, at + n, vi_file_end - at);
    call(16, original);
    finish();
    message("Read changed/failed; buffer not changed.");
    return 2;
  }
  call(16, original);
  finish();
  vi_file_end += n;
  message("File read. Press H for help.");
  return 0;
}
const char* vi_save_file(const char* name, const char* start, const char* end, int binary)
{
  const char* p;
  int used = 0, c, pending = 0, exists, ok = 1;
  if (!begin(name))
    return "Invalid CP/M filename!";
  tempname[0] = backname[0] = 0;
  if (!unique(temporary, "TMP", tempname, 1))
  {
    finish();
    return "Cannot create temporary file!";
  }
  p = start;
  for (;;)
  {
    if (pending)
    {
      c = 10;
      pending = 0;
    }
    else if (p < end)
    {
      c = (unsigned char)*p++;
      if (!binary && c == 10)
      {
        c = 13;
        pending = 1;
      }
    }
    else
      break;
    record[used++] = c;
    if (used == 128)
    {
      if (call(21, temporary) != 0)
      {
        ok = 0;
        break;
      }
      used = 0;
    }
  }
  if (ok)
  {
    if (!binary)
      record[used++] = 26;
    if (used)
    {
      memset(record + used, binary ? 0 : 26, 128 - used);
      if (call(21, temporary) != 0)
        ok = 0;
    }
  }
  if (call(16, temporary) == 255)
    ok = 0;
  if (!ok)
    goto fail;
  resetfcb(original);
  exists = call(15, original) != 255;
  if (exists)
  {
    if (call(16, original) == 255)
      goto fail;
    if (!unique(backup, "BAK", backname, 0))
      goto fail;
    if (!renamef(original, backup))
    {
      backname[0] = 0;
      goto fail;
    }
  }
  if (!renamef(temporary, original))
  {
    if (exists && renamef(backup, original))
      backname[0] = 0;
    goto fail;
  }
  tempname[0] = 0;
  if (exists && call(19, backup) == 255)
    goto fail;
  finish();
  return NULL;
fail:
  sprintf(errorbuf, "Save failed; retain %s%s%s (user %d)", tempname,
          tempname[0] && backname[0] ? " and " : "", backname, bdos(32, 255) & 255);
  finish();
  return errorbuf;
}
long vi_file_length(const char* name)
{
  (void)name;
  return -1L;
}
