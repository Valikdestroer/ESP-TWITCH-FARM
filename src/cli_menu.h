#ifndef CLI_MENU_H
#define CLI_MENU_H

#include "config.h"

class CLIMenu {
public:
    static void init();
    static void processInput();
    static void printBanner();
    static void printStatus();

private:
    static void handleCommand(String cmd);
};

#endif // CLI_MENU_H
