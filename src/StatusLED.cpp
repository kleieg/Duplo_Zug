
/**
 * @file StatusLED.cpp
 * @brief Implementation of the single-pixel status LED state machine.
 *
 * Provides the FastLED-backed implementation for steady colors, finite blink bursts and
 * continuous blink patterns used by the controller UI.
 *
 * @author Ralf Zühlsdorff
 * @date 2025
 *
 * @copyright
 * MIT License
 */

#include "StatusLED.h"

/**
 * @brief Construct a status LED controller with default idle state.
 * @param pin GPIO connected to the RGB LED data line.
 */
StatusLED::StatusLED(int pin) : _pin(pin), _color(CRGB::Black), _pattern(0), _blinking(false), _interval(500), _ontime(200), _blinkTimes(-1), _lastUpdate(0), _initialized(false), _dirty(true)
{
    _leds[0] = CRGB::Black;
}

/**
 * @brief Initialize the StatusLED with FastLED
 * Call this method in your setup() function
 */
void StatusLED::begin()
{
    if (!_initialized)
    {
        // Initialize FastLED for the specific pin
        // Note: Pin must be known at compile time for FastLED templates
        // Common pins: 48 for ESP32-S3-DevKitC-1
        switch (_pin)
        {
        case 48:
            FastLED.addLeds<NEOPIXEL, 48>(_leds, 1);
            break;
        case 2:
            FastLED.addLeds<NEOPIXEL, 2>(_leds, 1);
            break;
        case 8:
            FastLED.addLeds<NEOPIXEL, 8>(_leds, 1);
            break;
        default:
            // Default to pin 48 if pin not specifically supported
            FastLED.addLeds<NEOPIXEL, 48>(_leds, 1);
            break;
        }
        FastLED.setBrightness(50);
        _initialized = true;
    }

    _leds[0] = _color;
    FastLED.show();
    _dirty = false;
}

/**
 * @brief Set the LED color
 * @param color CRGB color value to set
 */
void StatusLED::setColor(CRGB color)
{
    _color = color;
    _blinking = false;
    _dirty = true;
    if (!_blinking && _initialized)
    {
        _leds[0] = _color;
        FastLED.show();
        _dirty = false;
    }
}

/**
 * @brief Set the LED brightness
 * @param val Brightness value (0-255)
 */
void StatusLED::setBrightness(int val)
{
    if (_initialized)
    {
        FastLED.setBrightness(val);
        FastLED.show();
    }
}

/**
 * @brief Report whether a finite blink burst is still playing.
 * @return `true` while a burst started with `times > 0` has not finished yet.
 */
bool StatusLED::isBurstActive() const
{
    return _blinking && _blinkTimes > 0;
}

/**
 * @brief Switch the LED off and cancel any active blink pattern.
 */
void StatusLED::setOff()
{
    _color = CRGB::Black;
    _blinking = false;
    _dirty = true;
    if (_initialized)
    {
        _leds[0] = CRGB::Black;
        FastLED.show();
        _dirty = false;
    }
}

/**
 * @brief Update the LED state - call this regularly in your main loop
 * Handles blinking patterns if enabled
 */
void StatusLED::update()
{
    if (!_initialized)
    {
        return; // Not initialized yet
    }

    if (!_blinking)
    {
        // If not blinking, just ensure the LED shows the current color
        if (_leds[0] != _color)
        {
            _leds[0] = _color;
            _dirty = true;
        }

        if (_dirty)
        {
            FastLED.show();
            _dirty = false;
        }
        return;
    }

    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - _lastUpdate;
    unsigned long cycleTime = _ontime + _interval;

    // A finite burst ends after `_blinkTimes` complete on/off cycles, so the last blink is
    // shown for its full on-time.
    if (_blinkTimes > 0 && elapsed >= (unsigned long)_blinkTimes * cycleTime)
    {
        // Finite blinking is complete, stop blinking and turn LED off
        _blinking = false;
        _color = CRGB::Black;   // Also reset color to black
        _leds[0] = CRGB::Black; // Turn LED off after finite blinks
        _dirty = true;
        FastLED.show();
        _dirty = false;
        return;
    }

    // Position in the blink cycle decides between on and off phase
    bool currentlyOn = ((elapsed % cycleTime) < (unsigned long)_ontime);

    if (currentlyOn)
    {
        // LED should be on (showing the color)
        if (_leds[0] != _color)
        {
            _leds[0] = _color;
            _dirty = true;
        }
    }
    else
    {
        // LED should be off
        if (_leds[0] != CRGB::Black)
        {
            _leds[0] = CRGB::Black;
            _dirty = true;
        }
    }

    if (_dirty)
    {
        FastLED.show();
        _dirty = false;
    }
}

/**
 * @brief Enable or disable blinking mode
 * @param blinking True to enable blinking, false to disable
 * @param ontime Duration in milliseconds the LED is on during each blink cycle
 * @param interval Duration in milliseconds the LED is off during each blink cycle
 * @param times Number of times to blink (-1 = infinite, >0 = finite)
 */
void StatusLED::setBlinking(bool blinking, int ontime, int interval, int times)
{
    _blinking = blinking;
    _ontime = ontime;
    _interval = interval;
    _blinkTimes = times;
    _lastUpdate = millis(); // Blink cycle origin
    _dirty = true;

    if (!_blinking && _initialized)
    {
        // If blinking is disabled, set LED to solid color
        _leds[0] = _color;
        FastLED.show();
        _dirty = false;
    }
}
