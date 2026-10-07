#pragma once

#include "esp_err.h"

esp_err_t diskActivityInit(void);
void diskActivityRead(void);
void diskActivityWrite(void);
