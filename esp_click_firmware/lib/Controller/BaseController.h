#pragma once

#include <Arduino.h>
#include "Message.h"
#include "SleepManager.h"

class BaseController
{
protected:
    QueueHandle_t messageQueue = nullptr;
    EventBits_t taskId = 0;

    BaseController() = default;

    static void controllerTask(void *pvParameters)
    {
        static_cast<BaseController *>(pvParameters)->run();
    }

    virtual void run() = 0;

    void startControllerTask(const char *taskName, uint32_t stackWords = 4096,
                             UBaseType_t priority = 1, uint32_t queueLength = 10)
    {
        messageQueue = xQueueCreate(queueLength, sizeof(Message));
        taskId = SleepManager::getInstance().registerTask();
        xTaskCreate(controllerTask, taskName, stackWords, this, priority, nullptr);
    }

public:
    virtual ~BaseController() = default;

    void addMessage(Message message)
    {
        if (messageQueue != nullptr)
        {
            SleepManager::getInstance().keepAwake(taskId);
            xQueueSend(messageQueue, &message, 0);
        }
    }
};
