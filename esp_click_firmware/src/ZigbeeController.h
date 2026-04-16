#pragma once

/**
 * Zigbee end device: four Binary Input endpoints (one per physical button).
 * Pulses the reported value on each button event for coordinators (e.g. ZHA / Zigbee2MQTT).
 *
 * Requires Arduino-ESP32 with Zigbee end-device enabled in sdkconfig (CONFIG_ZB_ZED) and
 * the Zigbee link libraries / partition scheme from env esp32-c6-devkitm-1-zigbee.
 */

#include <Arduino.h>
#include "Zigbee.h"
#include "BaseController.h"
#include "BoardConfig.h"
#include "Message.h"

class ZigbeeController : public BaseController
{
private:
    static constexpr uint8_t kFirstEndpoint = 1;
    static constexpr uint32_t kPulseMs = 80;

    ZigbeeBinary ep1{kFirstEndpoint + 0};
    ZigbeeBinary ep2{kFirstEndpoint + 1};
    ZigbeeBinary ep3{kFirstEndpoint + 2};
    ZigbeeBinary ep4{kFirstEndpoint + 3};

    ZigbeeController() = default;

    ZigbeeBinary *endpointForPin(int pin)
    {
        if (pin == BoardConfig::BTN1_PIN)
        {
            return &ep1;
        }
        if (pin == BoardConfig::BTN2_PIN)
        {
            return &ep2;
        }
        if (pin == BoardConfig::BTN3_PIN)
        {
            return &ep3;
        }
        if (pin == BoardConfig::BTN4_PIN)
        {
            return &ep4;
        }
        return nullptr;
    }

    void pulseBinaryInput(ZigbeeBinary *ep)
    {
        if (ep == nullptr || !Zigbee.connected())
        {
            return;
        }
        ep->setBinaryInput(true);
        ep->reportBinaryInput();
        vTaskDelay(pdMS_TO_TICKS(kPulseMs));
        ep->setBinaryInput(false);
        ep->reportBinaryInput();
    }

    void configureEndpoints(const char *manufacturer, const char *model)
    {
        const char *labels[4] = {"Button 1", "Button 2", "Button 3", "Button 4"};
        ZigbeeBinary *eps[4] = {&ep1, &ep2, &ep3, &ep4};

        for (int i = 0; i < 4; i++)
        {
            eps[i]->setManufacturerAndModel(manufacturer, model);
            eps[i]->addBinaryInput();
            eps[i]->setBinaryInputApplication(BINARY_INPUT_APPLICATION_TYPE_HVAC_OTHER);
            eps[i]->setBinaryInputDescription(labels[i]);
            Zigbee.addEndpoint(eps[i]);
        }
    }

    void run() override
    {
        while (true)
        {
            Message message;
            if (xQueueReceive(messageQueue, &message, portMAX_DELAY))
            {
                if (message.type == MessageType::BUTTON_PRESS)
                {
                    ZigbeeBinary *ep = endpointForPin(message.data.buttonPress.buttonId);
                    pulseBinaryInput(ep);
                }

                SleepManager::getInstance().allowSleep(this->taskId);
            }
        }
    }

public:
    static ZigbeeController &getInstance()
    {
        static ZigbeeController instance;
        return instance;
    }

    ZigbeeController(const ZigbeeController &) = delete;
    ZigbeeController &operator=(const ZigbeeController &) = delete;

    /**
     * Registers endpoints and starts the Zigbee stack (call from setup()).
     */
    void begin(const char *manufacturer, const char *model)
    {
        configureEndpoints(manufacturer, model);

        if (!Zigbee.begin())
        {
            Serial.println("Zigbee failed to start");
            return;
        }

        startControllerTask("Zigbee Task", 6144, 1, 10);
    }
};
