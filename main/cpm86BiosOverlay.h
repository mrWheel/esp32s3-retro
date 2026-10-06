#pragma once

#include <stdbool.h>

typedef struct cpm86Core cpm86Core;

bool cpm86BiosLoadOverlay(cpm86Core *core, const char *path);
