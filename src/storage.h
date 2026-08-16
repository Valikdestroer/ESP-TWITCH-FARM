#ifndef STORAGE_H
#define STORAGE_H

#include "config.h"

class StorageManager {
public:
    static bool init();
    static bool loadConfig(AppConfig& config);
    static bool saveConfig(const AppConfig& config);
    static void resetConfig();
};

#endif // STORAGE_H
