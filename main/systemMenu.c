#include "systemMenu.h"
#include "hostConsole.h"
#include "hostCore.h"
#include "machineRegistry.h"
#include "storage.h"
#include "network.h"
#include "fileTransfer.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static menuLine inputLine;

typedef enum
{
  mainMenu,
  returnPrompt,
  transferMode
} menuState;

static void showMenu(void)
{
  puts("\nESP32-S3 Retro Computer\n=======================");
  printf("SD: %s\n\n", storageStatus());
  for (size_t index = 0; index < machineCount(); ++index)
  {
    const retroMachine *machine = machineGet(index);
    printf("%u. %s [%s]\n", (unsigned)index + 1, machine->name, machineStateText(machine->probe(machine)));
  }
  printf("6. File Transfer%s\n\n", storageReady() ? "" : " [SD unavailable]");
  printf("Select system [1-6]: ");
  fflush(stdout);
}

static void showReturnPrompt(void)
{
  puts("Press ENTER to return to the main menu.");
}

static menuState selectMachine(unsigned choice)
{
  const retroMachine *machine = machineGet(choice - 1);
  if (machine == NULL)
  {
    return mainMenu;
  }
  printf("\n%s\n========\n", machine->name);
  machineState state = machine->probe(machine);
  if (state == machineNotImplemented)
  {
    machine->run();
  }
  else if (state != machineAvailable)
  {
    printf("Unavailable: %s\n", machineStateText(state));
  }
  else
  {
    esp_err_t result = machine->init();
    if (result == ESP_OK)
    {
      machine->run();
    }
    else
    {
      printf("Initialization failed: %s\n", esp_err_to_name(result));
    }
  }
  showReturnPrompt();
  return returnPrompt;
}

void systemMenuRun(void)
{
  menuState state = mainMenu;
  int64_t transferStarted = 0;
  bool serverFailed = false;
  bool timeoutShown = false;
  char previousAddress[24] = "";
  showMenu();
  while (true)
  {
    if (state == transferMode)
    {
      char address[24];
      bool connected = networkReady(address, sizeof(address));
      if (!connected && fileTransferActive())
      {
        fileTransferStop();
        previousAddress[0] = '\0';
        puts("WiFi connection lost; File Transfer stopped. Press ENTER, then reset to reconnect if needed.");
      }
      if (connected && !fileTransferActive() && !serverFailed)
      {
        esp_err_t result = fileTransferStart();
        if (result != ESP_OK)
        {
          printf("Cannot start File Transfer: %s\n", esp_err_to_name(result));
          serverFailed = true;
        }
      }
      if (connected && fileTransferActive() && strcmp(address, previousAddress) != 0)
      {
        snprintf(previousAddress, sizeof(previousAddress), "%s", address);
        printf("\nWiFi connected: %s\nOpen: http://%s/\n", address, address);
        puts("Anyone on this WiFi network can access the exchange files while File Transfer is active.");
        puts("Press ENTER to stop File Transfer and return to the main menu.");
      }
      if (!connected && !timeoutShown && esp_timer_get_time() - transferStarted > 120000000)
      {
        printf("\n%s\n", networkStatus());
        puts("No connection after 120 seconds. Continue setup or press ENTER to return.");
        timeoutShown = true;
      }
    }
    int character = hostConsoleGetChar();
    if (character < 0)
    {
      continue;
    }
    bool valid;
    if (!menuFeed(&inputLine, character, &valid))
    {
      if (character >= 32 && character <= 126)
      {
        hostConsolePutChar((char)character);
      }
      else if (character == 8 || character == 127)
      {
        hostConsoleWrite("\b \b");
      }
      continue;
    }
    hostConsolePutChar('\n');
    if (state == transferMode)
    {
      puts("Stopping File Transfer and closing active files...");
      fileTransferStop();
      state = mainMenu;
      storageRefresh();
      showMenu();
      continue;
    }
    if (state == returnPrompt)
    {
      state = mainMenu;
      showMenu();
      continue;
    }
    if (!valid || strlen(inputLine.text) != 1 || inputLine.text[0] < '1' || inputLine.text[0] > '6')
    {
      puts("Invalid selection. Enter one digit from 1 to 6, followed by ENTER.");
      showMenu();
      continue;
    }
    unsigned choice = (unsigned)(inputLine.text[0] - '0');
    if (choice < 6)
    {
      state = selectMachine(choice);
      continue;
    }
    if (!storageRefresh())
    {
      printf("File Transfer is unavailable.\n%s\n", storageStatus());
      showReturnPrompt();
      state = returnPrompt;
      continue;
    }
    puts("\nRetro File Transfer\n===================\nSD card: OK");
    esp_err_t result = networkStart();
    if (result != ESP_OK)
    {
      printf("WiFi could not start: %s\n", esp_err_to_name(result));
      showReturnPrompt();
      state = returnPrompt;
      continue;
    }
    puts("Connecting or provisioning WiFi. For setup join Retro-Setup, then open http://192.168.4.1/");
    puts("Press ENTER at any time to stop File Transfer and return to the main menu.");
    state = transferMode;
    transferStarted = esp_timer_get_time();
    timeoutShown = false;
    serverFailed = false;
    previousAddress[0] = '\0';
  }
}
