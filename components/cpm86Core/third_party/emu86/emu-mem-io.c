
#include "emu-mem-io.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


// Memory status

byte_t *mem_stat;
size_t mem_size;
byte_t mem_fault;
static byte_t unmapped_byte = 0xFF;

// TODO: implement data status
// byte_t data_stat [MEM_MAX];

byte_t _break_data_flag = 0;
addr_t _break_data_addr = 0x100000;


// Memory access

void * mem_get_addr (addr_t a)
	{
	a &= (MEM_MAX - 1);
	if (a >= mem_size)
		{
		mem_fault = 1;
		return &unmapped_byte;
		}

	// Data breakpoint test
	// Will break the main execution loop later

	if (a == _break_data_addr)
		_break_data_flag = 1;

	return (mem_stat + a);
	}

void mem_set_size (size_t size)
	{
	mem_size = size;
	}


byte_t mem_read_byte_0 (addr_t a)
	{
	byte_t * p = (byte_t *) mem_get_addr (a);
	return *p;
	}

word_t mem_read_word_0 (addr_t a)
	{
	byte_t * p = (byte_t *) mem_get_addr (a);
	byte_t high = *(byte_t *) mem_get_addr (a + 1);
	return (word_t) p[0] | ((word_t) high << 8);
	}


int mem_write_byte_0 (addr_t a, byte_t b, byte_t init)
	{
	(void) init;
	a &= (MEM_MAX - 1);
	if (a >= mem_size)
		{
		mem_fault = 1;
		return -1;
		}
	byte_t * p = (byte_t *) mem_get_addr (a);
	*p = b;

	return 0;
	}

int mem_write_word_0 (addr_t a, word_t w, byte_t init)
	{
	(void) init;
	a &= (MEM_MAX - 1);
	addr_t next = (a + 1) & (MEM_MAX - 1);
	if (a >= mem_size || next >= mem_size)
		{
		mem_fault = 1;
		return -1;
		}
	*(byte_t *) mem_get_addr (a) = (byte_t) w;
	*(byte_t *) mem_get_addr (next) = (byte_t) (w >> 8);
	return 0;
	}

//-------------------------------------------------------------------------------

// Memory reset

void mem_io_reset ()
	{
	// No or uninitialized memory: all bits to 1
	// Used to check if interrupt vector is initialized in op_int()

	memset (mem_stat, 0xFF, mem_size);
	mem_fault = 0;
	}

//-------------------------------------------------------------------------------

int io_read_byte_0 (word_t p, byte_t * b)
	{
	(void) b;
	int err = 0;

	switch (p)
		{
		default:
			printf ("\nerror: I/O read byte from unmapped %hXh", p);
			err = -1;
		}

	return err;
	}

int io_write_byte_0 (word_t p, byte_t b)
	{
	int err = 0;

	switch (p)
		{
		default:
			printf ("\nerror: I/O write byte %hhXh to unmapped %hXh", b, p);
			err = -1;
		}

	return err;
	}

int io_read_word_0 (word_t p, word_t * w)
	{
	(void) w;
	int err;

	if (p & 0x0001) {
		printf ("\nerror: I/O read word unaligned %hXh", p);
		err = -1;
		}
	else {
		printf ("\nerror: I/O read word from unmapped %hXh", p);
		err = -1;
		}

	return err;
	}

int io_write_word_0 (word_t p, word_t w)
	{
	int err;

	if (p & 0x0001) {
		printf ("\nerror: I/O write word %hXh unaligned %hXh", w, p);
		err = -1;
		}
	else {
		// no port
		printf ("\nerror: I/O write word %hXh to unmapped %hXh", w, p);
		err = -1;
		}

	return err;
	}

//-------------------------------------------------------------------------------
