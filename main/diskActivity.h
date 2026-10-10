#pragma once

#include "esp_err.h"

//-- Temporary diagnostics: set to 0 to silence the RGB LED request/transmit logging.
#define DISK_ACTIVITY_DEBUG_LOG 0

esp_err_t diskActivityInit(void);
void diskActivityRead(void);
void diskActivityWrite(void);
