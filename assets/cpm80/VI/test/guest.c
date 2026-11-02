#include "cpm80Guest.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
  unsigned char disk[cpm80SystemImageSize];
  const unsigned char* input;
  size_t pos, len;
  char output[160000];
  size_t used;
  unsigned prompts;
  bool ready;
} fixture;
static bool available(void* c)
{
  fixture* f = c;
  return f->prompts > 0 && f->pos < f->len && (f->pos < 12 || f->ready);
}
static int readchar(void* c)
{
  fixture* f = c;
  return available(f) ? f->input[f->pos++] : -1;
}
static void writechar(void* c, uint8_t b)
{
  fixture* f = c;
  assert(f->used < sizeof(f->output) - 1);
  f->output[f->used++] = b;
  if (f->used >= 2 && f->output[f->used - 2] == 'A' && b == '>')
    f->prompts++;
  if (strstr(f->output, "Normal Mode"))
    f->ready = true;
}
static bool drive(void* c, uint8_t d)
{
  (void)c;
  return d == 0;
}
static bool profile(void* c, uint8_t d, cpm80DiskProfile* p)
{
  (void)c;
  if (d != 0)
    return false;
  *p = cpm80DiskProfileSystem;
  return true;
}
static bool rd(void* c, uint8_t d, uint16_t t, uint16_t s, uint8_t* r)
{
  fixture* f = c;
  if (d || t >= 77 || s >= 26)
    return false;
  memcpy(r, f->disk + (t * 26 + s) * 128, 128);
  return true;
}
static bool wr(void* c, uint8_t d, uint16_t t, uint16_t s, const uint8_t* r)
{
  fixture* f = c;
  if (d || t >= 77 || s >= 26)
    return false;
  memcpy(f->disk + (t * 26 + s) * 128, r, 128);
  return true;
}
static void yield(void* c)
{
  (void)c;
}
static void load(const char* p, unsigned char* b, size_t n)
{
  FILE* f = fopen(p, "rb");
  assert(f);
  assert(fread(b, 1, n, f) == n);
  fclose(f);
}
int main(int argc, char** argv)
{
  assert(argc == 6);
  fixture* f = calloc(1, sizeof(*f));
  cpm80Guest* g = calloc(1, sizeof(*g));
  assert(f && g);
  uint8_t ccp[cpm80CcpSize], bdos[cpm80BdosSize], keys[8192];
  load(argv[1], ccp, sizeof(ccp));
  load(argv[2], bdos, sizeof(bdos));
  load(argv[3], f->disk, sizeof(f->disk));
  FILE* k = fopen(argv[4], "rb");
  assert(k);
  f->len = fread(keys, 1, sizeof(keys), k);
  fclose(k);
  f->input = keys;
  cpm80HostOps ops = {.consoleAvailable = available,
                      .consoleRead = readchar,
                      .consoleWrite = writechar,
                      .diskDriveAvailable = drive,
                      .diskDriveProfile = profile,
                      .diskReadRecord = rd,
                      .diskWriteRecord = wr,
                      .yield = yield,
                      .context = f};
  assert(cpm80GuestInitialize(g, &ops, ccp, bdos) == cpm80GuestInitOk);
  assert(cpm80GuestColdBoot(g));
  unsigned n;
  for (n = 0; n < 200000000 && f->prompts < 2; n++)
    assert(cpm80GuestStep(g));
  FILE* log = fopen(argv[5], "wb");
  assert(log);
  fwrite(f->output, 1, f->used, log);
  fclose(log);
  if (f->prompts != 2)
  {
    fprintf(stderr, "No return to CCP; pc=%x keys=%zu/%zu output=%s\n", g->cpu.processor.pc, f->pos,
            f->len, f->output);
    abort();
  }
  assert(f->pos == f->len);
  FILE* out = fopen(argv[3], "wb");
  assert(out);
  assert(fwrite(f->disk, 1, sizeof(f->disk), out) == sizeof(f->disk));
  fclose(out);
  printf("PASS full guest: real CCP + BDOS + BIOS + Z80 core, %u instructions\n", n);
  cpm80GuestDestroy(g);
  free(g);
  free(f);
  return 0;
}
