#include <Arduino.h>
#include <FastLED.h>



/**
 * @class StatusLED
 * @brief Controls a single RGB status LED including blink patterns.
 */
class StatusLED
{
public:
    /**
     * @brief Construct a status LED controller for the specified GPIO pin.
     * @param pin GPIO connected to the LED data input.
     */
    StatusLED(int pin);

    /**
     * @brief Initialize the FastLED backend and apply the current state.
     */
    void begin(); // Add begin method to initialize FastLED

    /**
     * @brief Refresh the LED output, including active blink timing.
     */
    void update();

    /**
     * @brief Set the LED color used for steady or blinking output.
     * @param color FastLED color value to display.
     */
    void setColor(CRGB color);

    /**
     * @brief Adjust the LED brightness.
     * @param val Brightness in the range 0..255.
     */
    void setBrightness(int val = 50);

    /**
     * @brief Enable or disable blinking with configurable timing.
     * @param blinking `true` to blink, `false` for steady output.
     * @param ontime Time in milliseconds that the LED stays on.
     * @param interval Time in milliseconds that the LED stays off.
     * @param times Number of blink cycles, or `-1` for continuous blinking.
     */
    void setBlinking(bool blinking, int ontime = 200, int interval = 500, int times = -1);

    /**
     * @brief Turn the LED off and cancel any active blink pattern.
     */
    void setOff();

private:
    int _pin;
    CRGB _leds[1]; // FastLED array with one LED
    CRGB _color;   // Current color to display
    int _pattern;
    bool _blinking;
    int _interval;
    int _ontime;
    int _blinkTimes;      // Number of times to blink (-1 = infinite)
    int _currentBlinks;   // Counter for current blink cycles
    bool _lastWasOn;      // Track previous LED state for blink counting
    unsigned long _lastUpdate;
    bool _initialized;
    bool _dirty;
};
