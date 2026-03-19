/**
 * @file StatusLED.h
 * @brief Minimal wrapper around FastLED for the controller's single RGB status LED.
 *
 * Encapsulates steady colors and finite or continuous blink patterns so the rest of the
 * firmware can express status transitions without interacting with FastLED timing details.
 *
 * @author Ralf Zühlsdorff
 * @date 2025
 *
 * @copyright
 * MIT License
 */

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
    int _pin;                  ///< GPIO used by the NeoPixel-compatible status LED.
    CRGB _leds[1];             ///< Backing FastLED buffer for the single on-board LED.
    CRGB _color;               ///< Target color for steady display or the active blink phase.
    int _pattern;              ///< Reserved for future pattern expansion; currently unused.
    bool _blinking;            ///< `true` while blink timing is active.
    int _interval;             ///< Off-time in milliseconds between blink pulses.
    int _ontime;               ///< On-time in milliseconds for each blink pulse.
    int _blinkTimes;           ///< Number of requested blinks, or `-1` for continuous blinking.
    int _currentBlinks;        ///< Number of completed on-phases in the current blink sequence.
    bool _lastWasOn;           ///< Tracks the previous phase to count finite blinks correctly.
    unsigned long _lastUpdate; ///< Timestamp used as the blink cycle origin.
    bool _initialized;         ///< Indicates whether FastLED has already been configured.
    bool _dirty;               ///< Prevents redundant hardware refreshes when visible state is unchanged.
};
