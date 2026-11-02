#include "cpm80Cpu.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct
{
  char name[12];
  unsigned char data[16000];
  unsigned size;
  int exists;
} file;
typedef struct
{
  cpm80Cpu cpu;
  file files[12];
  const unsigned char* keys;
  unsigned keypos, keylen, polls, dma, user;
  int stopped, failwrite, failrename;
  char output[150000];
  unsigned used;
} fixture;
static uint8_t inport(void* c, uint8_t p)
{
  (void)c;
  (void)p;
  assert(!"unexpected port input");
  return 0;
}
static void outport(void* c, uint8_t p, uint8_t v)
{
  (void)c;
  (void)p;
  (void)v;
  assert(!"unexpected port output");
}
static file* lookup(fixture* f, const char* n, int create)
{
  file* empty = NULL;
  for (int i = 0; i < 12; i++)
  {
    if (f->files[i].exists && !memcmp(n, f->files[i].name, 11))
      return &f->files[i];
    if (!f->files[i].exists)
      empty = &f->files[i];
  }
  if (!create)
    return NULL;
  assert(empty);
  memcpy(empty->name, n, 11);
  empty->name[11] = 0;
  empty->size = 0;
  empty->exists = 1;
  return empty;
}
static void output(fixture* f, unsigned char c)
{
  assert(f->used < sizeof(f->output) - 1);
  f->output[f->used++] = c;
}
static void bdos(fixture* f)
{
  z80* z = &f->cpu.processor;
  unsigned dx = z->d * 256 + z->e, fn = z->c, result = 0;
  unsigned char* p = f->cpu.memory + dx;
  char name[12];
  memcpy(name, p + 1, 11);
  name[11] = 0;
  file* fi;
  unsigned r, n;
  switch (fn)
  {
  case 0:
    f->stopped = 1;
    break;
  case 2:
    output(f, z->e);
    break;
  case 9:
    while (*p != '$')
      output(f, *p++);
    break;
  case 6:
    if (z->e == 255)
    {
      if (f->keypos < f->keylen)
        result = f->keys[f->keypos++];
      else
      {
        f->polls++;
        if (f->polls > 20000)
        {
          fprintf(stderr, "Out of keys at %x\n%.*s\n", z->pc,
                  (int)(f->used > 2000 ? 2000 : f->used),
                  f->output + (f->used > 2000 ? f->used - 2000 : 0));
          abort();
        }
      }
    }
    else
      output(f, z->e);
    break;
  case 12:
    result = 0x22;
    break;
  case 14:
    result = 0;
    break;
  case 25:
    result = 0;
    break;
  case 26:
    f->dma = dx;
    break;
  case 32:
    if (z->e != 255)
      f->user = z->e;
    result = f->user;
    break;
  case 15:
    fi = lookup(f, name, 0);
    result = fi ? 0 : 255;
    break;
  case 16:
    result = 0;
    break;
  case 19:
    fi = lookup(f, name, 0);
    if (fi)
      fi->exists = 0;
    else
      result = 255;
    break;
  case 22:
    if (lookup(f, name, 0))
      result = 255;
    else
    {
      lookup(f, name, 1);
      p[12] = p[32] = 0;
    }
    break;
  case 20:
  case 21:
    fi = lookup(f, name, 0);
    assert(fi);
    r = (p[12] * 128 + p[32]) * 128;
    if (fn == 20)
    {
      if (r >= fi->size)
        result = 1;
      else
      {
        n = fi->size - r;
        if (n > 128)
          n = 128;
        memset(f->cpu.memory + f->dma, 26, 128);
        memcpy(f->cpu.memory + f->dma, fi->data + r, n);
      }
    }
    else if (f->failwrite)
      result = 2;
    else
    {
      assert(r + 128 <= sizeof(fi->data));
      memcpy(fi->data + r, f->cpu.memory + f->dma, 128);
      if (fi->size < r + 128)
        fi->size = r + 128;
    }
    if (!result)
    {
      p[32]++;
      if (p[32] == 128)
      {
        p[32] = 0;
        p[12]++;
      }
    }
    break;
  case 23:
    fi = lookup(f, name, 0);
    assert(fi);
    memcpy(name, p + 17, 11);
    if (f->failrename || lookup(f, name, 0))
      result = 255;
    else
      memcpy(fi->name, name, 11);
    break;
  default:
    fprintf(stderr, "Unexpected BDOS %u at PC %x\n", fn, z->pc);
    abort();
  }
  z->a = result;
  z->l = result;
  z->h = 0;
  z->pc = f->cpu.memory[z->sp] + 256 * f->cpu.memory[(uint16_t)(z->sp + 1)];
  z->sp += 2;
}
static fixture* setup(const char* cmd, const unsigned char* keys, unsigned len,
                      const char* original)
{
  fixture* f = calloc(1, sizeof(*f));
  assert(f);
  assert(cpm80CpuInitialize(&f->cpu, inport, outport, f));
  f->keys = keys;
  f->keylen = len;
  f->dma = 128;
  f->user = 0;
  FILE* in = fopen(cmd, "rb");
  assert(in);
  unsigned n = fread(f->cpu.memory + 256, 1, 0xc300, in);
  assert(n > 1000);
  fclose(in);
  f->cpu.memory[0] = 0xc3;
  f->cpu.memory[1] = 3;
  f->cpu.memory[2] = 0xda;
  f->cpu.memory[5] = 0xc3;
  f->cpu.memory[6] = 0;
  f->cpu.memory[7] = 0xcc;
  const char* tail = " TEST.TXT";
  f->cpu.memory[128] = strlen(tail);
  memcpy(f->cpu.memory + 129, tail, strlen(tail));
  f->cpu.memory[129 + strlen(tail)] = 0;
  f->cpu.processor.pc = 256;
  f->cpu.processor.sp = 0xc3fe;
  f->cpu.memory[0xc3fe] = 0;
  f->cpu.memory[0xc3ff] = 0;
  memset(f->cpu.memory + 0xcc00, 0xa5, 0x3400);
  if (original)
  {
    file* fi = lookup(f, "TEST    TXT", 1);
    fi->size = strlen(original);
    memcpy(fi->data, original, fi->size);
  }
  return f;
}
static void run(fixture* f)
{
  unsigned i;
  for (i = 0; i < 100000000 && !f->stopped; i++)
  {
    unsigned pc = f->cpu.processor.pc;
    if (pc == 0)
    {
      f->stopped = 1;
      break;
    }
    if (pc == 5)
    {
      bdos(f);
      continue;
    }
    assert(cpm80CpuStep(&f->cpu));
  }
  if (!f->stopped)
  {
    fprintf(stderr, "Timeout at %x keys %u/%u\n", f->cpu.processor.pc, f->keypos, f->keylen);
    abort();
  }
  for (i = 0xcc00; i < 65536; i++)
    assert(f->cpu.memory[i] == 0xa5);
}
static void check(fixture* f, const char* name, const char* want)
{
  file* fi = lookup(f, name, 0);
  assert(fi);
  unsigned n = strlen(want);
  if (fi->size < n || memcmp(fi->data, want, n))
  {
    fprintf(stderr, "Expected '%s', got '%.*s'\n", want, (int)fi->size, fi->data);
    abort();
  }
  if (fi->size > n)
    assert(fi->data[n] == 26);
}
static void done(fixture* f)
{
  cpm80CpuDestroy(&f->cpu);
  free(f);
}
#define CASE(k, o) setup(argv[1], (const unsigned char*)k, sizeof(k) - 1, o)
int main(int argc, char** argv)
{
  assert(argc == 2);
  fixture* f;
  f = CASE("iHello world\033:wq\r", NULL);
  run(f);
  check(f, "TEST    TXT", "Hello world\r\n");
  done(f);
  puts("PASS Z80 COM: startup, heap, insert, create, save and exit");
  f = CASE("iX\033:wq\r", "old\r\n");
  run(f);
  check(f, "TEST    TXT", "Xold\r\n");
  done(f);
  puts("PASS Z80 COM: transactional replacement of existing file");
  f = CASE("\033[B\033[Cxi!\033:wq\r", "one\r\ntwo\r\nthree\r\n");
  run(f);
  check(f, "TEST    TXT", "one\r\nt!o\r\nthree\r\n");
  done(f);
  puts("PASS Z80 COM: ANSI arrows, delete and insert");
  f = CASE("iX\033u:wq\r", "old\r\n");
  run(f);
  check(f, "TEST    TXT", "old\r\n");
  done(f);
  puts("PASS Z80 COM: undo");
  f = CASE("/two\rdd:wq\r", "one\r\ntwo\r\nthree\r\n");
  run(f);
  check(f, "TEST    TXT", "one\r\nthree\r\n");
  done(f);
  puts("PASS Z80 COM: search and shorter rewrite");
  f = CASE("iX\033:w\r:q\r:q!\r", "original\r\n");
  f->failwrite = 1;
  run(f);
  check(f, "TEST    TXT", "original\r\n");
  assert(strstr(f->output, "File not written out"));
  done(f);
  puts("PASS Z80 COM: failed write preserves original and dirty flag");
  f = CASE("iX\033[CiY\033:wq\r", "ab\r\n");
  run(f);
  check(f, "TEST    TXT", "XaYb\r\n");
  done(f);
  puts("PASS Z80 COM: arrow finalizes insertion safely");
  f = CASE("\033OFx\033[1~i!\033:wq\r", "abc\r\n");
  run(f);
  check(f, "TEST    TXT", "!ab\r\n");
  done(f);
  puts("PASS Z80 COM: SS3 End and CSI Home");
  f = CASE("yyjp:wq\r", "one\r\ntwo\r\n");
  run(f);
  check(f, "TEST    TXT", "one\r\ntwo\r\none\r\n");
  done(f);
  puts("PASS Z80 COM: yank and put");
  f = CASE(":r MISSING.TXT\r:wq\r", "keep\r\n");
  run(f);
  check(f, "TEST    TXT", "keep\r\n");
  done(f);
  puts("PASS Z80 COM: missing include preserves buffer");
  f = CASE("/two\rnx:wq\r", "one two two\r\n");
  run(f);
  check(f, "TEST    TXT", "one two wo\r\n");
  done(f);
  puts("PASS Z80 COM: repeated search");
  f = CASE("iX\033:r BIG.TXT\r:wq\r", "keep\r\n");
  file* big = lookup(f, "BIG     TXT", 1);
  big->size = 8000;
  memset(big->data, 'a', 8000);
  run(f);
  check(f, "TEST    TXT", "Xkeep\r\n");
  assert(strstr(f->output, "File too long"));
  done(f);
  puts("PASS Z80 COM: oversized include rejected before changing buffer");
  f = CASE("dd:wq\r", "placeholder");
  file* large = lookup(f, "TEST    TXT", 0);
  large->size = 7999;
  memset(large->data, 'a', 7998);
  large->data[7998] = 10;
  run(f);
  large = lookup(f, "TEST    TXT", 0);
  assert(large->size >= 8000);
  for (unsigned i = 0; i < 7998; i++)
    assert(large->data[i] == 'a');
  assert(strstr(f->output, "No memory for line buffer"));
  done(f);
  puts("PASS Z80 COM: clipboard exhaustion refuses deletion safely");
  f = CASE("iX\033:wq\r:q!\r", "original\r\n");
  f->failrename = 1;
  run(f);
  check(f, "TEST    TXT", "original\r\n");
  assert(strstr(f->output, "Save failed"));
  done(f);
  puts("PASS Z80 COM: rename failure preserves original and keeps editor open");
  return 0;
}
