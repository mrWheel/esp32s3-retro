#pragma once
#include <stdbool.h>

bool storageBootMount(void);
bool storageRefresh(void);
bool storageReady(void);
const char *storageStatus(void);
bool storageDirectoryExists(const char *path);
