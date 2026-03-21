#pragma once
#include <Arduino.h>

class DigitalInput
{
private:
    int pin;
    uint8_t mode;
    bool flipped = false;

public:
    DigitalInput(int pin, uint8_t mode, bool flipped = false) : pin(pin), mode(mode), flipped(flipped)
    {
        pinMode(pin, mode);
    }

    bool isActive() const
    {
        return digitalRead(pin) != flipped;
    }

    void setFlipped(bool flipped)
    {
        this->flipped = flipped;
    }

    int getPin() const
    {
        return pin;
    }
};