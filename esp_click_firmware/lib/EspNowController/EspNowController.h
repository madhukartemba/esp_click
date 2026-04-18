#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <Arduino.h>
#include <esp_now.h>
#include "BaseController.h"
#include "Message.h" // Ensure this includes the EncryptedPacket struct

// Cryptography
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/gcm.h> // NEW: For AES-GCM Encryption

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
    int rssi;
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
    volatile uint8_t currentSweepChannel = 0;
    uint32_t messageCounter = 0;

    volatile bool pairingResponseReceived = false;
    uint8_t peerPublicKey[65];
    size_t peerPublicKeyLen = 0;

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
        else if (len == sizeof(Message))
        {
            Message *msg = (Message *)incomingData;
            if (msg->type == PAIRING_RESPONSE)
            {
                memcpy(peerPublicKey, msg->data.pairing.publicKey, msg->data.pairing.keyLen);
                peerPublicKeyLen = msg->data.pairing.keyLen;

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
                    onBeforeSend(message);

                bool success = sendMessage(&message);

                if (onAfterSend)
                    onAfterSend(message, success);

                SleepManager::getInstance().allowSleep(this->taskId);
            }
        }
    }

    // ==========================================
    // NEW: AES-GCM Helpers
    // ==========================================
    void generate_iv(uint8_t *iv, uint32_t counter)
    {
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);

        // IV = Sender MAC (6 bytes) + Message Counter (4 bytes) + Padding (2 bytes)
        memcpy(&iv[0], mac, 6);
        memcpy(&iv[6], &counter, sizeof(counter));
        iv[10] = 0x00;
        iv[11] = 0x00;
    }

    bool encrypt_message(const Message *plaintext_msg, EncryptedPacket *out_packet)
    {
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);

        if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, sharedEncryptionKey, 128) != 0)
        {
            Serial.println("Failed to set AES encryption key");
            mbedtls_gcm_free(&gcm);
            return false;
        }

        generate_iv(out_packet->iv, plaintext_msg->counter);

        int ret = mbedtls_gcm_crypt_and_tag(
            &gcm,
            MBEDTLS_GCM_ENCRYPT,
            sizeof(Message),
            out_packet->iv, AES_IV_LENGTH,
            NULL, 0, // No Additional Authenticated Data (AAD) needed
            (const unsigned char *)plaintext_msg,
            out_packet->ciphertext,
            AES_TAG_LENGTH,
            out_packet->tag);

        mbedtls_gcm_free(&gcm);

        if (ret != 0)
        {
            Serial.printf("Encryption failed with error: -0x%04X\n", -ret);
            return false;
        }

        return true;
    }

    bool sendMessageToKnownNode(Message *message)
    {

        // STRICT GUARD: Do not allow unencrypted messages to known nodes if we are not paired
        if (!isPaired)
        {
            Serial.println("Cannot broadcast discovery: Device is not paired.");
            return false;
        }

        Serial.printf("Targeting known node on channel %d\n", lastSendNode.lastChannel);

        esp_wifi_set_channel(lastSendNode.lastChannel, WIFI_SECOND_CHAN_NONE);

        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, lastSendNode.targetMAC, 6);
        peerInfo.channel = lastSendNode.lastChannel;
        peerInfo.ifidx = WIFI_IF_STA;
        peerInfo.encrypt = false; // We use application-layer encryption, not hardware

        if (esp_now_add_peer(&peerInfo) != ESP_OK)
        {
            Serial.println("Failed to add peer");
            return false;
        }

        expectedMessageId = message->counter;
        appAckReceived = false;

        EncryptedPacket encPacket;
        if (encrypt_message(message, &encPacket))
        {
            esp_now_send(lastSendNode.targetMAC, (uint8_t *)&encPacket, sizeof(EncryptedPacket));
        }
        else
        {
            Serial.println("Aborting send due to encryption failure.");
            esp_now_del_peer(lastSendNode.targetMAC);
            return false;
        }

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
        // STRICT GUARD: Do not allow unencrypted discovery broadcasts
        if (!isPaired)
        {
            Serial.println("Cannot broadcast discovery: Device is not paired.");
            return false;
        }

        Serial.println("Broadcasting ENCRYPTED DISCOVERY ping (sweeping channels)...");
        uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

        bestFoundNode = {{0}, 0, -127, false};
        lastSendNode.isNodeKnown = false;

        esp_now_peer_info_t bcPeer = {};
        memcpy(bcPeer.peer_addr, broadcastMAC, 6);
        bcPeer.channel = 0;
        bcPeer.encrypt = false;
        bcPeer.ifidx = WIFI_IF_STA;

        esp_now_add_peer(&bcPeer);

        Message pingMsg;
        pingMsg.deviceId = 0;
        pingMsg.type = DISCOVERY_REQUEST;

        for (int i = 1; i <= 13; i++)
        {
            esp_wifi_set_channel(i, WIFI_SECOND_CHAN_NONE);
            currentSweepChannel = i;

            pingMsg.counter = ++messageCounter;
            expectedMessageId = pingMsg.counter;

            EncryptedPacket encPing;
            if (encrypt_message(&pingMsg, &encPing))
            {
                esp_now_send(broadcastMAC, (uint8_t *)&encPing, sizeof(EncryptedPacket));
            }
            else
            {
                Serial.println("Failed to encrypt discovery ping. Skipping transmission.");
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }

        esp_now_del_peer(broadcastMAC);

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

        if (!isPaired)
        {
            Serial.println("Device is not paired. Cannot send messages to known nodes without pairing.");
            return false;
        }

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
            if (!isPaired && !initiatePairing())
                return false;
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
            vTaskDelay(pdMS_TO_TICKS(50));
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

    void registerOnBeforeSend(std::function<void(Message)> callback) { this->onBeforeSend = callback; }
    void registerOnAfterSend(std::function<void(Message, bool)> callback) { this->onAfterSend = callback; }
};

EspNowController *EspNowController::s_instance = nullptr;