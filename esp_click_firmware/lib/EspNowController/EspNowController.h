#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <Arduino.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_random.h>
#include <cstring>
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
RTC_DATA_ATTR uint64_t rtcSessionId = 0;             // Current crypto session; 0 when unpaired

namespace SessionIdHistory
{
    constexpr size_t kMax = 8;
    uint64_t g_ids[kMax];
    size_t g_len = 0;

    bool contains(uint64_t id)
    {
        for (size_t i = 0; i < g_len; i++)
        {
            if (g_ids[i] == id)
                return true;
        }
        return false;
    }

    void push(uint64_t id)
    {
        if (g_len < kMax)
        {
            g_ids[g_len++] = id;
            return;
        }
        memmove(g_ids, g_ids + 1, (kMax - 1) * sizeof(uint64_t));
        g_ids[kMax - 1] = id;
    }

    void clear()
    {
        g_len = 0;
        memset(g_ids, 0, sizeof(g_ids));
    }
} // namespace SessionIdHistory

// NVS: AES key + paired flag only; saved once when pairing succeeds
namespace PairingNvs
{
    static constexpr const char *kNs = "enow";
    static constexpr const char *kBlob = "p";
    static constexpr uint32_t kMagic = 0x4E574B31;
    static constexpr uint16_t kVersion = 1;

    struct __attribute__((packed)) Blob
    {
        uint32_t magic;
        uint16_t version;
        uint8_t sharedEncryptionKey[16];
        uint8_t isPaired;
    };
}

class EspNowController : public BaseController
{
private:
    static EspNowController *s_instance;

    volatile bool appAckReceived = false;
    volatile uint32_t expectedMessageId = 0;
    volatile uint8_t currentSweepChannel = 0;
    uint32_t messageCounter = 0;

    // Pairing Retry Count
    uint8_t pairingRetryCount = 3;

    volatile bool pairingResponseReceived = false;
    uint8_t peerPublicKey[65];
    size_t peerPublicKeyLen = 0;

    unsigned long ackWaitTimeout = 100;

    std::function<void(Message)> onBeforeSend = nullptr;
    std::function<void(Message, bool)> onAfterSend = nullptr;
    std::function<void()> onPairingInit = nullptr;
    std::function<void(bool)> onPairingComplete = nullptr;

    uint8_t pairingButtonId = -1;

    EspNowController() {}
    ~EspNowController() {}

    void initSessionIdForBoot()
    {
        if (!isPaired)
        {
            rtcSessionId = 0;
            return;
        }

        for (int attempt = 0; attempt < 32; attempt++)
        {
            uint64_t candidate = 0;
            esp_fill_random(&candidate, sizeof(candidate));
            if (candidate == 0 || SessionIdHistory::contains(candidate))
                continue;
            rtcSessionId = candidate;
            SessionIdHistory::push(rtcSessionId);
            return;
        }

        uint64_t fallback = (uint64_t)esp_random() | ((uint64_t)esp_random() << 32);
        if (fallback == 0)
            fallback = 1;
        while (SessionIdHistory::contains(fallback))
            ++fallback;
        rtcSessionId = fallback;
        SessionIdHistory::push(rtcSessionId);
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
        Serial.println("Received ESP-NOW packet!");

        // First check if it's an ACK for a message we sent
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
                if (message.type == MessageType::BUTTON_PRESS &&
                    message.data.buttonPress.buttonId == pairingButtonId)
                {
                    if (message.data.buttonPress.event == LONG_PRESS)
                    {
                        initiatePairing();
                    }
                }
                else
                {
                    if (onBeforeSend)
                        onBeforeSend(message);

                    bool success = sendMessage(&message);

                    if (onAfterSend)
                        onAfterSend(message, success);
                }

                SleepManager::getInstance().allowSleep(this->taskId);
            }
        }
    }

    // ==========================================
    // AES-GCM Helpers
    // ==========================================
    // 12-byte GCM nonce: sessionId (8, LE) + counter (4, LE). Unique per (sessionId, counter) for a given key.
    void generate_iv(uint8_t *iv, uint32_t counter, uint64_t sessionId)
    {
        memcpy(&iv[0], &sessionId, sizeof(sessionId));
        memcpy(&iv[8], &counter, sizeof(counter));
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

        generate_iv(out_packet->iv, plaintext_msg->counter, plaintext_msg->sessionId);

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

        message->sessionId = rtcSessionId;

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

        message->counter = ++messageCounter;

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

        Message pingMsg{};
        pingMsg.deviceId = 0;
        pingMsg.type = DISCOVERY_REQUEST;
        pingMsg.sessionId = rtcSessionId;

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

    void loadPairingFromPrefs()
    {
        Preferences prefs;
        if (!prefs.begin(PairingNvs::kNs, true))
        {
            Serial.println("Pairing NVS: failed to open namespace for read");
            return;
        }

        PairingNvs::Blob blob{};
        const size_t n = prefs.getBytes(PairingNvs::kBlob, &blob, sizeof(blob));
        prefs.end();

        if (n != sizeof(blob) || blob.magic != PairingNvs::kMagic || blob.version != PairingNvs::kVersion)
        {
            return;
        }

        memcpy(sharedEncryptionKey, blob.sharedEncryptionKey, sizeof(sharedEncryptionKey));
        isPaired = blob.isPaired != 0;

        Serial.println("Pairing NVS: restored key");
    }

    void savePairingToPrefs()
    {
        PairingNvs::Blob blob{};
        blob.magic = PairingNvs::kMagic;
        blob.version = PairingNvs::kVersion;
        memcpy(blob.sharedEncryptionKey, sharedEncryptionKey, sizeof(blob.sharedEncryptionKey));
        blob.isPaired = isPaired ? 1 : 0;

        Preferences prefs;
        if (!prefs.begin(PairingNvs::kNs, false))
        {
            Serial.println("Pairing NVS: failed to open namespace for write");
            return;
        }

        const size_t w = prefs.putBytes(PairingNvs::kBlob, &blob, sizeof(blob));
        prefs.end();

        if (w != sizeof(blob))
        {
            Serial.println("Pairing NVS: write failed");
        }
    }

    void clearPairingFromPrefs()
    {
        Preferences prefs;
        if (!prefs.begin(PairingNvs::kNs, false))
        {
            Serial.println("Pairing NVS: failed to open namespace for clear");
            return;
        }
        prefs.clear();
        prefs.end();
        Serial.println("Pairing NVS: cleared");
    }

    void wipePairingConfig()
    {
        clearPairingFromPrefs();
        memset(sharedEncryptionKey, 0, sizeof(sharedEncryptionKey));
        isPaired = false;
        rtcSessionId = 0;
        SessionIdHistory::clear();

        lastSendNode = LastSendNode{};
        bestFoundNode = BestNode{{0}, 0, -127, false};

        messageCounter = 0;
    }

    bool pairDevice()
    {
        mbedtls_ecdh_context ecdh;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_entropy_context entropy;

        mbedtls_ecdh_init(&ecdh);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_entropy_init(&entropy);

        // A neat C++ Lambda to handle memory cleanup before we return
        auto cleanup = [&]()
        {
            mbedtls_ecdh_free(&ecdh);
            mbedtls_ctr_drbg_free(&ctr_drbg);
            mbedtls_entropy_free(&entropy);
        };

        int ret = 0;

        // 1. Seed the random number generator
        const char *pers = "esp_click_pairing";
        ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers));
        if (ret != 0)
        {
            Serial.printf("Failed to seed RNG. Error: -0x%04X\n", -ret);
            cleanup();
            return false;
        }

        // 2. Setup the ECDH context
        ret = mbedtls_ecdh_setup(&ecdh, MBEDTLS_ECP_DP_CURVE25519);
        if (ret != 0)
        {
            Serial.printf("Failed to setup ECDH context. Error: -0x%04X\n", -ret);
            cleanup();
            return false;
        }

        // 3. Prepare the Pairing Request Message (plaintext; sessionId must stay 0)
        Message pairingMsg{};
        pairingMsg.type = PAIRING_REQUEST;
        pairingMsg.counter = ++messageCounter;
        pairingMsg.sessionId = 0;

        // 4. Generate Key Pair
        size_t olen = 0;
        ret = mbedtls_ecdh_make_public(&ecdh, &olen, pairingMsg.data.pairing.publicKey, 65, mbedtls_ctr_drbg_random, &ctr_drbg);
        if (ret != 0)
        {
            Serial.printf("Failed to generate public key. Error: -0x%04X\n", -ret);
            cleanup();
            return false;
        }
        pairingMsg.data.pairing.keyLen = olen;

        pairingResponseReceived = false;

        // 5. Broadcast the public key across all channels and wait
        uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_now_peer_info_t bcPeer = {};
        memcpy(bcPeer.peer_addr, broadcastMAC, 6);
        bcPeer.channel = 0;
        bcPeer.encrypt = false;
        bcPeer.ifidx = WIFI_IF_STA;
        esp_now_add_peer(&bcPeer);

        Serial.println("Broadcasting Public Key. Sweeping channels...");
        for (int i = 1; i <= 13; i++)
        {
            esp_wifi_set_channel(i, WIFI_SECOND_CHAN_NONE);
            esp_now_send(broadcastMAC, (uint8_t *)&pairingMsg, sizeof(Message));

            // Wait long enough ON THIS CHANNEL for the receiver to compute ECDH (~260ms) and reply
            unsigned long startWait = millis();
            while (!pairingResponseReceived && millis() - startWait < 500)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            if (pairingResponseReceived)
            {
                Serial.printf("Receiver found and responded on channel %d!\n", i);
                break; // Stop sweeping, we got our response!
            }
        }
        esp_now_del_peer(broadcastMAC);

        // 7. Compute Shared Secret
        if (pairingResponseReceived)
        {
            ret = mbedtls_ecdh_read_public(&ecdh, peerPublicKey, peerPublicKeyLen);
            if (ret == 0)
            {
                uint8_t shared_secret[32];
                size_t secret_len;
                ret = mbedtls_ecdh_calc_secret(&ecdh, &secret_len, shared_secret, sizeof(shared_secret), mbedtls_ctr_drbg_random, &ctr_drbg);
                if (ret == 0)
                {
                    memcpy(sharedEncryptionKey, shared_secret, 16);
                    isPaired = true;
                    savePairingToPrefs();
                    initSessionIdForBoot();
                    Serial.println("Pairing SUCCESS! Shared AES key established and saved to NVS.");
                }
                else
                {
                    Serial.printf("Pairing FAILED: Could not calculate shared secret. Error: -0x%04X\n", -ret);
                }
            }
            else
            {
                Serial.printf("Pairing FAILED: Could not read peer public key. Error: -0x%04X\n", -ret);
            }
        }
        else
        {
            Serial.println("Pairing FAILED: Timeout waiting for receiver.");
        }

        // 8. Cleanup cryptography contexts safely and return
        cleanup();
        return isPaired;
    }

public:
    static EspNowController &getInstance()
    {
        static EspNowController instance;
        return instance;
    }

    EspNowController(const EspNowController &) = delete;
    EspNowController &operator=(const EspNowController &) = delete;

    void begin(uint8_t pairingButtonId)
    {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();

        s_instance = this;

        this->pairingButtonId = pairingButtonId;

        loadPairingFromPrefs();
        initSessionIdForBoot();

        startControllerTask("ESP-NOW Task", 16384, 1, 10);
    }

    bool initiatePairing()
    {
        wipePairingConfig();

        Serial.println("Initiating ECDH Key Exchange...");
        if (onPairingInit)
            onPairingInit();

        for (uint8_t attempt = 1; attempt <= pairingRetryCount + 1; attempt++)
        {
            Serial.printf("Pairing Attempt %d of %d\n", attempt, pairingRetryCount + 1);
            if (pairDevice())
            {
                break;
            }
            else if (attempt <= pairingRetryCount)
            {
                Serial.println("Pairing attempt failed. Retrying...");
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }

        if (onPairingComplete)
            onPairingComplete(isPaired);

        return isPaired;
    }

    bool isDevicePaired() { return isPaired; }

    void setPairingRetryCount(uint8_t count) { pairingRetryCount = count; }
    uint8_t getPairingRetryCount() { return pairingRetryCount; }

    void registerOnBeforeSend(std::function<void(Message)> callback) { this->onBeforeSend = callback; }
    void registerOnAfterSend(std::function<void(Message, bool)> callback) { this->onAfterSend = callback; }
    void registerOnPairingInit(std::function<void()> callback) { this->onPairingInit = callback; }
    void registerOnPairingComplete(std::function<void(bool)> callback) { this->onPairingComplete = callback; }
};

EspNowController *EspNowController::s_instance = nullptr;