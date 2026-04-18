#pragma once
#include "PressEvent.h"
#include "BatteryStatus.h"

enum MessageType
{
    BUTTON_PRESS,
    BATTERY_STATUS,
    DISCOVERY_REQUEST,
    PAIRING_REQUEST, // NEW: Sender -> Receiver
    PAIRING_RESPONSE // NEW: Receiver -> Sender
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
    uint64_t sessionId = 0; // 0 = unpaired / plaintext pairing traffic; must match IV derivation when encrypted
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

        struct
        {
            size_t keyLen;
            uint8_t publicKey[65];
        } pairing;
    } data;
};

#define AES_IV_LENGTH 12
#define AES_TAG_LENGTH 16

// AES-GCM IV (12 bytes) on wire: sessionId (uint64_t LE) || counter (uint32_t LE). Must match decrypted Message.sessionId + Message.counter.

struct __attribute__((packed)) EncryptedPacket
{
    uint8_t iv[AES_IV_LENGTH];
    uint8_t ciphertext[sizeof(Message)];
    uint8_t tag[AES_TAG_LENGTH];
};