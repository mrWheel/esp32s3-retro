#include "tiny8086.h"
#include "cpm86SystemInfo.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
  cpm86InfoStream stream;
  cpm86InfoValues values;
  unsigned snapshots, keys;
  bool supported, badHeader, stopped;
  char output[40000];
  size_t used;
  const char* input;
} fixture;
static bool readPort(void* context, uint16_t port, uint8_t* value)
{
  fixture* f = context;
  if (port == 0xf8)
  {
    *value = f->supported ? 0xa6 : 0xff;
    return true;
  }
  if (port == 0xf9)
  {
    *value = cpm86InfoRead(&f->stream);
    return true;
  }
  assert(!"unexpected port read");
  return false;
}
static bool writePort(void* context, uint16_t port, uint8_t value)
{
  fixture* f = context;
  assert(port == 0xf8 && value == 1);
  f->snapshots++;
  cpm86InfoBuild(&f->stream, &f->values);
  if (f->badHeader)
    f->stream.text[0] = 'X';
  return true;
}
static bool interrupt(void* context, tiny8086* cpu, uint8_t vector)
{
  fixture* f = context;
  assert(vector == 0xe0);
  unsigned function = cpu->reg[tiny8086RegCx] & 255;
  unsigned dl = cpu->reg[tiny8086RegDx] & 255;
  switch (function)
  {
  case 0:
    f->stopped = true;
    break;
  case 6:
    if (dl == 255)
    {
      assert(f->keys < strlen(f->input));
      cpu->reg[tiny8086RegAx] = (unsigned char)f->input[f->keys++];
    }
    else
    {
      assert(f->used < sizeof(f->output) - 1);
      f->output[f->used++] = (char)dl;
    }
    break;
  case 12:
    cpu->reg[tiny8086RegAx] = 0x1122;
    break;
  case 25:
    cpu->reg[tiny8086RegAx] = 1;
    break;
  case 32:
    assert(dl == 255);
    cpu->reg[tiny8086RegAx] = 3;
    break;
  default:
    assert(!"unexpected BDOS call");
  }
  return true;
}
static void run(const char* filename, bool supported, bool badHeader, const char* keys,
                unsigned snapshots)
{
  fixture* f = calloc(1, sizeof(*f));
  assert(f);
  f->supported = supported;
  f->badHeader = badHeader;
  f->input = keys;
  f->values.version = "v-test";
  f->values.guestRamBytes = 640 * 1024;
  f->values.psramBytes = 8 * 1024 * 1024;
  f->values.uptimeMs = 123456;
  f->values.instructions = 9876543210ULL;
  for (unsigned i = 0; i < 6; i++)
  {
    f->values.drives[i] = (cpm86InfoDrive){true, i == 1, 8392704,
                                           "/sdcard/retro/images/cpm86/disk image with spaces.dsk"};
  }
  uint8_t* memory = calloc(1, 0x100000);
  assert(memory);
  FILE* input = fopen(filename, "rb");
  assert(input);
  assert(fseek(input, 128, SEEK_SET) == 0);
  size_t bytes = fread(memory + 0x10000, 1, 65536, input);
  assert(bytes > 256);
  fclose(input);
  tiny8086 cpu;
  tiny8086Config cfg = {memory, 0x100000, readPort, writePort, interrupt, f};
  assert(tiny8086Init(&cpu, &cfg) == tiny8086Ok);
  cpu.seg[tiny8086SegCs] = cpu.seg[tiny8086SegDs] = cpu.seg[tiny8086SegEs] =
      cpu.seg[tiny8086SegSs] = 0x1000;
  cpu.ip = 0x100;
  for (unsigned i = 0; !f->stopped && i < 1000000; i++)
  {
    tiny8086Result result = tiny8086Step(&cpu);
    if (result != tiny8086Ok)
    {
      fprintf(stderr, "Fault %u at %x\n", result, cpu.faultStartIp);
      abort();
    }
  }
  assert(f->stopped);
  assert(f->snapshots == snapshots);
  if (!supported)
    assert(strstr(f->output, "interface is not available"));
  else if (badHeader)
    assert(strstr(f->output, "Invalid or unsupported"));
  else
  {
    assert(strstr(f->output, "640 KiB"));
    assert(strstr(f->output, "9876543210"));
    assert(strstr(f->output, "read-only"));
    assert(strstr(f->output, "Mounted: 6 of 6"));
    assert(strstr(f->output, "1122h"));
    assert(strstr(f->output, "User area: 3"));
    assert(strstr(f->output, "Type any key to continue"));
    assert(strstr(f->output, "\033[2K"));
    assert(strstr(f->output, "\033[A"));
  }
  assert(strstr(f->output, "\033[?25h"));
  printf("PASS: supported=%u badHeader=%u snapshots=%u, %zu output bytes\n", supported, badHeader,
         snapshots, f->used);
  free(memory);
  free(f);
}
int main(int argc, char** argv)
{
  assert(argc == 2);
  //-- First draw pauses at the continue prompt (any key), r rebuilds (second snapshot),
  //-- the space continues that second pause and q exits at the final prompt, so two
  //-- snapshots and four keys are used.
  run(argv[1], true, false, " r q", 2);
  run(argv[1], false, false, "q", 0);
  run(argv[1], true, true, "q", 1);
  cpm86InfoStream s;
  cpm86InfoValues v = {0};
  v.version = "bad\033[2J\nname";
  cpm86InfoBuild(&s, &v);
  assert(!strchr(s.text, 27));
  for (size_t i = 0; i < s.length; i++)
    assert(cpm86InfoRead(&s));
  assert(cpm86InfoRead(&s) == 0 && cpm86InfoRead(&s) == 0);
  puts("PASS: snapshot sanitization and end-of-stream");
  return 0;
}
