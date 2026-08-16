#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "config.h"
#include <WebServer.h>

class WebServerManager {
public:
    static void init();
    static void handleClient();

private:
    static WebServer server;
    static void handleRoot();
    static void handleApiStatus();
    static void handleApiConfig();
    static void handleApiLogs();
    static void handleApiWifiScan();
    static void handleApiRestart();
    static void handleApiGames();
    static void handleApiGamesAdd();
    static void handleApiGamesRemove();
    static void handleApiGamesReorder();
    static void handleApiGamesClear();
    static void handleApiChannelClear();
    static void handleNotFound();
};

#endif // WEB_SERVER_H
