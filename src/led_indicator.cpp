#include "led_indicator.h"

static LedState currentState = LED_STATE_OFF;
static bool ledEnabled = true;
static uint32_t claimFlashStart = 0;
static uint32_t lastUpdate = 0;
static uint16_t breathStep = 0;
static bool breathIncreasing = true;

// Maximum brightness capped at 10% (25 / 255)
#define MAX_BRIGHTNESS 25

void LedIndicator::init() {
    pinMode(RGB_LED_PIN, OUTPUT);
    setRgb(0, 0, 0);
    currentState = LED_STATE_OFF;
}

void LedIndicator::setEnabled(bool enabled) {
    ledEnabled = enabled;
    if (!enabled) {
        setRgb(0, 0, 0);
    }
}

bool LedIndicator::isEnabled() {
    return ledEnabled;
}

void LedIndicator::setState(LedState state) {
    if (currentState != state) {
        currentState = state;
        breathStep = 0;
        breathIncreasing = true;
        update();
    }
}

void LedIndicator::flashClaim() {
    claimFlashStart = millis();
    if (claimFlashStart == 0) claimFlashStart = 1;
    // Purple flash (Red + Blue @ 10%)
    setRgb(MAX_BRIGHTNESS, 0, MAX_BRIGHTNESS);
}

void LedIndicator::setRgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!ledEnabled) {
        neopixelWrite(RGB_LED_PIN, 0, 0, 0);
        return;
    }
    // Strict 10% cap
    if (r > MAX_BRIGHTNESS) r = MAX_BRIGHTNESS;
    if (g > MAX_BRIGHTNESS) g = MAX_BRIGHTNESS;
    if (b > MAX_BRIGHTNESS) b = MAX_BRIGHTNESS;
    neopixelWrite(RGB_LED_PIN, r, g, b);
}

void LedIndicator::update() {
    uint32_t now = millis();
    if (now - lastUpdate < 30) return; // 33 FPS animation refresh
    lastUpdate = now;

    // 1. Handle temporary Claim Flash (Purple for 600ms)
    if (claimFlashStart > 0) {
        if (now - claimFlashStart < 600) {
            setRgb(MAX_BRIGHTNESS, 0, MAX_BRIGHTNESS);
            return;
        } else {
            claimFlashStart = 0;
        }
    }

    if (!ledEnabled) {
        setRgb(0, 0, 0);
        return;
    }

    // 2. State-specific animation patterns
    switch (currentState) {
        case LED_STATE_OFF:
            setRgb(0, 0, 0);
            break;

        case LED_STATE_WIFI_CONNECTING: {
            // Blue slow pulse (0 to 20)
            if (breathIncreasing) {
                breathStep++;
                if (breathStep >= 20) breathIncreasing = false;
            } else {
                if (breathStep > 0) breathStep--;
                else breathIncreasing = true;
            }
            setRgb(0, 0, (uint8_t)breathStep);
            break;
        }

        case LED_STATE_AUTH_SEARCH: {
            // Yellow solid (Red + Green @ 10%)
            setRgb(MAX_BRIGHTNESS, MAX_BRIGHTNESS / 2, 0);
            break;
        }

        case LED_STATE_FARMING: {
            // Green soft breath (3 to 22)
            if (breathIncreasing) {
                breathStep++;
                if (breathStep >= 22) breathIncreasing = false;
            } else {
                if (breathStep > 3) breathStep--;
                else breathIncreasing = true;
            }
            setRgb(0, (uint8_t)breathStep, 0);
            break;
        }

        case LED_STATE_ERROR: {
            // Red solid @ 10%
            setRgb(MAX_BRIGHTNESS, 0, 0);
            break;
        }

        case LED_STATE_CLAIM: {
            // Purple
            setRgb(MAX_BRIGHTNESS, 0, MAX_BRIGHTNESS);
            break;
        }
    }
}
