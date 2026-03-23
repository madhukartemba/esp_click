#pragma once
#include <Arduino.h>
#include "DigitalInput.h"

enum ButtonState
{
    IDLE,
    PRESSED,
    WAITING_FOR_DOUBLE_PRESS,
    AWAITING_RELEASE
};

enum PressEvent
{
    NONE,
    SINGLE_PRESS,
    DOUBLE_PRESS,
    LONG_PRESS,
};

class Button : public DigitalInput
{
private:
    ButtonState state = IDLE;
    ButtonState previousState = IDLE;
    PressEvent event = NONE;

    unsigned long pressTimer = 0;
    unsigned long longPressDuration = 1000; // Duration to consider a long press (in milliseconds)
    unsigned long doublePressGap = 250;     // Max gap between presses for a double press (in milliseconds)

    std::function<void(ButtonState)> stateChangeCallback;

    bool isButtonPressed()
    {
        return isActive();
    }

public:
    Button(int pin, uint8_t mode, bool flipped = false) : DigitalInput(pin, mode, flipped)
    {
    }

    void update()
    {
        switch (state)
        {
        case IDLE:
            if (isButtonPressed())
            {
                state = PRESSED;
                pressTimer = millis();
            }
            break;
        case PRESSED:
            if (isButtonPressed() && (millis() - pressTimer) > longPressDuration)
            {
                // Button is long pressed
                event = LONG_PRESS;
                state = AWAITING_RELEASE;
            }
            else if (!isButtonPressed())
            {
                state = WAITING_FOR_DOUBLE_PRESS;
                pressTimer = millis();
            }
            break;
        case WAITING_FOR_DOUBLE_PRESS:
            if (!isButtonPressed() && (millis() - pressTimer) > doublePressGap)
            {
                event = SINGLE_PRESS;
                state = IDLE;
            }
            else if (isButtonPressed())
            {
                event = DOUBLE_PRESS;
                state = AWAITING_RELEASE;
            }
            break;
        case AWAITING_RELEASE:
            if (!isButtonPressed())
            {
                state = IDLE;
            }
            break;
        default:
            break;
        };

        if (state != previousState)
        {
            previousState = state;
            if (stateChangeCallback)
            {
                stateChangeCallback(state);
            }
        }
    }

    bool hasEvent()
    {
        return event != NONE;
    }

    PressEvent getEvent()
    {
        PressEvent currentEvent = event;
        event = NONE;
        return currentEvent;
    }

    void setLongPressDuration(unsigned long duration)
    {
        longPressDuration = duration;
    }

    void setDoublePressGap(unsigned long gap)
    {
        doublePressGap = gap;
    }

    void registerStateChangeCallback(std::function<void(ButtonState)> callback)
    {
        stateChangeCallback = callback;
    }
};