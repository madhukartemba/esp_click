#pragma once

#include <Arduino.h>
#include <string>
#include <NimBLEDevice.h>
#include "BaseController.h"
#include "Message.h"

/**
 * BLE controller that advertises BTHome v2 packets so Home Assistant can discover
 * the device via the Bluetooth / BTHome integration (no Wi-Fi bridge).
 *
 * Uses UUID 0xFCD2 service data, trigger-based device info (0x44), GAP flags,
 * and complete local name in the scan response for discovery and naming.
 */
class BleController : public BaseController
{
private:
    NimBLEAdvertising *advertising = nullptr;
    const char *deviceName = "ESP-Click";
    uint8_t packetId = 0;

    BleController() = default;

    static uint8_t pressEventToBthome(PressEvent ev)
    {
        switch (ev)
        {
        case SINGLE_PRESS:
            return 0x01;
        case DOUBLE_PRESS:
            return 0x02;
        case LONG_PRESS:
            return 0x04;
        case NONE:
        default:
            return 0x00;
        }
    }

    bool initBle()
    {
        NimBLEDevice::init(std::string(deviceName));
        advertising = NimBLEDevice::getAdvertising();
        advertising->setName(std::string(deviceName));
        advertising->setMinInterval(32);
        advertising->setMaxInterval(160);
        return advertising != nullptr;
    }

    void broadcastBthomePayload(const uint8_t *payload, size_t len)
    {
        if (advertising == nullptr || len == 0)
        {
            return;
        }

        NimBLEAdvertisementData adv;
        adv.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);

        NimBLEUUID bthomeUuid((uint16_t)0xfcd2);
        adv.setServiceData(bthomeUuid, payload, len);

        NimBLEAdvertisementData scan;
        scan.setName(std::string(deviceName), true);

        advertising->setAdvertisementData(adv);
        advertising->setScanResponseData(scan);

        advertising->start();
        for (int i = 0; i < 5; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        advertising->stop();
    }

    void sendButtonMessage(const Message &message)
    {
        const int pressedIdx = message.data.buttonPress.buttonId;
        if (pressedIdx < 0 || pressedIdx > 3)
        {
            return;
        }
        const PressEvent activeEvent = message.data.buttonPress.event;

        // BTHome: object ids in numerical order — 0x00 packet id, then four 0x3A button events.
        uint8_t payload[32];
        size_t o = 0;

        payload[o++] = 0x44;
        payload[o++] = 0x00;
        payload[o++] = packetId++;

        for (int b = 0; b < 4; b++)
        {
            payload[o++] = 0x3A;
            payload[o++] = (b == pressedIdx) ? pressEventToBthome(activeEvent) : 0x00;
        }

        broadcastBthomePayload(payload, o);
    }

    void sendBatteryMessage(const Message &message)
    {
        uint8_t level = static_cast<uint8_t>(message.data.batteryLevel.level);
        if (level > 100)
        {
            level = 100;
        }

        uint8_t payload[8];
        size_t o = 0;

        payload[o++] = 0x44;
        payload[o++] = 0x00;
        payload[o++] = packetId++;
        payload[o++] = 0x01;
        payload[o++] = level;

        broadcastBthomePayload(payload, o);
    }

    void run() override
    {
        if (!initBle())
        {
            Serial.println("Failed to initialize BLE. BLE controller task will idle.");
            while (true)
            {
                vTaskDelay(portMAX_DELAY);
            }
        }

        while (true)
        {
            Message message;
            if (xQueueReceive(messageQueue, &message, portMAX_DELAY))
            {
                if (message.type == MessageType::BUTTON_PRESS)
                {
                    sendButtonMessage(message);
                }
                else if (message.type == MessageType::BATTERY_STATUS)
                {
                    sendBatteryMessage(message);
                }

                SleepManager::getInstance().allowSleep(this->taskId);
            }
        }
    }

public:
    static BleController &getInstance()
    {
        static BleController instance;
        return instance;
    }

    BleController(const BleController &) = delete;
    BleController &operator=(const BleController &) = delete;

    void begin(const char *name)
    {
        deviceName = name;
        startControllerTask("BLE BTHome Task", 6144, 1, 10);
    }
};
