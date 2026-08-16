#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <Arduino.h>

// Default WS2812 RGB LED pin on ESP32-S3 DevKitC-1
#ifndef RGB_LED_PIN
#ifdef RGB_BUILTIN
#define RGB_LED_PIN RGB_BUILTIN
#else
#define RGB_LED_PIN 48
#endif
#endif

enum LedState {
    LED_STATE_OFF,
    LED_STATE_WIFI_CONNECTING,  // Blue slow pulse
    LED_STATE_AUTH_SEARCH,      // Yellow solid
    LED_STATE_FARMING,          // Green soft breath
    LED_STATE_CLAIM,            // Purple quick flash
    LED_STATE_ERROR             // Red solid
};

class LedIndicator {
public:
    static void init();
    static void setState(LedState state);
    static void flashClaim();
    static void update();
    static void setEnabled(bool enabled);
    static bool isEnabled();

private:
    static void setRgb(uint8_t r, uint8_t g, uint8_t b);
};

#endif // LED_INDICATOR_H
