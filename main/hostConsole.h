#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t hostConsoleInit(void);
int hostConsoleGetChar(void);
bool hostConsoleCharAvailable(void);
void hostConsolePutChar(char value);
void hostConsoleWrite(const char *text);
