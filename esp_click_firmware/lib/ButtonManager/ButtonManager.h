#pragma once
#include <Arduino.h>
#include <vector>
#include "Button.h"
#include "SleepManager.h"
#include "BaseController.h"
#include "PressEvent.h"
#include "Message.h"

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
    std::vector<BaseController *> messageSinks;

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
                    Message message;
                    message.type = MessageType::BUTTON_PRESS;
                    message.data.buttonPress.buttonId = button->getPin();
                    message.data.buttonPress.event = button->getEvent();
                    for (BaseController *sink : messageSinks)
                    {
                        sink->addMessage(message);
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

public:
    void begin()
    {
        taskId = SleepManager::getInstance().registerTask();
        xTaskCreate(ButtonManager::buttonTask, "Button Manager", 2048, this, 1, NULL);
    }

    void registerButton(Button *button, bool registerWakeupPin = true)
    {
        buttons.push_back(button);

        if (registerWakeupPin)
        {
            SleepManager::getInstance().registerWakeupPin(button->getPin());
        }

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

    void registerMessageSink(BaseController *controller)
    {
        messageSinks.push_back(controller);
    }

};