#pragma once
#include <Arduino.h>

class SleepManager
{
private:
    EventGroupHandle_t sleepEventGroup;

    int eventCount = 0;

    SleepManager() {}

public:
    static SleepManager &getInstance()
    {
        static SleepManager instance;
        return instance;
    }

    SleepManager(const SleepManager &) = delete;
    SleepManager &operator=(const SleepManager &) = delete;

    void begin()
    {
        sleepEventGroup = xEventGroupCreate();

        if (sleepEventGroup == NULL)
        {
            Serial.println("Failed to create sleep event group!");
        }
    }

    EventBits_t registerTask()
    {
        if (eventCount >= 24)
        {
            Serial.println("Error: Maximum of 24 tasks are registered!");
            return 0;
        }

        EventBits_t assignedBit = (1 << eventCount);

        eventCount++;

        return assignedBit;
    }

    void keepAwake(EventBits_t taskId)
    {
        xEventGroupSetBits(sleepEventGroup, taskId);
    }

    void allowSleep(EventBits_t taskId)
    {
        xEventGroupClearBits(sleepEventGroup, taskId);
    }
};