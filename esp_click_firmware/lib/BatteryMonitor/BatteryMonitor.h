#include <Arduino.h>
#include "AnalogInput.h"
#include "DigitalInput.h"
#include "Utils.h"

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
    float voltageDividerRatio = 1.0f;
    int batteryLevel = 0;
    BatteryStatus previousStatus = NOT_CONNECTED;
    BatteryStatus status = DISCHARGING;
    AnalogInput *batteryAdcInput;
    DigitalInput *powerGoodInput;
    DigitalInput *chargeInput;

    float batteryPresenceVoltageThreshold = 2.4f;
    float batteryFullyChargedVoltageThreshold = 4.0f;
    float batteryVoltageRangeMin = 3.0f;
    float batteryVoltageRangeMax = 4.2f;

    void (*batteryStatusChangeCallback)(BatteryStatus) = nullptr;

    static void monitorTask(void *pvParameters)
    {
        BatteryMonitor *instance = (BatteryMonitor *)pvParameters;
        instance->run();
    }

    float getBatteryVoltage()
    {
        int adcValue = batteryAdcInput->getReading();
        float voltage = (adcValue / 4095.0f) * 3.3f * voltageDividerRatio;
        return voltage;
    }

    void calculateBatteryLevel()
    {
        float voltage = getBatteryVoltage();
        calculateBatteryLevel(voltage);
    }

    void calculateBatteryLevel(float batteryVoltage)
    {
        batteryLevel = (int)Utils::mapFloat(batteryVoltage, batteryVoltageRangeMin, batteryVoltageRangeMax, 0.0f, 100.0f);
        if (batteryLevel < 0)
            batteryLevel = 0;
        else if (batteryLevel > 100)
            batteryLevel = 100;
    }

    void run()
    {
        while (true)
        {
            float voltage = getBatteryVoltage();
            calculateBatteryLevel(voltage);

            if (voltage < 2.4f)
            {
                status = NOT_CONNECTED;
                batteryLevel = 0;
                continue;
            }

            bool isInputPowerGood = powerGoodInput->isActive();
            bool isCharging = chargeInput->isActive();

            if (isInputPowerGood && isCharging)
            {
                status = CHARGING;
            }
            else if (isInputPowerGood && !isCharging)
            {
                if (voltage < batteryPresenceVoltageThreshold)
                {
                    status = NOT_CONNECTED;
                }
                else if (voltage < batteryFullyChargedVoltageThreshold)
                {
                    status = DISCHARGING;
                }
                else
                {
                    status = FULL_CHARGED;
                }
            }
            else if (!isInputPowerGood && !isCharging)
            {
                status = DISCHARGING;
            }
            else
            {
                status = CHARGE_FAULT;
            }

            if (previousStatus != status)
            {
                if (batteryStatusChangeCallback)
                {
                    batteryStatusChangeCallback(status);
                }
                previousStatus = status;
            }

            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

public:
    BatteryMonitor(int adcPin, int pgoodPin, int chgPin)
    {
        batteryAdcInput = new AnalogInput(adcPin);
        powerGoodInput = new DigitalInput(pgoodPin, INPUT, true);
        chargeInput = new DigitalInput(chgPin, INPUT, true);

        // Instantly read the battery level and status on startup
        calculateBatteryLevel();
        run();
    }

    void setVoltageDividerRatio(float ratio)
    {
        voltageDividerRatio = ratio;
    }

    int getBatteryLevel() const
    {
        return batteryLevel;
    }

    BatteryStatus getBatteryStatus() const
    {
        return status;
    }

    void begin()
    {
        xTaskCreate(BatteryMonitor::monitorTask, "Battery Monitor", 2048, this, 1, NULL);
    }

    void onBatteryStatusChange(void (*callback)(BatteryStatus))
    {
        batteryStatusChangeCallback = callback;
    }
};