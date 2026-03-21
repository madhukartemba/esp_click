#pragma once

class IOPin
{
private:
    int pin;

public:
    IOPin(int pin) : pin(pin)
    {
    }

    int getPin() const
    {
        return pin;
    }
};