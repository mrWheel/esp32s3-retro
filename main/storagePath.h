#pragma once

#include <stdbool.h>
#include <stddef.h>

bool storageResolveRetroPath(const char *retroPath, char *resolvedPath, size_t pathCapacity);
