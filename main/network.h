#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t networkStart(void);
bool networkReady(char *address, size_t capacity);
const char *networkStatus(void);
