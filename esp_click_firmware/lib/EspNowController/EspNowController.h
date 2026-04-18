#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <Arduino.h>
#include <esp_now.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include "BaseController.h"

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
RTC_DATA_ATTR uint8_t sharedEncryptionKey[16] = {0}; // The final AES key
RTC_DATA_ATTR bool isPaired = false;                 // Tracks if we have exchanged keys

class EspNowController : public BaseController
{
private:
    static EspNowController *s_instance;

    volatile bool appAckReceived = false;
    volatile uint32_t expectedMessageId = 0;
    volatile uint8_t currentSweepChannel = 0; // Tracks the channel during discovery
    uint32_t messageCounter = 0;

    // Pairing State Variables
    volatile bool pairingResponseReceived = false;
    uint8_t peerPublicKey[65];
    size_t peerPublicKeyLen = 0;

    // Timeout for targeted sends
    unsigned long ackWaitTimeout = 100;

    std::function<void(Message)> onBeforeSend = nullptr;
    std::function<void(Message, bool)> onAfterSend = nullptr;

    EspNowController() {}
    ~EspNowController() {}

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

                if (!lastSendNode.isNodeKnown)
                {
                    int currentRssi = info->rx_ctrl->rssi;
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
        // 2. Handle Key Exchange Responses (Cleartext Message size)
        else if (len == sizeof(Message))
        {
            Message *msg = (Message *)incomingData;
            if (msg->type == PAIRING_RESPONSE)
            {
                // Save the receiver's public key so the main thread can process it
                memcpy(peerPublicKey, msg->data.pairing.publicKey, msg->data.pairing.keyLen);
                peerPublicKeyLen = msg->data.pairing.keyLen;

                // Also grab their MAC and Channel so we know where to send future encrypted packets
                memcpy(lastSendNode.targetMAC, info->src_addr, 6);
                lastSendNode.lastChannel = info->rx_ctrl->channel;
                lastSendNode.isNodeKnown = true;

                pairingResponseReceived = true;
            }
        }
    }

    void run() override
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

        startControllerTask("ESP-NOW Task", 4096, 1, 10);
    }

    bool initiatePairing()
    {
        Serial.println("Initiating ECDH Key Exchange...");

        mbedtls_ecdh_context ecdh;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_entropy_context entropy;

        mbedtls_ecdh_init(&ecdh);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_entropy_init(&entropy);

        // 1. Seed the random number generator
        const char *pers = "esp_click_pairing";
        mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers));

        // 2. Setup the ECDH context with the SECP256R1 Curve (mbedTLS 3.x API)
        if (mbedtls_ecdh_setup(&ecdh, MBEDTLS_ECP_DP_SECP256R1) != 0)
        {
            Serial.println("Failed to setup ECDH context");
            return false;
        }

        // 3. Prepare the Pairing Request Message
        Message pairingMsg;
        pairingMsg.type = PAIRING_REQUEST;
        pairingMsg.counter = ++messageCounter;

        // 4. Generate Key Pair AND write the public key directly to our message buffer (mbedTLS 3.x API)
        size_t olen = 0;
        if (mbedtls_ecdh_make_public(&ecdh, &olen, pairingMsg.data.pairing.publicKey, 65, mbedtls_ctr_drbg_random, &ctr_drbg) != 0)
        {
            Serial.println("Failed to generate public key");
            return false;
        }
        pairingMsg.data.pairing.keyLen = olen;

        pairingResponseReceived = false;

        // 5. Broadcast the public key across all channels
        uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_now_peer_info_t bcPeer = {};
        memcpy(bcPeer.peer_addr, broadcastMAC, 6);
        bcPeer.channel = 0;
        bcPeer.encrypt = false;
        bcPeer.ifidx = WIFI_IF_STA;
        esp_now_add_peer(&bcPeer);

        for (int i = 1; i <= 13; i++)
        {
            esp_wifi_set_channel(i, WIFI_SECOND_CHAN_NONE);
            esp_now_send(broadcastMAC, (uint8_t *)&pairingMsg, sizeof(Message));
            vTaskDelay(pdMS_TO_TICKS(50)); // Wait 50ms per channel for a receiver to catch it
        }
        esp_now_del_peer(broadcastMAC);

        // 6. Wait for a receiver to respond with their public key
        Serial.println("Public key broadcasted. Waiting for Receiver response...");
        unsigned long start = millis();
        while (!pairingResponseReceived && millis() - start < 3000)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // 7. Compute Shared Secret
        if (pairingResponseReceived)
        {
            // Read the Receiver's public key point (mbedTLS 3.x API)
            if (mbedtls_ecdh_read_public(&ecdh, peerPublicKey, peerPublicKeyLen) == 0)
            {

                // Compute the raw shared secret (mbedTLS 3.x API)
                uint8_t shared_secret[32];
                size_t secret_len;
                if (mbedtls_ecdh_calc_secret(&ecdh, &secret_len, shared_secret, sizeof(shared_secret), mbedtls_ctr_drbg_random, &ctr_drbg) == 0)
                {

                    // Truncate the 32-byte ECDH secret down to a 16-byte AES Key
                    memcpy(sharedEncryptionKey, shared_secret, 16);
                    isPaired = true;
                    Serial.println("Pairing SUCCESS! Shared AES key established and saved to RTC.");
                }
                else
                {
                    Serial.println("Pairing FAILED: Could not calculate shared secret.");
                }
            }
            else
            {
                Serial.println("Pairing FAILED: Could not read peer public key.");
            }
        }
        else
        {
            Serial.println("Pairing FAILED: Timeout waiting for receiver.");
        }

        // 8. Cleanup cryptography contexts
        mbedtls_ecdh_free(&ecdh);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);

        return isPaired;
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