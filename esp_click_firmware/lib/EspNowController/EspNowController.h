#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <Arduino.h>
#include <esp_now.h>
#include "ButtonManager.h"
#include "BatteryMonitor.h"
#include "SleepManager.h"

struct LastSendNode
{
    uint8_t lastChannel = 1;
    uint8_t targetMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool isNodeKnown = false;
};

RTC_DATA_ATTR LastSendNode lastSendNode;

enum MessageType
{
    BUTTON_PRESS,
    BATTERY_STATUS,
    DISCOVERY_REQUEST
};

// Application-level ACK structure MUST be packed
struct __attribute__((packed)) AckMessage
{
    uint32_t counter;
    bool success;
};

// Message structure MUST be packed
struct __attribute__((packed)) Message
{
    uint32_t counter;
    int deviceId = 0;
    MessageType type;
    union
    {
        struct
        {
            int buttonId;
            PressEvent event;
        } buttonPress;

        struct
        {
            int level;
            BatteryStatus status;
        } batteryLevel;
    } data;
};

class EspNowController
{
private:
    QueueHandle_t messageQueue;
    EventBits_t taskId;

    static EspNowController *s_instance;

    volatile bool appAckReceived = false;
    volatile uint32_t expectedMessageId = 0;
    uint32_t messageCounter = 0;

    // Increased timeout to 100ms because an app-level reply takes slightly longer than a hardware ACK
    unsigned long ackWaitTimeout = 100;

    std::function<void(Message)> onBeforeSend = nullptr;
    std::function<void(Message, bool)> onAfterSend = nullptr;

    EspNowController() {}
    ~EspNowController() {}

    static void controllerTask(void *pvParameters)
    {
        EspNowController *instance = (EspNowController *)pvParameters;
        instance->run();
    }

    // Switched to Receive Callback
    static void IRAM_ATTR binRecvCb(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len)
    {
        if (s_instance)
        {
            s_instance->onDataReceived(info, incomingData, len);
        }
    }

    void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len)
    {
        if (len == sizeof(AckMessage))
        {
            AckMessage *ack = (AckMessage *)incomingData;

            if (ack->counter == expectedMessageId && ack->success)
            {
                appAckReceived = true;

                // If we are broadcasting and find a node, save its MAC address
                if (!lastSendNode.isNodeKnown)
                {
                    memcpy(lastSendNode.targetMAC, info->src_addr, 6);
                }
            }
        }
    }

    void run()
    {
        bool initSuccessful = initEspNow();

        if (!initSuccessful)
        {
            Serial.println("Failed to initialize ESP-NOW. Controller task will not run.");
            vTaskDelay(portMAX_DELAY);
        }

        while (true)
        {
            Message message;
            if (xQueueReceive(messageQueue, &message, portMAX_DELAY))
            {
                // Assign a unique ID to the message before sending
                message.counter = ++messageCounter;

                if (onBeforeSend)
                {
                    onBeforeSend(message);
                }

                bool success = sendMessage(&message);

                if (onAfterSend)
                {
                    onAfterSend(message, success);
                }
            }
        }
    }

    bool sendMessageToKnownNode(Message *message)
    {
        Serial.printf("Targeting known node on channel %d\n", lastSendNode.lastChannel);

        // Switch to the known channel
        esp_wifi_set_channel(lastSendNode.lastChannel, WIFI_SECOND_CHAN_NONE);

        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, lastSendNode.targetMAC, 6);
        peerInfo.channel = lastSendNode.lastChannel;
        peerInfo.ifidx = WIFI_IF_STA;
        peerInfo.encrypt = false;

        if (esp_now_add_peer(&peerInfo) != ESP_OK)
        {
            Serial.println("Failed to add peer");
            return false;
        }

        // Prepare for ACK
        expectedMessageId = message->counter;
        appAckReceived = false;

        esp_now_send(lastSendNode.targetMAC, (uint8_t *)message, sizeof(Message));

        bool ackResult = waitForAck();

        esp_now_del_peer(lastSendNode.targetMAC);

        if (!ackResult)
        {
            Serial.println("App-level ACK wait failed, marking node as unknown");
            lastSendNode.isNodeKnown = false;
        }
        return ackResult;
    }

    bool findNodeViaBroadcast()
    {
        Serial.println("Broadcasting DISCOVERY ping (sweeping channels)...");
        uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

        esp_now_peer_info_t bcPeer = {};
        memcpy(bcPeer.peer_addr, broadcastMAC, 6);
        bcPeer.channel = 0;
        bcPeer.encrypt = false;
        bcPeer.ifidx = WIFI_IF_STA;

        esp_now_add_peer(&bcPeer);

        // Create a dummy message just for discovery
        Message pingMsg;
        pingMsg.deviceId = 0;
        pingMsg.type = DISCOVERY_REQUEST;

        for (int i = 1; i <= 13; i++)
        {
            esp_wifi_set_channel(i, WIFI_SECOND_CHAN_NONE);

            pingMsg.counter = ++messageCounter; // Assign unique ID to ping
            expectedMessageId = pingMsg.counter;
            appAckReceived = false;

            esp_now_send(broadcastMAC, (uint8_t *)&pingMsg, sizeof(Message));

            if (waitForAck())
            {
                Serial.printf("Presence Node found on channel %d! Target MAC saved.\n", i);
                lastSendNode.lastChannel = i;
                lastSendNode.isNodeKnown = true;
                // Target MAC was already saved by your onDataReceived callback
                break;
            }
        }

        esp_now_del_peer(broadcastMAC);
        return lastSendNode.isNodeKnown;
    }

    // 3. Update sendMessage to use the new flow
    bool sendMessage(Message *message)
    {
        // If we don't know a node, find one first using the Ping
        if (!lastSendNode.isNodeKnown)
        {
            bool foundNode = findNodeViaBroadcast();
            if (!foundNode)
            {
                Serial.println("Failed to find any Presence Nodes via broadcast sweep");
                return false;
            }
        }

        // Now that we definitely have a known MAC (either cached or just found),
        // send the ACTUAL payload directly to that specific node.
        return sendMessageToKnownNode(message);
    }

    bool waitForAck()
    {
        unsigned long start = millis();
        while (!appAckReceived)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            if (millis() - start > ackWaitTimeout)
            {
                return false;
            }
        }
        return true;
    }

    bool initEspNow()
    {
        if (esp_now_init() != ESP_OK)
        {
            Serial.println("Error initializing ESP-NOW");
            return false;
        }

        // Register the RECEIVE callback for application-level ACKs
        esp_now_register_recv_cb(binRecvCb);

        return true;
    }

public:
    static EspNowController &getInstance()
    {
        static EspNowController instance;
        return instance;
    }

    EspNowController(const EspNowController &) = delete;
    EspNowController &operator=(const EspNowController &) = delete;

    void begin()
    {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();

        s_instance = this;

        this->messageQueue = xQueueCreate(10, sizeof(Message));
        this->taskId = SleepManager::getInstance().registerTask();

        xTaskCreate(EspNowController::controllerTask, "ESP-NOW Task", 4096, this, 1, NULL);
    }

    void addMessage(Message *message)
    {
        if (messageQueue != NULL)
        {
            xQueueSend(messageQueue, message, 0);
        }
    }

    void registerOnBeforeSend(std::function<void(Message)> callback)
    {
        this->onBeforeSend = callback;
    }

    void registerOnAfterSend(std::function<void(Message, bool)> callback)
    {
        this->onAfterSend = callback;
    }
};

// Define the static instance pointer outside the class
EspNowController *EspNowController::s_instance = nullptr;