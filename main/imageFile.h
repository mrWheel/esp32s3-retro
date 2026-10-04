#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct
{
  FILE *file;
  uint64_t size;
  bool readOnly;
} imageFile;

bool imageOpen(imageFile *image, const char *path, bool readOnly);
uint64_t imageSize(const imageFile *image);
bool imageReadAt(imageFile *image, uint64_t offset, void *buffer, size_t length);
bool imageWriteAt(imageFile *image, uint64_t offset, const void *buffer, size_t length);
bool imageFlush(imageFile *image);
bool imageClose(imageFile *image);
bool resourceSize(const char *path, uint64_t *size);
