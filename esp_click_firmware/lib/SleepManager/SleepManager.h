#pragma once
#include <Arduino.h>

class SleepManager
{
private:
    EventGroupHandle_t sleepEventGroup;

    EventBits_t registeredTasksMask = 0;

    int eventCount = 0;
    unsigned long sleepTimeout = 10000;

    unsigned long lastActivityTime = 0;

    SleepManager() {}

    static void sleepTask(void *pvParameters)
    {
        SleepManager *instance = (SleepManager *)pvParameters;
        instance->run();
    }

    void run()
    {
        while (true)
        {
            if (eventCount > 0)
            {
                xEventGroupWaitBits(sleepEventGroup, registeredTasksMask, pdFALSE, pdTRUE, portMAX_DELAY);
            }

            unsigned long idleTime = millis() - lastActivityTime;

            if (idleTime >= sleepTimeout)
            {
                // Simulate sleep
                Serial.println("Going into deep sleep");
                vTaskDelay(pdMS_TO_TICKS(1000000));
            }
            else
            {
                unsigned long waitTime = sleepTimeout - idleTime;
                vTaskDelay(pdMS_TO_TICKS(waitTime));
            }
        }
    }

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

        xTaskCreate(SleepManager::sleepTask, "Sleep Manager", 2048, this, 1, NULL);
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

        registeredTasksMask |= assignedBit;

        xEventGroupSetBits(sleepEventGroup, assignedBit);

        return assignedBit;
    }

    void keepAwake(EventBits_t taskId)
    {
        xEventGroupClearBits(sleepEventGroup, taskId);
    }

    void allowSleep(EventBits_t taskId)
    {
        xEventGroupSetBits(sleepEventGroup, taskId);
        lastActivityTime = millis();
    }

    void reportActivity()
    {
        lastActivityTime = millis();
    }

    void setSleepTimeout(unsigned long timeoutMs)
    {
        sleepTimeout = timeoutMs;
    }
};