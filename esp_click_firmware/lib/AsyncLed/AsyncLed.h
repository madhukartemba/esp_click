#include <Arduino.h>

enum Color
{
    RED,
    GREEN,
    BLUE,
    YELLOW,
    CYAN,
    MAGENTA,
    WHITE,
    BLACK
};

enum LedMode
{
    OFF,
    BLINK,
    PULSE,
    SOLID
};

enum LedSpeed
{
    GLACIAL = 2000, // 2 seconds (Great for slow, sleepy breathing pulses)
    SLOW = 1000,    // 1 second (Standard relaxed blink)
    MEDIUM = 500,   // 0.5 seconds (Standard alert)
    FAST = 250,     // 0.25 seconds (Urgent alert)
    RAPID = 100,    // 0.1 seconds (Very urgent, fast flickering)
    STROBE = 50     // 0.05 seconds (Aggressive strobe/flash effect)
};

struct LedCommand
{
    LedMode mode;
    Color color;
    LedSpeed speed = MEDIUM;
    int count = -1;
};

enum LedHardware
{
    COMMON_CATHODE,
    COMMON_ANODE
};

enum LedType
{
    SINGLE,
    RGB
};

class AsyncLed
{
private:
    int rPin;
    int gPin;
    int bPin;
    int pin;
    bool fadeBetweenCommands = true;
    LedCommand pendingCmd;
    LedHardware hardwareType;
    LedType ledType;
    QueueHandle_t commandQueue;

    static void ledTask(void *pvParameters)
    {
        AsyncLed *ledInstance = (AsyncLed *)pvParameters;
        ledInstance->run();
    }

    void getColorValues(Color c, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        switch (c)
        {
        case RED:
            r = 255;
            g = 0;
            b = 0;
            break;
        case GREEN:
            r = 0;
            g = 255;
            b = 0;
            break;
        case BLUE:
            r = 0;
            g = 0;
            b = 255;
            break;
        case YELLOW:
            r = 255;
            g = 255;
            b = 0;
            break;
        case CYAN:
            r = 0;
            g = 255;
            b = 255;
            break;
        case MAGENTA:
            r = 255;
            g = 0;
            b = 255;
            break;
        case WHITE:
            r = 255;
            g = 255;
            b = 255;
            break;
        case BLACK:
            r = 0;
            g = 0;
            b = 0;
            break;
        default:
            r = 0;
            g = 0;
            b = 0;
            break;
        }
    }

    // --- Helper 2: Write to hardware based on Anode/Cathode & Single/RGB ---
    void setHardwareColor(uint8_t r, uint8_t g, uint8_t b)
    {
        if (ledType == RGB)
        {
            // Apply Common Anode inversion if needed
            uint8_t outR = (hardwareType == COMMON_ANODE) ? (255 - r) : r;
            uint8_t outG = (hardwareType == COMMON_ANODE) ? (255 - g) : g;
            uint8_t outB = (hardwareType == COMMON_ANODE) ? (255 - b) : b;

            analogWrite(rPin, outR);
            analogWrite(gPin, outG);
            analogWrite(bPin, outB);
        }
        else
        {
            // For a single pin, extract brightness (max of r, g, b)
            uint8_t intensity = max({r, g, b});
            uint8_t outPin = (hardwareType == COMMON_ANODE) ? (255 - intensity) : intensity;

            analogWrite(pin, outPin);
        }
    }
    // --- The Main Task Loop ---
    void run()
    {
        LedCommand currentCmd = {OFF, BLACK, MEDIUM, -1};
        int currentCount = -1; // Tracks remaining cycles (-1 = infinite)

        // Animation state variables
        bool blinkState = false;
        float pulseLevel = 0.0f;
        bool pulseIncreasing = true;
        unsigned long lastActionTime = 0;

        // Transition state variables
        bool isFadingOut = false;
        float fadeOutLevel = 1.0f;
        bool isFadingIn = false;
        float fadeInLevel = 0.0f;

        while (true)
        {
            // --- 1. DETERMINE WAIT TIME & CHECK QUEUE ---
            LedCommand tempCmd;
            bool gotNewCmd = false;

            // If we are actively fading, don't pull from the queue. Let the fade finish.
            if (isFadingOut || isFadingIn)
            {
                vTaskDelay(pdMS_TO_TICKS(20)); // Fixed 20ms animation frame
            }
            else
            {
                // If the LED is resting (OFF or SOLID), we sleep indefinitely to save CPU.
                // If animating (BLINK or PULSE), we only wait 20ms.
                TickType_t waitTime = (currentCmd.mode == OFF || currentCmd.mode == SOLID) ? portMAX_DELAY : pdMS_TO_TICKS(20);

                // Check the queue. If no command arrives, this will timeout and act as our 20ms frame delay!
                if (xQueueReceive(commandQueue, &tempCmd, waitTime) == pdTRUE)
                {
                    gotNewCmd = true;
                }
            }

            // --- 2. SETUP TRANSITION ON NEW COMMAND ---
            if (gotNewCmd)
            {
                // Figure out EXACTLY how bright the LED is at this microsecond
                float currentBrightness = 0.0f;
                if (currentCmd.mode == SOLID)
                    currentBrightness = 1.0f;
                else if (currentCmd.mode == BLINK)
                    currentBrightness = blinkState ? 1.0f : 0.0f;
                else if (currentCmd.mode == PULSE)
                    currentBrightness = pulseLevel;

                // If the LED is visibly on, we must fade it out first
                if (fadeBetweenCommands && currentBrightness > 0.01f)
                {
                    pendingCmd = tempCmd;
                    isFadingOut = true;
                    fadeOutLevel = currentBrightness; // Start fading from EXACTLY where it is right now
                }
                else
                {
                    // If the LED is already black (or OFF), instantly swap to the new command
                    currentCmd = tempCmd;
                    currentCount = currentCmd.count; // Capture the count
                    blinkState = false;
                    pulseLevel = 0.0f;
                    pulseIncreasing = true;

                    if (currentCmd.mode == SOLID)
                    {
                        isFadingIn = true;
                        fadeInLevel = 0.0f;
                    }
                    else if (currentCmd.mode == OFF)
                    {
                        setHardwareColor(0, 0, 0);
                    }
                }
            }

            // Get target RGB values for the active color
            uint8_t targetR, targetG, targetB;
            getColorValues(currentCmd.color, targetR, targetG, targetB);
            unsigned long currentMillis = millis();

            // --- 3. EXECUTE ANIMATIONS & TRANSITIONS ---
            if (isFadingOut)
            {
                float step = 20.0f / (float)currentCmd.speed; // Calculate fade step based on desired fade-out duration
                fadeOutLevel -= step;

                if (fadeOutLevel <= 0.0f)
                {
                    // Fade out complete! Swap to the pending command
                    isFadingOut = false;
                    currentCmd = pendingCmd;
                    currentCount = currentCmd.count; // Capture the count from pending
                    blinkState = false;
                    pulseLevel = 0.0f;
                    pulseIncreasing = true;

                    if (currentCmd.mode == SOLID)
                    {
                        isFadingIn = true;
                        fadeInLevel = 0.0f;
                    }
                    else
                    {
                        setHardwareColor(0, 0, 0);
                    }
                }
                else
                {
                    setHardwareColor(targetR * fadeOutLevel, targetG * fadeOutLevel, targetB * fadeOutLevel);
                }
            }
            else if (isFadingIn)
            {
                float step = 20.0f / (float)currentCmd.speed;
                fadeInLevel += step;

                if (fadeInLevel >= 1.0f)
                {
                    fadeInLevel = 1.0f;
                    isFadingIn = false;
                }
                setHardwareColor(targetR * fadeInLevel, targetG * fadeInLevel, targetB * fadeInLevel);
            }
            else if (currentCmd.mode == SOLID)
            {
                setHardwareColor(targetR, targetG, targetB);
            }
            else if (currentCmd.mode == BLINK)
            {
                if (currentMillis - lastActionTime >= currentCmd.speed)
                {
                    lastActionTime = currentMillis;
                    blinkState = !blinkState;
                    setHardwareColor(blinkState ? targetR : 0, blinkState ? targetG : 0, blinkState ? targetB : 0);

                    // If it just turned OFF, a full blink cycle is complete!
                    if (!blinkState && currentCount > 0)
                    {
                        currentCount--;
                        if (currentCount == 0)
                        {
                            currentCmd.mode = OFF; // Finished!
                        }
                    }
                }
            }
            else if (currentCmd.mode == PULSE)
            {
                float step = 20.0f / (float)currentCmd.speed;

                // End of a pulse cycle (hit the bottom)
                if (pulseLevel <= 0.0f && !pulseIncreasing)
                {
                    if (currentCount > 0)
                    {
                        currentCount--;
                        if (currentCount == 0)
                        {
                            currentCmd.mode = OFF; // Finished!
                        }
                    }

                    // Only bounce back up if we didn't just turn off
                    if (currentCmd.mode != OFF)
                    {
                        pulseIncreasing = true;
                    }
                }

                if (currentCmd.mode == PULSE) // Still pulsing
                {
                    if (pulseIncreasing)
                    {
                        pulseLevel += step;
                        if (pulseLevel >= 1.0f)
                        {
                            pulseLevel = 1.0f;
                            pulseIncreasing = false;
                        }
                    }
                    else
                    {
                        pulseLevel -= step;
                        if (pulseLevel <= 0.0f)
                        {
                            pulseLevel = 0.0f;
                        }
                    }
                    setHardwareColor(targetR * pulseLevel, targetG * pulseLevel, targetB * pulseLevel);
                }
            }
        }
    }

public:
    AsyncLed(int rPin, int gPin, int bPin, LedHardware hardwareType) : rPin(rPin), gPin(gPin), bPin(bPin), hardwareType(hardwareType), ledType(RGB) {}
    AsyncLed(int pin, LedHardware hardwareType) : pin(pin), hardwareType(hardwareType), ledType(SINGLE) {}

    void begin()
    {
        commandQueue = xQueueCreate(10, sizeof(LedCommand));

        xTaskCreate(AsyncLed::ledTask, "LED Task", 2048, this, 1, NULL);
    }

    // Helper function for the main file to easily send commands for RGB
    // 1. Full RGB Control: Mode + Color + (Optional) Speed
    void set(LedMode mode, Color color, LedSpeed speed = MEDIUM)
    {
        LedCommand cmd = {mode, color, speed};
        xQueueSend(commandQueue, &cmd, 0);
    }

    // 2. Single LED Control: Mode + Speed
    // (Defaults to WHITE so the single pin gets 255/max brightness)
    void set(LedMode mode, LedSpeed speed)
    {
        LedCommand cmd = {mode, WHITE, speed};
        xQueueSend(commandQueue, &cmd, 0);
    }

    // 3. Simple Control: Just Mode
    void set(LedMode mode)
    {
        LedCommand cmd = {mode, WHITE, MEDIUM};
        xQueueSend(commandQueue, &cmd, 0);
    }

    // 4. Counted Control: Blink or Pulse X times!
    void set(LedMode mode, int count, Color color, LedSpeed speed = MEDIUM)
    {
        LedCommand cmd = {mode, color, speed, count};
        xQueueSend(commandQueue, &cmd, 0);
    }
};