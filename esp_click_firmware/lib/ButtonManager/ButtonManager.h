#pragma once
#include <Arduino.h>
#include "Button.h"

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
        }
    }

public:
    void begin()
    {
        buttonEventQueue = xQueueCreate(10, sizeof(ButtonEvent));
        xTaskCreate(ButtonManager::buttonTask, "Button Manager", 2048, this, 1, NULL);
    }

    void registerButton(Button *button)
    {
        buttons.push_back(button);
    }

    QueueHandle_t getQueue()
    {
        return buttonEventQueue;
    }
};