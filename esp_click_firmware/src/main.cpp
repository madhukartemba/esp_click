#include "BoardConfig.h"
#include "AsyncLed.h"
#include "BatteryMonitor.h"
#include "Button.h"
#include "ButtonManager.h"
#include "SleepManager.h"
#include "EspNowController.h"

AsyncLed myLed(
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

ButtonManager buttonManger;

void setup()
{
  Serial.begin(9600);
  SleepManager::getInstance().begin();

  buttonManger.registerButton(&button1);
  buttonManger.registerButton(&button2);
  buttonManger.registerButton(&button3);
  buttonManger.registerButton(&button4);

  buttonManger.begin();

  myLed.begin();

  batteryMonitor.setVoltageDividerRatio(BoardConfig::VOLTAGE_DIVIDER_RATIO);

  batteryMonitor.onBatteryStatusChange(
      [](BatteryStatus oldStatus, BatteryStatus newStatus)
      {
        switch (newStatus)
        {
        case NOT_CONNECTED:
          Serial.println("NOT_CONNECTED");
          myLed.set(LedMode::SOLID, 6, Color::RED);
          break;
        case CHARGING:
          Serial.println("CHARGING");
          myLed.set(LedMode::PULSE, Color::RED, LedSpeed::GLACIAL);
          break;
        case DISCHARGING:
          Serial.println("DISCHARGING");
          if (oldStatus != DISCHARGING)
          {
            myLed.set(LedMode::OFF);
          }
          break;
        case FULL_CHARGED:
          Serial.println("FULL_CHARGED");
          myLed.set(LedMode::SOLID, Color::GREEN);
          break;
        case CHARGE_FAULT:
          Serial.println("CHARGE_FAULT");
          myLed.set(LedMode::BLINK, 3, Color::RED);
          break;
        default:
          Serial.println("UNKNOWN");
          break;
        }
      });

  batteryMonitor.begin();

  EspNowController::getInstance().registerOnAfterSend(
      [](Message message, bool success)
      {
        if (message.type == MessageType::BUTTON_PRESS)
        {
          if (success)
          {
            myLed.set(LedMode::OFF);
          }
          else
          {
            myLed.set(LedMode::BLINK, 2, Color::RED);
          }
        }
      });

  EspNowController::getInstance().registerOnBeforeSend(
      [](Message message)
      {
        if (message.type == MessageType::BUTTON_PRESS)
        {
          if (message.data.buttonPress.event == SINGLE_PRESS)
            myLed.set(LedMode::SOLID, Color::WHITE);
          else if (message.data.buttonPress.event == DOUBLE_PRESS)
            myLed.set(LedMode::SOLID, Color::GREEN);
          else if (message.data.buttonPress.event == LONG_PRESS)
            myLed.set(LedMode::SOLID, Color::BLUE);
        }
      });

  EspNowController::getInstance().begin();
}

void loop() {}