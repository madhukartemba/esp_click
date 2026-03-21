

enum BatteryStatus
{
    CHARGING,
    DISCHARGING,
    FULL_CHARGED,
    CHARGE_FAULT
};

class BatteryMonitor
{
private:
    int batterySensePin;
    float voltageDividerRatio;
};