#include <Arduino.h>
#include "AsyncLed.h"
#include "Button.h"

// 1. Create the LED instance (Using your ESP32-C6 pins and Common Anode)
AsyncLed myLed(21, 22, 23, COMMON_ANODE);
Button button(0, INPUT, true);

void setup()
{
  Serial.begin(9600);
  myLed.begin();
}

void loop()
{
  button.update();
  if (button.hasEvent())
  {
    switch (button.getEvent())
    {
    case SINGLE_PRESS:
      myLed.set(PULSE, 1, RED);
      break;
    case DOUBLE_PRESS:
      myLed.set(PULSE, 2, GREEN);
      break;
    case LONG_PRESS:
      myLed.set(PULSE, 3, WHITE);
      break;
    default:
      break;
    }
    delay(25);
  }
}