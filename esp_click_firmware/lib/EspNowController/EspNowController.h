#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <Arduino.h>
#include <esp_now.h>
#include "SleepManager.h"
#include "Message.h"

struct LastSendNode
{
    uint8_t lastChannel = 1;
    uint8_t targetMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool isNodeKnown = false;
};

struct BestNode
{
    uint8_t mac[6];
    int channel;
    int rssi; // Higher (less negative) is better. e.g., -50 is better than -80.
    bool found;
};

BestNode bestFoundNode = {{0}, 0, -127, false};

RTC_DATA_ATTR LastSendNode lastSendNode;

class EspNowController
{
private:
    QueueHandle_t messageQueue;
    EventBits_t taskId;

    static EspNowController *s_instance;

    volatile bool appAckReceived = false;
    volatile uint32_t expectedMessageId = 0;
    volatile uint8_t currentSweepChannel = 0; // Tracks the channel during discovery
    uint32_t messageCounter = 0;

    // Timeout for targeted sends
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
                appAckReceived = true; // Signal targeted send that it succeeded

                // If we are in discovery mode (node is unknown), evaluate the RSSI
                if (!lastSendNode.isNodeKnown)
                {
                    int currentRssi = info->rx_ctrl->rssi;

                    // If this is the best signal we've seen so far, save it
                    if (currentRssi > bestFoundNode.rssi)
                    {
                        memcpy(bestFoundNode.mac, info->src_addr, 6);
                        bestFoundNode.rssi = currentRssi;
                        bestFoundNode.channel = currentSweepChannel;
                        bestFoundNode.found = true;
                    }
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
                SleepManager::getInstance().allowSleep(this->taskId);
            }
        }
    }

    bool sendMessageToKnownNode(Message *message)
    {
        Serial.printf("Targeting known node on channel %d\n", lastSendNode.lastChannel);

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

        // Reset discovery tracking before we start
        bestFoundNode = {{0}, 0, -127, false};
        lastSendNode.isNodeKnown = false; // Ensures the callback records RSSI

        esp_now_peer_info_t bcPeer = {};
        memcpy(bcPeer.peer_addr, broadcastMAC, 6);
        bcPeer.channel = 0;
        bcPeer.encrypt = false;
        bcPeer.ifidx = WIFI_IF_STA;

        esp_now_add_peer(&bcPeer);

        Message pingMsg;
        pingMsg.deviceId = 0;
        pingMsg.type = DISCOVERY_REQUEST;

        // Sweep all 13 channels
        for (int i = 1; i <= 13; i++)
        {
            esp_wifi_set_channel(i, WIFI_SECOND_CHAN_NONE);
            currentSweepChannel = i; // Update global so the callback knows the channel

            pingMsg.counter = ++messageCounter;
            expectedMessageId = pingMsg.counter;

            esp_now_send(broadcastMAC, (uint8_t *)&pingMsg, sizeof(Message));

            // Fixed delay to allow ALL nodes on this channel time to reply
            // We do NOT use waitForAck() here because that stops at the first reply
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        esp_now_del_peer(broadcastMAC);

        // Evaluate results after the full sweep
        if (bestFoundNode.found)
        {
            Serial.printf("Best Presence Node found on CH %d with RSSI %d!\n", bestFoundNode.channel, bestFoundNode.rssi);
            lastSendNode.lastChannel = bestFoundNode.channel;
            memcpy(lastSendNode.targetMAC, bestFoundNode.mac, 6);
            lastSendNode.isNodeKnown = true;
        }
        else
        {
            Serial.println("No nodes found during sweep.");
        }

        return lastSendNode.isNodeKnown;
    }

    bool sendMessage(Message *message)
    {
        if (!lastSendNode.isNodeKnown && !findNodeViaBroadcast())
        {
            Serial.println("Failed to find any Presence Nodes via broadcast sweep");
            return false;
        }

        if (sendMessageToKnownNode(message))
        {
            Serial.println("Message sent and ACK received successfully!");
            return true;
        }

        Serial.println("Failed to send to known node, broadcasting again to rediscover");
        if (findNodeViaBroadcast())
        {
            return sendMessageToKnownNode(message);
        }

        return false;
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

    void addMessage(Message message)
    {
        if (messageQueue != NULL)
        {
            SleepManager::getInstance().keepAwake(this->taskId);
            xQueueSend(messageQueue, &message, 0);
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

EspNowController *EspNowController::s_instance = nullptr;