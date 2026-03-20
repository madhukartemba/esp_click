#include <Arduino.h>
#include "AsyncLed.h"
#include "Button.h"
#include "ButtonManager.h"

AsyncLed myLed(21, 22, 23, COMMON_ANODE);

Button button1(0, INPUT, true);
Button button2(1, INPUT, true);
Button button3(2, INPUT, true);
Button button4(3, INPUT, true);

ButtonManager buttonManger;

QueueHandle_t buttonQueue;
ButtonEvent event;

void setup()
{
  Serial.begin(9600);
  myLed.begin();

  buttonManger.registerButton(&button1);
  buttonManger.registerButton(&button2);
  buttonManger.registerButton(&button3);
  buttonManger.registerButton(&button4);

  buttonManger.begin();
  buttonQueue = buttonManger.getQueue();
}

void loop()
{
  if (xQueueReceive(buttonQueue, &event, portMAX_DELAY))
  {
    switch (event.event)
    {
    case SINGLE_PRESS:
      Serial.printf("Button %d Single Press\n", event.id);
      myLed.set(PULSE, 1, WHITE);
      break;
    case DOUBLE_PRESS:
      Serial.printf("Button %d Double Press\n", event.id);
      myLed.set(PULSE, 1, GREEN);
      break;
    case LONG_PRESS:
      Serial.printf("Button %d Long Press\n", event.id);
      myLed.set(PULSE, 1, BLUE);
      break;
    default:
      break;
    }
  }
}