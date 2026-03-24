#pragma once
#include <Arduino.h>
#include "Button.h"
#include "SleepManager.h"
#include "EspNowController.h"
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
                    EspNowController::getInstance().addMessage(message);
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

    void registerButton(Button *button)
    {
        buttons.push_back(button);

        SleepManager::getInstance().registerWakeupPin(button->getPin());

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

};