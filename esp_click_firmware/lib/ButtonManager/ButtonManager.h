#pragma once
#include <Arduino.h>
#include "Button.h"
#include "SleepManager.h"

struct ButtonEvent
{
    int id;
    PressEvent event;
};

class ButtonManager
{
private:
    int buttonCount = 0;
    std::vector<Button *> buttons;
    QueueHandle_t buttonEventQueue;

    EventBits_t taskId;

    static void buttonTask(void *pvParameters)
    {
        ButtonManager *instance = (ButtonManager *)pvParameters;
        instance->run();
    }

    void run()
    {
        while (true)
        {
            for (Button *button : buttons)
            {
                button->update();
                if (button->hasEvent())
                {
                    ButtonEvent event = {.id = button->getPin(), .event = button->getEvent()};
                    xQueueSend(buttonEventQueue, &event, 0);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

public:
    void begin()
    {
        buttonEventQueue = xQueueCreate(10, sizeof(ButtonEvent));
        taskId = SleepManager::getInstance().registerTask();
        xTaskCreate(ButtonManager::buttonTask, "Button Manager", 2048, this, 1, NULL);
    }

    void registerButton(Button *button)
    {
        buttons.push_back(button);

        button->registerStateChangeCallback(
            [this](ButtonState state)
            {
                if (state != IDLE)
                {
                    SleepManager::getInstance().keepAwake(this->taskId);
                }
                else
                {
                    SleepManager::getInstance().allowSleep(this->taskId);
                }
            });
    }

    QueueHandle_t getQueue()
    {
        return buttonEventQueue;
    }
};