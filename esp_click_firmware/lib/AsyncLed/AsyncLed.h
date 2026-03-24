#pragma once
#include <Arduino.h>
#include <functional> // <-- ADDED for std::function
#include "SleepManager.h"

enum Color
{
    RED,
    GREEN,
    BLUE,
    YELLOW,
    CYAN,
    MAGENTA,
    WHITE,
    BLACK
};

enum LedMode
{
    OFF,
    BLINK,
    PULSE,
    SOLID
};

enum LedSpeed
{
    GLACIAL = 2000, // 2 seconds (Great for slow, sleepy breathing pulses)
    SLOW = 1000,    // 1 second (Standard relaxed blink)
    MEDIUM = 500,   // 0.5 seconds (Standard alert)
    FAST = 250,     // 0.25 seconds (Urgent alert)
    RAPID = 100,    // 0.1 seconds (Very urgent, fast flickering)
    STROBE = 50     // 0.05 seconds (Aggressive strobe/flash effect)
};

struct LedCommand
{
    LedMode mode;
    Color color;
    LedSpeed speed = MEDIUM;
    int count = -1;
};

enum LedHardware
{
    COMMON_CATHODE,
    COMMON_ANODE
};

enum LedType
{
    SINGLE,
    RGB
};

class AsyncLed
{
private:
    int rPin;
    int gPin;
    int bPin;
    int pin;
    bool fadeBetweenCommands = true;
    LedCommand pendingCmd;
    LedHardware hardwareType;
    LedType ledType;
    QueueHandle_t commandQueue;
    EventBits_t taskId;

    // --- UPDATED: Callback variables ---
    std::function<void()> animStartCallback = nullptr;
    std::function<void(LedCommand)> animEndCallback = nullptr; // Now takes LedCommand

    static void ledTask(void *pvParameters)
    {
        AsyncLed *ledInstance = (AsyncLed *)pvParameters;
        ledInstance->run();
    }

    void getColorValues(Color c, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        switch (c)
        {
        case RED:
            r = 255;
            g = 0;
            b = 0;
            break;
        case GREEN:
            r = 0;
            g = 255;
            b = 0;
            break;
        case BLUE:
            r = 0;
            g = 0;
            b = 255;
            break;
        case YELLOW:
            r = 255;
            g = 255;
            b = 0;
            break;
        case CYAN:
            r = 0;
            g = 255;
            b = 255;
            break;
        case MAGENTA:
            r = 255;
            g = 0;
            b = 255;
            break;
        case WHITE:
            r = 255;
            g = 255;
            b = 255;
            break;
        case BLACK:
            r = 0;
            g = 0;
            b = 0;
            break;
        default:
            r = 0;
            g = 0;
            b = 0;
            break;
        }
    }

    void setHardwareColor(uint8_t r, uint8_t g, uint8_t b)
    {
        if (ledType == RGB)
        {
            uint8_t outR = (hardwareType == COMMON_ANODE) ? (255 - r) : r;
            uint8_t outG = (hardwareType == COMMON_ANODE) ? (255 - g) : g;
            uint8_t outB = (hardwareType == COMMON_ANODE) ? (255 - b) : b;

            analogWrite(rPin, outR);
            analogWrite(gPin, outG);
            analogWrite(bPin, outB);
        }
        else
        {
            uint8_t intensity = max({r, g, b});
            uint8_t outPin = (hardwareType == COMMON_ANODE) ? (255 - intensity) : intensity;

            analogWrite(pin, outPin);
        }
    }

    void run()
    {
        LedCommand currentCmd = {OFF, BLACK, MEDIUM, -1};
        int currentCount = -1;

        // Animation state variables
        bool blinkState = false;
        float pulseLevel = 0.0f;
        bool pulseIncreasing = true;
        unsigned long lastActionTime = 0;

        // Transition state variables
        bool isFadingOut = false;
        float fadeOutLevel = 1.0f;
        bool isFadingIn = false;
        float fadeInLevel = 0.0f;

        // State trackers for callbacks
        bool wasActive = false;
        LedCommand lastActiveCmd = {OFF, BLACK, MEDIUM, -1}; // Tracks the last non-OFF state

        while (true)
        {
            LedCommand tempCmd;
            bool gotNewCmd = false;

            if (isFadingOut || isFadingIn)
            {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            else
            {
                TickType_t waitTime = (currentCmd.mode == OFF || currentCmd.mode == SOLID) ? portMAX_DELAY : pdMS_TO_TICKS(20);

                if (xQueueReceive(commandQueue, &tempCmd, waitTime) == pdTRUE)
                {
                    gotNewCmd = true;
                }
            }

            if (gotNewCmd)
            {
                float currentBrightness = 0.0f;
                if (currentCmd.mode == SOLID)
                    currentBrightness = 1.0f;
                else if (currentCmd.mode == BLINK)
                    currentBrightness = blinkState ? 1.0f : 0.0f;
                else if (currentCmd.mode == PULSE)
                    currentBrightness = pulseLevel;

                if (fadeBetweenCommands && currentBrightness > 0.01f)
                {
                    pendingCmd = tempCmd;
                    isFadingOut = true;
                    fadeOutLevel = currentBrightness;
                }
                else
                {
                    currentCmd = tempCmd;
                    currentCount = currentCmd.count;
                    blinkState = false;
                    pulseLevel = 0.0f;
                    pulseIncreasing = true;

                    if (currentCmd.mode == SOLID)
                    {
                        isFadingIn = true;
                        fadeInLevel = 0.0f;
                    }
                    else if (currentCmd.mode == OFF)
                    {
                        setHardwareColor(0, 0, 0);
                    }
                }
            }

            uint8_t targetR, targetG, targetB;
            getColorValues(currentCmd.color, targetR, targetG, targetB);
            unsigned long currentMillis = millis();

            if (isFadingOut)
            {
                float step = 20.0f / (float)currentCmd.speed;
                fadeOutLevel -= step;

                if (fadeOutLevel <= 0.0f)
                {
                    isFadingOut = false;
                    currentCmd = pendingCmd;
                    currentCount = currentCmd.count;
                    blinkState = false;
                    pulseLevel = 0.0f;
                    pulseIncreasing = true;

                    if (currentCmd.mode == SOLID)
                    {
                        isFadingIn = true;
                        fadeInLevel = 0.0f;
                    }
                    else
                    {
                        setHardwareColor(0, 0, 0);
                    }
                }
                else
                {
                    setHardwareColor(targetR * fadeOutLevel, targetG * fadeOutLevel, targetB * fadeOutLevel);
                }
            }
            else if (isFadingIn)
            {
                float step = 20.0f / (float)currentCmd.speed;
                fadeInLevel += step;

                if (fadeInLevel >= 1.0f)
                {
                    fadeInLevel = 1.0f;
                    isFadingIn = false;
                }
                setHardwareColor(targetR * fadeInLevel, targetG * fadeInLevel, targetB * fadeInLevel);
            }
            else if (currentCmd.mode == SOLID)
            {
                setHardwareColor(targetR, targetG, targetB);
            }
            else if (currentCmd.mode == BLINK)
            {
                if (currentMillis - lastActionTime >= currentCmd.speed)
                {
                    lastActionTime = currentMillis;
                    blinkState = !blinkState;
                    setHardwareColor(blinkState ? targetR : 0, blinkState ? targetG : 0, blinkState ? targetB : 0);

                    if (!blinkState && currentCount > 0)
                    {
                        currentCount--;
                        if (currentCount == 0)
                        {
                            currentCmd.mode = OFF;
                        }
                    }
                }
            }
            else if (currentCmd.mode == PULSE)
            {
                float step = 20.0f / (float)currentCmd.speed;

                if (pulseLevel <= 0.0f && !pulseIncreasing)
                {
                    if (currentCount > 0)
                    {
                        currentCount--;
                        if (currentCount == 0)
                        {
                            currentCmd.mode = OFF;
                        }
                    }

                    if (currentCmd.mode != OFF)
                    {
                        pulseIncreasing = true;
                    }
                }

                if (currentCmd.mode == PULSE)
                {
                    if (pulseIncreasing)
                    {
                        pulseLevel += step;
                        if (pulseLevel >= 1.0f)
                        {
                            pulseLevel = 1.0f;
                            pulseIncreasing = false;
                        }
                    }
                    else
                    {
                        pulseLevel -= step;
                        if (pulseLevel <= 0.0f)
                        {
                            pulseLevel = 0.0f;
                        }
                    }
                    setHardwareColor(targetR * pulseLevel, targetG * pulseLevel, targetB * pulseLevel);
                }
            }

            // --- Existing Sleep Check ---
            if (currentCmd.mode == OFF && !isFadingOut && !isFadingIn)
            {
                SleepManager::getInstance().allowSleep(this->taskId);
            }

            // --- UPDATED: Callback Trigger Logic ---
            bool isCurrentlyActive = !(currentCmd.mode == OFF && !isFadingOut && !isFadingIn);

            if (isCurrentlyActive && !wasActive)
            {
                if (animStartCallback)
                    animStartCallback();
            }
            else if (!isCurrentlyActive && wasActive)
            {
                if (animEndCallback)
                    animEndCallback(lastActiveCmd); // Pass the recorded command!
            }

            wasActive = isCurrentlyActive;
            lastActiveCmd = currentCmd;
        }
    }

public:
    AsyncLed(int rPin, int gPin, int bPin, LedHardware hardwareType) : rPin(rPin), gPin(gPin), bPin(bPin), hardwareType(hardwareType), ledType(RGB) {}
    AsyncLed(int pin, LedHardware hardwareType) : pin(pin), hardwareType(hardwareType), ledType(SINGLE) {}

    void begin()
    {
        this->taskId = SleepManager::getInstance().registerTask();
        commandQueue = xQueueCreate(10, sizeof(LedCommand));
        xTaskCreate(AsyncLed::ledTask, "LED Task", 2048, this, 1, NULL);
    }

    void registerOnAnimStart(std::function<void()> callback)
    {
        animStartCallback = callback;
    }

    // --- UPDATED: Callback Registration ---
    void registerOnAnimEnd(std::function<void(LedCommand)> callback)
    {
        animEndCallback = callback;
    }

    void set(LedMode mode, Color color, LedSpeed speed = MEDIUM)
    {
        SleepManager::getInstance().keepAwake(this->taskId);
        LedCommand cmd = {mode, color, speed};
        xQueueSend(commandQueue, &cmd, 0);
    }

    void set(LedMode mode, LedSpeed speed)
    {
        SleepManager::getInstance().keepAwake(this->taskId);
        LedCommand cmd = {mode, WHITE, speed};
        xQueueSend(commandQueue, &cmd, 0);
    }

    void set(LedMode mode)
    {
        SleepManager::getInstance().keepAwake(this->taskId);
        LedCommand cmd = {mode, WHITE, MEDIUM};
        xQueueSend(commandQueue, &cmd, 0);
    }

    void set(LedMode mode, int count, Color color, LedSpeed speed = MEDIUM)
    {
        SleepManager::getInstance().keepAwake(this->taskId);
        LedCommand cmd = {mode, color, speed, count};
        xQueueSend(commandQueue, &cmd, 0);
    }
};