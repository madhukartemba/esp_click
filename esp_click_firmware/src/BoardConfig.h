#pragma once
#include <Arduino.h>

namespace BoardConfig
{
    // LED Configuration
    constexpr uint8_t LED_PIN_R = 21;
    constexpr uint8_t LED_PIN_G = 22;
    constexpr uint8_t LED_PIN_B = 23;

    // Battery Monitor Configuration
    constexpr uint8_t BATTERY_PIN = 6;
    constexpr uint8_t POWER_GOOD_PIN = 4;
    constexpr uint8_t CHARGE_DETECT_PIN = 5;
    constexpr float VOLTAGE_DIVIDER_RATIO = 910.0f / 1380.0f;
    constexpr int LOW_BATTERY_THRESHOLD = 80;

    // Button Configuration
    constexpr uint8_t BTN1_PIN = 0;
    constexpr uint8_t BTN2_PIN = 1;
    constexpr uint8_t BTN3_PIN = 2;
    constexpr uint8_t BTN4_PIN = 3;

}