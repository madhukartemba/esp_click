#include <Arduino.h>

enum ButtonState
{
    IDLE,
    PRESSED,
    WAITING_FOR_DOUBLE_PRESS,
    AWAITING_RELEASE
};

enum ButtonEvent
{
    NONE,
    SINGLE_PRESS,
    DOUBLE_PRESS,
    LONG_PRESS,
};

class Button
{
private:
    int pin;
    uint8_t mode;
    bool flipped = false;
    ButtonState state = IDLE;
    ButtonEvent event = NONE;
    unsigned long pressTimer = 0;

    unsigned long longPressDuration = 1000; // Duration to consider a long press (in milliseconds)
    unsigned long doublePressGap = 250;     // Max gap between presses for a double press (in milliseconds)

    bool isButtonPressed()
    {
        return digitalRead(pin) != flipped;
    }

public:
    Button(int pin, uint8_t mode, bool flipped = false) : pin(pin), mode(mode), flipped(flipped)
    {
        pinMode(pin, mode);
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
                Serial.println("Button Pressed switching to PRESSED");
            }
            break;
        case PRESSED:
            if (isButtonPressed() && (millis() - pressTimer) > longPressDuration)
            {
                // Button is long pressed
                event = LONG_PRESS;
                state = AWAITING_RELEASE;
                Serial.println("Button Long Press detected switching to AWAITING_RELEASE");
            }
            else if (!isButtonPressed())
            {
                state = WAITING_FOR_DOUBLE_PRESS;
                pressTimer = millis();
                Serial.println("Button Released switching to WAITING_FOR_DOUBLE_PRESS");
            }
            break;
        case WAITING_FOR_DOUBLE_PRESS:
            if (!isButtonPressed() && (millis() - pressTimer) > doublePressGap)
            {
                event = SINGLE_PRESS;
                state = IDLE;
                Serial.println("Button Single Press detected switching to IDLE");
            }
            else if (isButtonPressed())
            {
                event = DOUBLE_PRESS;
                state = AWAITING_RELEASE;
                Serial.println("Button Double Press detected switching to AWAITING_RELEASE");
            }
            break;
        case AWAITING_RELEASE:
            if (!isButtonPressed())
            {
                state = IDLE;
                Serial.println("Button Released switching to IDLE");
            }
            break;
        default:
            break;
        };
    }

    bool hasEvent()
    {
        return event != NONE;
    }

    ButtonEvent getEvent()
    {
        ButtonEvent currentEvent = event;
        event = NONE;

        Serial.print("getEvent called, returning: ");
        switch (currentEvent)
        {
        case SINGLE_PRESS:
            Serial.println("SINGLE_PRESS");
            break;
        case DOUBLE_PRESS:
            Serial.println("DOUBLE_PRESS");
            break;
        case LONG_PRESS:
            Serial.println("LONG_PRESS");
            break;
        default:
            Serial.println("NONE");
            break;
        }
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

    void setFlipped(bool isFlipped)
    {
        flipped = isFlipped;
    }
};