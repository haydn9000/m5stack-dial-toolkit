/**
 * @file main.cpp
 * @brief M5Stack Dial User Demo - ported from ESP-IDF to Arduino/PlatformIO
 *        Original: https://github.com/m5stack/M5Dial-UserDemo
 */
#include <Arduino.h>
#include "hal/hal.h"
#include "apps/app.h"
#include "apps/launcher/launcher.h"

static HAL::HAL hal;

void setup()
{
    /* Hardware init */
    hal.init();
}

void loop()
{
    /* Start launcher - runs indefinitely */
    MOONCAKE::USER_APP::Launcher app_launcher;
    app_launcher.setUserData((void*)&hal);
    app_launcher.onSetup();
    app_launcher.onCreate();

    while (true)
    {
        app_launcher.onRunning();
    }
}