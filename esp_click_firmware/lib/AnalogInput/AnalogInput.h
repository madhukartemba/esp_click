#pragma once
#include <Arduino.h>

class AnalogInput : public IOPin
{

public:
    AnalogInput(int pin) : IOPin(pin)
    {
        pinMode(getPin(), INPUT);
    }

    int getPin() const
    {
        return IOPin::getPin();
    }

    int getReadingMilliVolts() const
    {
        return analogReadMilliVolts(getPin());
    }
};