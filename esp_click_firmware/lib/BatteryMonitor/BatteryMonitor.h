#include <Arduino.h>
#include "AnalogInput.h"
#include "DigitalInput.h"

enum BatteryStatus
{
    CHARGING,
    DISCHARGING,
    FULL_CHARGED,
    NOT_CONNECTED,
    CHARGE_FAULT,
};

class BatteryMonitor
{
private:
    float voltageDividerRatio;
    int batteryLevel = 0;
    BatteryStatus status = DISCHARGING;
    AnalogInput *batteryAdcInput;
    DigitalInput *powerGoodInput;
    DigitalInput *chargeInput;

public:
    BatteryMonitor(int adcPin, int pgoodPin, int chgPin)
    {
        batteryAdcInput = new AnalogInput(adcPin);
        powerGoodInput = new DigitalInput(pgoodPin, INPUT, true);
        chargeInput = new DigitalInput(chgPin, INPUT, true);
    }
};