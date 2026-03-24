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
    COMMON_ANODE
);

BatteryMonitor batteryMonitor(
    BoardConfig::BATTERY_PIN, 
    BoardConfig::POWER_GOOD_PIN,
    BoardConfig::CHARGE_DETECT_PIN
);

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
  batteryMonitor.begin();

  EspNowController::getInstance().registerOnAfterSend(
      [](Message message, bool success)
      {
        if (message.type == MessageType::BUTTON_PRESS)
        {
          if(success) {
            myLed.set(LedMode::BLINK, 1, Color::GREEN);
          } else {
            myLed.set(LedMode::BLINK, 2, Color::RED);
          }
        }
      });

  EspNowController::getInstance().registerOnBeforeSend(
      [](Message message)
      {
        if (message.type == MessageType::BUTTON_PRESS)
        {
          myLed.set(LedMode::SOLID, 1, Color::WHITE);
        }
      });

  EspNowController::getInstance().begin();
}

void loop() {}