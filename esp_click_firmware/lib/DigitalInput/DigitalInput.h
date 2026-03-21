#pragma once
#include <Arduino.h>
#include "IOPin.h"

class DigitalInput : public IOPin
{
private:
    uint8_t mode;
    bool flipped = false;

public:
    DigitalInput(int pin, uint8_t mode, bool flipped = false) : IOPin(pin), mode(mode), flipped(flipped)
    {
        pinMode(pin, mode);
    }

    bool isActive() const
    {
        return digitalRead(getPin()) != flipped;
    }

    void setFlipped(bool flipped)
    {
        this->flipped = flipped;
    }

    int getPin() const
    {
        return IOPin::getPin();
    }
};