#pragma once
#include <Arduino.h>
#include "AnalogInput.h"
#include "DigitalInput.h"
#include "Utils.h"
#include "EspNowController.h"
#include "SleepManager.h"
#include "BatteryStatus.h"

class BatteryMonitor
{
private:
    float voltageDividerRatio = 1.0f;
    int batteryLevel = 0;
    BatteryStatus previousStatus = BatteryStatus::NOT_CONNECTED;
    BatteryStatus status = BatteryStatus::DISCHARGING;
    AnalogInput *batteryAdcInput;
    DigitalInput *powerGoodInput;
    DigitalInput *chargeInput;

    float batteryPresenceVoltageThreshold = 2.4f;
    float batteryFullyChargedVoltageThreshold = 4.0f;
    float batteryVoltageRangeMin = 3.0f;
    float batteryVoltageRangeMax = 4.15f;

    EventBits_t taskId;

    std::function<void(BatteryStatus, BatteryStatus)> batteryStatusChangeCallback = nullptr;

    static void monitorTask(void *pvParameters)
    {
        BatteryMonitor *instance = (BatteryMonitor *)pvParameters;
        instance->run();
    }

    float getBatteryVoltage()
    {
        int rawMilliVolts = batteryAdcInput->getReadingMilliVolts();
        return (rawMilliVolts / 1000.0f) / voltageDividerRatio;
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
        // === ADC STABILIZATION WARM-UP ===
        for (int i = 0; i < 50; i++)
        {
            getBatteryVoltage();
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        bool firstRun = true;
        int previousBatteryLevel = -1;

        while (true)
        {
            // 1. Read the ADC
            float voltage = getBatteryVoltage();
            calculateBatteryLevel(voltage);

            if (voltage < batteryPresenceVoltageThreshold)
            {
                status = BatteryStatus::NOT_CONNECTED;
                batteryLevel = 0;
            }
            else
            {
                bool isInputPowerGood = powerGoodInput->isActive();
                bool isCharging = chargeInput->isActive();

                if (isInputPowerGood && isCharging)
                {
                    status = BatteryStatus::CHARGING;
                }
                else if (isInputPowerGood && !isCharging)
                {
                    if (voltage < batteryPresenceVoltageThreshold)
                        status = BatteryStatus::NOT_CONNECTED;
                    else if (voltage < batteryFullyChargedVoltageThreshold)
                        status = BatteryStatus::DISCHARGING;
                    else
                        status = BatteryStatus::FULL_CHARGED;
                }
                else if (!isInputPowerGood && !isCharging)
                {
                    status = BatteryStatus::DISCHARGING;
                }
                else
                {
                    status = BatteryStatus::CHARGE_FAULT;
                }
            }

            // // === 2. SMART SLEEP LOCK LOGIC ===
            if (status == BatteryStatus::CHARGING || status == BatteryStatus::FULL_CHARGED)
            {
                // Infinite power available: keep the MCU awake permanently
                SleepManager::getInstance().keepAwake(this->taskId);
            }
            else
            {
                // On battery: allow the SleepManager to count down and sleep
                SleepManager::getInstance().allowSleep(this->taskId);
            }

            bool statusChanged = (status != previousStatus);

            bool shouldPublish = false;

            if (firstRun)
            {
                // Always publish once per wake cycle so HA knows the current level
                shouldPublish = true;
            }
            else if (statusChanged)
            {
                // Always publish immediately if a cable is plugged in or unplugged
                shouldPublish = true;
            }
            else if (abs(previousBatteryLevel - batteryLevel) >= 5)
            {
                shouldPublish = true;
            }

            if (shouldPublish)
            {
                if (batteryStatusChangeCallback)
                {
                    batteryStatusChangeCallback(previousStatus, status);
                }

                Message message;
                message.type = MessageType::BATTERY_STATUS;
                message.data.batteryLevel.level = batteryLevel;
                message.data.batteryLevel.status = status;
                EspNowController::getInstance().addMessage(message);

                firstRun = false;
                previousStatus = status;
                previousBatteryLevel = batteryLevel;
            }

            // 4. Wait 3 seconds before next ADC poll
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

public:
    BatteryMonitor(int adcPin, int pgoodPin, int chgPin)
    {
        batteryAdcInput = new AnalogInput(adcPin);
        powerGoodInput = new DigitalInput(pgoodPin, INPUT, true);
        chargeInput = new DigitalInput(chgPin, INPUT, true);
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
        this->taskId = SleepManager::getInstance().registerTask();
        SleepManager::getInstance().registerWakeupPin(powerGoodInput->getPin());
        SleepManager::getInstance().registerWakeupPin(chargeInput->getPin());
        xTaskCreate(BatteryMonitor::monitorTask, "Battery Monitor", 2048, this, 1, NULL);
    }

    void onBatteryStatusChange(std::function<void(BatteryStatus, BatteryStatus)> callback)
    {
        batteryStatusChangeCallback = callback;
    }
};