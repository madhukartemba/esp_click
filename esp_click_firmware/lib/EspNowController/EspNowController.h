#pragma once
#include <Arduino.h>
#include "ButtonManager.h"
#include "BatteryMonitor.h"
#include "SleepManager.h"

enum MessageType
{
    BUTTON_PRESS,
    BATTERY_STATUS,
};

struct Message
{
    int deviceId = 0;
    int entityId;
    MessageType type;
    union
    {
        struct
        {
            PressEvent event;
        } buttonPress;

        struct
        {
            int level;
            BatteryStatus status;
        } batteryLevel;
    } data;
};

class EspNowController
{
private:
    QueueHandle_t messageQueue;
    EventBits_t taskId;

    std::function<void()> onBeforeSend = nullptr;
    std::function<void(bool)> onAfterSend = nullptr;

    EspNowController() {}
    ~EspNowController() {}

    static void controllerTask(void *pvParameters)
    {
        EspNowController *instance = (EspNowController *)pvParameters;
        instance->run();
    }

    void run()
    {
        while (true)
        {

            Message message;
            if (xQueueReceive(messageQueue, &message, portMAX_DELAY))
            {

                if (onBeforeSend)
                {
                    onBeforeSend();
                }

                // Logic to send ESP-NOW message would go here
                bool success = true; // Placeholder for your ESP-NOW sending logic

                if (onAfterSend)
                {
                    onAfterSend(success);
                }
            }
        }
    }

public:
    static EspNowController &getInstance()
    {
        static EspNowController instance;
        return instance;
    }

    EspNowController(const EspNowController &) = delete;
    EspNowController &operator=(const EspNowController &) = delete;

    void begin(QueueHandle_t messageQueue)
    {
        this->messageQueue = messageQueue;
        this->taskId = SleepManager::getInstance().registerTask();

        xTaskCreate(EspNowController::controllerTask, "ESP-NOW Task", 2048, this, 1, NULL);
    }

    void addMessage()
    {
    }

    void registerOnBeforeSend(std::function<void()> callback)
    {
        this->onBeforeSend = callback;
    }

    void registerOnAfterSend(std::function<void(bool)> callback)
    {
        this->onAfterSend = callback;
    }
};