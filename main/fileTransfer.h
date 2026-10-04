#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t fileTransferStart(void);
void fileTransferStop(void);
bool fileTransferActive(void);
const char *fileTransferToken(void);
