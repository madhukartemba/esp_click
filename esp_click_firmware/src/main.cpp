#include "BoardConfig.h"
#include "AsyncLed.h"
#include "BatteryMonitor.h"
#include "Button.h"
#include "ButtonManager.h"
#include "SleepManager.h"
#include "EspNowController.h"

AsyncLed statusLed(
    BoardConfig::LED_PIN_R,
    BoardConfig::LED_PIN_G,
    BoardConfig::LED_PIN_B,
    COMMON_ANODE);

BatteryMonitor batteryMonitor(
    BoardConfig::BATTERY_PIN,
    BoardConfig::POWER_GOOD_PIN,
    BoardConfig::CHARGE_DETECT_PIN);

Button button1(BoardConfig::BTN1_PIN, INPUT, true);
Button button2(BoardConfig::BTN2_PIN, INPUT, true);
Button button3(BoardConfig::BTN3_PIN, INPUT, true);
Button button4(BoardConfig::BTN4_PIN, INPUT, true);
Button pairingButton(BoardConfig::PAIRING_MODE_PIN, INPUT_PULLUP, true);

ButtonManager buttonManager;

void setup()
{
  Serial.begin(9600);
  SleepManager::getInstance().begin();

  buttonManager.registerButton(&button1);
  buttonManager.registerButton(&button2);
  buttonManager.registerButton(&button3);
  buttonManager.registerButton(&button4);
  buttonManager.registerButton(&pairingButton, false);

  buttonManager.registerMessageSink(&EspNowController::getInstance());

  buttonManager.begin();

  statusLed.begin();

  batteryMonitor.setVoltageDividerRatio(BoardConfig::VOLTAGE_DIVIDER_RATIO);

  batteryMonitor.onBatteryStatusChange(
      [](BatteryStatus oldStatus, BatteryStatus newStatus)
      {
        switch (newStatus)
        {
        case NOT_CONNECTED:
          Serial.println("NOT_CONNECTED");
          statusLed.set(LedMode::BLINK, 6, Color::RED);
          break;
        case CHARGING:
          Serial.println("CHARGING");
          statusLed.set(LedMode::PULSE, Color::RED, LedSpeed::GLACIAL);
          break;
        case DISCHARGING:
          Serial.println("DISCHARGING");
          if (oldStatus != DISCHARGING)
          {
            statusLed.set(LedMode::OFF);
          }
          break;
        case FULL_CHARGED:
          Serial.println("FULL_CHARGED");
          statusLed.set(LedMode::SOLID, Color::GREEN);
          break;
        case CHARGE_FAULT:
          Serial.println("CHARGE_FAULT");
          statusLed.set(LedMode::BLINK, 3, Color::RED);
          break;
        default:
          Serial.println("UNKNOWN");
          break;
        }
      });

  statusLed.registerOnAnimEnd(
      [](LedCommand cmd)
      {
        if (batteryMonitor.getBatteryStatus() == BatteryStatus::CHARGING)
        {
          statusLed.set(LedMode::PULSE, Color::RED, LedSpeed::GLACIAL);
        }
        else if (batteryMonitor.getBatteryStatus() == BatteryStatus::FULL_CHARGED)
        {
          statusLed.set(LedMode::SOLID, Color::GREEN);
        }
      });

  batteryMonitor.begin();

  EspNowController::getInstance().registerOnBeforeSend(
      [](Message message)
      {
        if (message.type == MessageType::BUTTON_PRESS)
        {
          if (batteryMonitor.getBatteryLevel() <= BoardConfig::LOW_BATTERY_THRESHOLD)
            statusLed.set(LedMode::SOLID, Color::RED);
          else if (message.data.buttonPress.event == SINGLE_PRESS)
            statusLed.set(LedMode::SOLID, Color::WHITE);
          else if (message.data.buttonPress.event == DOUBLE_PRESS)
            statusLed.set(LedMode::SOLID, Color::GREEN);
          else if (message.data.buttonPress.event == LONG_PRESS)
            statusLed.set(LedMode::SOLID, Color::BLUE);
        }
      });

  EspNowController::getInstance().registerOnAfterSend(
      [](Message message, bool success)
      {
        if (message.type == MessageType::BUTTON_PRESS)
        {
          if (success)
          {
            statusLed.set(LedMode::OFF);
          }
          else
          {
            if (EspNowController::getInstance().isDevicePaired())
            {
              statusLed.set(LedMode::BLINK, 2, Color::RED);
            }
            else
            {
              statusLed.set(LedMode::BLINK, 5, Color::RED);
            }
          }
        }
      });

  EspNowController::getInstance().registerOnPairingInit(
      []()
      {
        statusLed.set(LedMode::PULSE, Color::MAGENTA);
      });

  EspNowController::getInstance().registerOnPairingComplete(
      [](bool success)
      {
        if (success)
        {
          statusLed.set(LedMode::BLINK, 2, Color::GREEN);
        }
        else
        {
          statusLed.set(LedMode::BLINK, 4, Color::RED);
        }
      });

  EspNowController::getInstance().begin(BoardConfig::PAIRING_MODE_PIN);
}

void loop() {}