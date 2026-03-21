#pragma once
#include <Arduino.h>

class AnalogInput
{
private:
    int pin;

public:
    AnalogInput(int pin) : pin(pin)
    {
        pinMode(pin, INPUT);
    }

    int getPin() const
    {
        return pin;
    }

    int getReading() const
    {
        return analogRead(pin);
    }
};