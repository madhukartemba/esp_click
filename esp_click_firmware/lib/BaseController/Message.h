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