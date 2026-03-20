#include <Arduino.h>
#include "AsyncLed.h"

// 1. Create the LED instance (Using your ESP32-C6 pins and Common Anode)
AsyncLed myLed(21, 22, 23, COMMON_ANODE);

// 2. Define Button Pins
const int buttonPins[4] = {0, 1, 2, 3};

// 3. Array to track button states (so we only trigger once per click)
bool lastButtonState[4] = {HIGH, HIGH, HIGH, HIGH};

void setup()
{
  Serial.begin(9600);
  Serial.println("Starting LED Test...");

  // Initialize the LED background task
  myLed.begin();

  // Initialize all 4 buttons with internal pullups
  for (int i = 0; i < 4; i++)
  {
    pinMode(buttonPins[i], INPUT);
  }

  // Start with a quick boot animation, then turn off
  myLed.set(SOLID, WHITE);
  myLed.set(OFF);
}

void loop()
{
  // Loop through all 4 buttons to check for presses
  for (int i = 0; i < 4; i++)
  {
    // Read the current state of the button
    bool currentState = digitalRead(buttonPins[i]);

    // Detect a "falling edge" (button went from unpressed to pressed)
    // Since they are PULLUP, HIGH = unpressed, LOW = pressed
    if (lastButtonState[i] == HIGH && currentState == LOW)
    {
      Serial.printf("Button %d pressed!\n", i);

      // Send commands based on which button was clicked
      if (i == 0)
      {
        // Button 0: Solid Red (Will trigger the fade-out if pressed again!)
        myLed.set(SOLID, RED);
      }
      else if (i == 1)
      {
        // Button 1: Pulse Green Medium
        myLed.set(PULSE, GREEN, FAST);
      }
      else if (i == 2)
      {
        // Button 2: Blink Blue Fast
        myLed.set(BLINK, BLUE, SLOW);
      }
      else if (i == 3)
      {
        // Button 3: Turn Off
        myLed.set(OFF);
      }
    }

    // Save the state for the next loop
    lastButtonState[i] = currentState;
  }

  // A tiny delay to debounce the mechanical buttons
  delay(20);
}