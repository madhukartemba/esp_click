#pragma once
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
};

struct Message
{
    int deviceId = 0;
    int entityId;
    MessageType type;
    union
    {
        struct
        {
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

    bool ackReceived = false;
    bool initSuccessful = false;

    unsigned long ackWaitTimeout = 50; // 50 ms default timeout for waiting for ACK

    std::function<void(Message)> onBeforeSend = nullptr;
    std::function<void(Message, bool)> onAfterSend = nullptr;

    EspNowController() {}
    ~EspNowController() {}

    static void controllerTask(void *pvParameters)
    {
        EspNowController *instance = (EspNowController *)pvParameters;
        instance->run();
    }

    void onAckReceived()
    {
        ackReceived = true;
    }

    void run()
    {

        initSuccessful = initEspNow();

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
        Serial.println("Sending to known node...");
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

        esp_now_send(lastSendNode.targetMAC, (uint8_t *)message, sizeof(Message));

        bool ackResult = waitForAck();

        esp_now_del_peer(lastSendNode.targetMAC);

        if (!ackResult)
        {
            Serial.println("ACK wait failed, marking node as unknown");
            lastSendNode.isNodeKnown = false;
        }
        return ackResult;
    }

    bool broadcastMessage(Message *message)
    {
        Serial.println("Broadcasting message...");
        uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

        esp_now_peer_info_t bcPeer = {};
        memcpy(bcPeer.peer_addr, broadcastMAC, 6);
        bcPeer.channel = 0;
        bcPeer.encrypt = false;
        bcPeer.ifidx = WIFI_IF_STA;

        esp_now_add_peer(&bcPeer);

        for (int i = 1; i <= 13; i++)
        {
            esp_now_send(broadcastMAC, (uint8_t *)message, sizeof(Message));
            bool ackResult = waitForAck();
            if (ackResult)
            {
                Serial.printf("ACK received on channel %d! Updating known node info.\n", i);
                lastSendNode.lastChannel = i;
                memcpy(lastSendNode.targetMAC, broadcastMAC, 6);
                lastSendNode.isNodeKnown = true;
                break;
            }
        }

        esp_now_del_peer(broadcastMAC);

        return lastSendNode.isNodeKnown;
    }

    bool sendMessage(Message *message)
    {
        if (lastSendNode.isNodeKnown)
        {
            return sendMessageToKnownNode(message);
        }
        else
        {
            bool broadcastSuccess = broadcastMessage(message);
            if (!broadcastSuccess)
            {
                Serial.println("Failed to send message via broadcast");
                return false;
            }

            return sendMessageToKnownNode(message);
        }
    }

    bool waitForAck()
    {
        while (!ackReceived)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        ackReceived = false;
        return true;
    }

    bool initEspNow()
    {
        if (esp_now_init() != ESP_OK)
        {
            Serial.println("Error initializing ESP-NOW");
            return false;
        }

        esp_now_register_send_cb([](const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
                                 { EspNowController::getInstance().onAckReceived(); });

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

    void begin(QueueHandle_t messageQueue)
    {
        this->messageQueue = messageQueue;
        this->taskId = SleepManager::getInstance().registerTask();

        xTaskCreate(EspNowController::controllerTask, "ESP-NOW Task", 2048, this, 1, NULL);
    }

    void addMessage(Message *message)
    {
        xQueueSend(messageQueue, message, 0);
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