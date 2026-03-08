
/**
 * @file ADCButton.cpp
 * @brief Implementation for ADC-based button reading with averaging and debouncing
 *
 * This file implements the ADCButtons class for reading multiple buttons
 * connected via voltage divider to a single ADC pin. Features include:
 * - Dynamic button array management with automatic resizing
 * - Circular buffer ADC sampling with configurable averaging
 * - Debouncing and signal filtering
 * - Automatic min/max calibration tracking
 * - Input validation and error handling
 *
 * @author Ralf Zühlsdorff
 * @date 2025
 * @copyright Copyright (c) 2025 Ralf Zühlsdorff
 *
 * @license MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ADCButton.h"

#define DEBUG // Enable debug logging
#include "debug.h"

/**
 * @brief Constructor - Initialize ADC button reader with comprehensive setup
 *
 * Initializes all member variables, validates input parameters, configures the ADC pin,
 * and allocates memory for the sampling buffer and button array. Uses RAII pattern
 * for automatic resource management.
 *
 * @param adc_pin ADC pin number to read from
 * @param samples Number of samples for averaging (validated: >0, default: 10)
 * @param margin Tolerance margin for button detection (validated: >=0, default: 50)
 * @param resolution ADC resolution in bits (validated: 8-16, default: 12)
 * @param debounceTime Debounce delay in milliseconds (validated: >=0, default: 25)
 */
ADCButtons::ADCButtons(int adc_pin, int samples, int margin, int resolution, int debounceTime)
    : adcPin(adc_pin),
      samples(samples),
      margin(margin),
      resolution(resolution),
      debounceTime(debounceTime),
      min((1 << resolution) - 1), // Set min to max possible ADC value
      max(0),
      buttonCount(0),
      buttonCapacity(0),
      callback(nullptr)  // Initialize callback to nullptr
{
    // Input validation
    if (samples <= 0)
        this->samples = 10;
    if (margin < 0)
        this->margin = 50;
    if (resolution < 8 || resolution > 16)
        this->resolution = 12;
    if (adc_pin < 0)
        this->adcPin = 7;
    if (debounceTime < 0)
        this->debounceTime = 25;

    // Configure ADC pin for button reading
    analogReadResolution(this->resolution);
    pinMode(this->adcPin, INPUT);

    // Allocate memory for the values array
    values = new int[samples](); // () initializes all elements to 0

    // Initialize all values to 0
    for (int i = 0; i < samples; i++)
    {
        values[i] = 0;
    }

    // Start with capacity for 5 buttons (can grow dynamically)
    buttonCapacity = 5;
    buttons = new ButtonState[buttonCapacity]();

    for (int i = 0; i < buttonCapacity; i++)
    {
        buttons[i].adcValue = -1; // Mark all slots as unused until configured
        buttons[i].adcMin = -1;
        buttons[i].adcMax = -1;
        buttons[i].pressed = false;
    }
}

/**
 * @brief Destructor - Clean up dynamically allocated memory
 *
 * Properly deallocates all dynamic memory to prevent memory leaks.
 * Sets pointers to nullptr for safety.
 */
ADCButtons::~ADCButtons()
{
    // Free the dynamically allocated memory
    delete[] values;
    values = nullptr;

    delete[] buttons;
    buttons = nullptr;
}

/**
 * @brief Register a callback function for button state changes
 * 
 * Sets up a callback function that will be invoked whenever any button
 * changes state (pressed to released or released to pressed).
 * 
 * @param cb Callback function pointer, or nullptr to disable callbacks
 *           Function signature: void callback(int button_no, bool pressed)
 */
void ADCButtons::registerButtonCallback(buttonCallback cb)
{
    callback = cb;
}

/**
 * @brief Register a new button with its expected ADC value
 *
 * Adds a button to the dynamic array at the specified button number.
 * Automatically resizes the array if needed to accommodate the button number.
 * Validates both the button number and ADC value.
 *
 * @param button_no Button number (0-based index) to assign to this button
 * @param adcValue The ADC value expected when this button is pressed
 *                 Must be within range [0, 2^resolution)
 */
void ADCButtons::addButton(int button_no, int adcValue)
{
    // Input validation
    if (button_no < 0)
    {
        return; // Invalid button number
    }
    
    if (adcValue < 0 || adcValue >= (1 << resolution))
    {
        return; // Invalid ADC value for current resolution
    }

    // Check if we need to resize the array to accommodate the button number
    int requiredCapacity = button_no + 1;
    if (requiredCapacity > buttonCapacity)
    {
        // Expand capacity to at least the required size, or double current capacity, whichever is larger
        int newCapacity = (requiredCapacity > buttonCapacity * 2) ? requiredCapacity : buttonCapacity * 2;
        ButtonState *newButtons = new ButtonState[newCapacity]();

        // Copy existing buttons
        for (int i = 0; i < buttonCount; i++)
        {
            newButtons[i] = buttons[i];
        }

        // Initialize new slots to default values
        for (int i = buttonCount; i < newCapacity; i++)
        {
            newButtons[i].adcValue = -1;  // Mark as unused
            newButtons[i].pressed = false;
        }

        // Delete old array and update pointer
        delete[] buttons;
        buttons = newButtons;
        buttonCapacity = newCapacity;
    }

    addButtonRange(button_no, adcValue - margin, adcValue + margin);
}

void ADCButtons::addButtonRange(int button_no, int adcMin, int adcMax)
{
    if (button_no < 0)
    {
        return;
    }

    if (adcMin > adcMax)
    {
        const int temp = adcMin;
        adcMin = adcMax;
        adcMax = temp;
    }

    if (adcMin < 0)
    {
        adcMin = 0;
    }

    const int maxAdcValue = (1 << resolution) - 1;
    if (adcMax > maxAdcValue)
    {
        adcMax = maxAdcValue;
    }

    const int requiredCapacity = button_no + 1;
    if (requiredCapacity > buttonCapacity)
    {
        const int newCapacity = (requiredCapacity > buttonCapacity * 2) ? requiredCapacity : buttonCapacity * 2;
        ButtonState *newButtons = new ButtonState[newCapacity]();

        for (int i = 0; i < buttonCount; i++)
        {
            newButtons[i] = buttons[i];
        }

        for (int i = buttonCount; i < newCapacity; i++)
        {
            newButtons[i].adcValue = -1;
            newButtons[i].adcMin = -1;
            newButtons[i].adcMax = -1;
            newButtons[i].pressed = false;
        }

        delete[] buttons;
        buttons = newButtons;
        buttonCapacity = newCapacity;
    }

    buttons[button_no].adcMin = adcMin;
    buttons[button_no].adcMax = adcMax;
    buttons[button_no].adcValue = (adcMin + adcMax) / 2;
    buttons[button_no].pressed = false;

    if (button_no >= buttonCount)
    {
        buttonCount = button_no + 1;
    }
}

/**
 * @brief Update all button states by reading and processing ADC values
 *
 * Performs the main button scanning logic:
 * - Throttles ADC readings using debounce timing
 * - Maintains circular buffer of ADC samples for averaging
 * - Updates min/max calibration values
 * - Calculates averaged ADC value
 * - Updates pressed state for all registered buttons
 *
 * Should be called regularly in the main program loop.
 * Uses static variables to maintain state between calls.
 */
void ADCButtons::updateButtons()
{
    // Static variables to maintain state between calls
    static int currentIndex = 0;
    static bool bufferFilled = false;
    static unsigned long lastUpdate = 0;
    static int lastRawAdcValue = -1;
    static int settlingSamplesRemaining = 0;

    // Throttle ADC readings to avoid excessive sampling
    if (millis() - lastUpdate < debounceTime)
    {
        return;
    }
    lastUpdate = millis();

    // Read ADC value from configured pin
    int adcValue = analogRead(adcPin);

    const int settleThreshold = (margin > 25) ? (margin / 2) : 25;
    if (lastRawAdcValue >= 0 && abs(adcValue - lastRawAdcValue) > settleThreshold)
    {
        settlingSamplesRemaining = samples;
        currentIndex = 0;
        bufferFilled = false;
        DEBUG_LOG("ADCButtons: Detected ADC step from %d to %d, waiting for capacitor settling (%d samples)", lastRawAdcValue, adcValue, settlingSamplesRemaining);
    }
    lastRawAdcValue = adcValue;


    // Store value in circular buffer
    values[currentIndex] = adcValue;
    currentIndex = (currentIndex + 1) % samples;

    if (settlingSamplesRemaining > 0)
    {
        settlingSamplesRemaining--;
        if (settlingSamplesRemaining > 0)
        {
            return;
        }

        currentIndex = 0;
        bufferFilled = false;
        DEBUG_LOG("ADCButtons: Settling phase finished, collecting fresh samples");
        return;
    }

    // Mark buffer as filled after first complete cycle
    if (currentIndex == 0)
        bufferFilled = true;
    else
        return; // Not enough samples yet, skip this update

    // Only use samples if buffer has been filled at least once
    min = (1 << resolution) - 1; // Reset min to max possible ADC value
    max = 0;                     // Reset max to 0
    int sum = 0;
    for (int i = 0; i < samples; i++)
    {
        // Update min/max values for calibration
        if (values[i] < min)
            min = values[i];
        if (values[i] > max)
            max = values[i];

        sum += values[i];
    }

    const int average = sum / samples;
    const int spread = max - min;

    int detectedButton = -1;
    int detectedDistance = (1 << resolution);

    for (int i = 0; i < buttonCount; i++)
    {
        // Skip unused button slots (marked with adcValue = -1)
        if (buttons[i].adcValue < 0)
        {
            continue;
        }

        if (average < buttons[i].adcMin || average > buttons[i].adcMax)
        {
            continue;
        }

        const int rangeCenter = (buttons[i].adcMin + buttons[i].adcMax) / 2;
        const int distance = abs(average - rangeCenter);
        if (distance < detectedDistance)
        {
            detectedDistance = distance;
            detectedButton = i;
        }
    }

    // A valid press must be close to one calibrated button and the sample spread must
    // stay reasonably tight. This is much more tolerant of small ADC noise and short
    // transitions than requiring every single sample to stay inside the button window.
    if (detectedButton >= 0)
    {
        const int allowedSpread = (margin * 4 > 180) ? margin * 4 : 180;
        if (spread > allowedSpread)
        {
            detectedButton = -1;
        }
    }

    bool anyButtonPressedBeforeUpdate = false;
    for (int i = 0; i < buttonCount; i++)
    {
        if (buttons[i].adcValue >= 0 && buttons[i].pressed)
        {
            anyButtonPressedBeforeUpdate = true;
            break;
        }
    }

    if (detectedButton >= 0 || anyButtonPressedBeforeUpdate)
    {
        DEBUG_LOG("ADCButtons: avg=%d min=%d max=%d spread=%d detected=%d distance=%d", average, min, max, spread, detectedButton, detectedDistance);
    }

    // Update all button states
    for (int i = 0; i < buttonCount; i++)
    {
        // Skip unused button slots (marked with adcValue = -1)
        if (buttons[i].adcValue < 0)
        {
            continue;
        }
        
        const bool inRange = (i == detectedButton);
    
        if(buttons[i].pressed && !inRange)  // transition from pressed to released
        {
            // Button released, log the state change
            buttons[i].pressed = false; // Update pressed state
            DEBUG_LOG("Button %d: Released, range=[%d,%d], avg=%d, [ %d, %d ]", i, buttons[i].adcMin, buttons[i].adcMax, average, min, max);
            
            // Call callback if registered
            if (callback != nullptr) {
                callback(i, false);  // button_no = i, pressed = false
            }
        }
        else if(!buttons[i].pressed && inRange) // transition from released to pressed
        {
            buttons[i].pressed = true;
            DEBUG_LOG("Button %d: Pressed, range=[%d,%d], avg=%d, [ %d, %d ]", i, buttons[i].adcMin, buttons[i].adcMax, average, min, max);
            
            // Call callback if registered
            if (callback != nullptr) {
                callback(i, true);  // button_no = i, pressed = true
            }
        }
    }
}

/**
 * @brief Get the current state of a specific button
 *
 * Returns the pressed state of the specified button with bounds checking.
 *
 * @param button_no Button index (0-based)
 * @return true if button is currently pressed, false if not pressed or invalid index
 */
bool ADCButtons::getButtonState(int button_no)
{
    // Validate button number
    if (button_no < 0 || button_no >= buttonCount)
    {
        return false;
    }
    
    // Check if button slot is actually used
    if (buttons[button_no].adcValue < 0)
    {
        return false;  // Unused button slot
    }

    return buttons[button_no].pressed;
}

/**
 * @brief Get the total number of registered buttons
 *
 * @return Number of buttons currently registered in the system (excludes unused slots)
 */
int ADCButtons::getButtonCount() const
{
    int count = 0;
    for (int i = 0; i < buttonCount; i++)
    {
        if (buttons[i].adcValue >= 0)  // Count only used button slots
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief Get the highest button number currently in use
 *
 * @return Highest button number (0-based), or -1 if no buttons registered
 */
int ADCButtons::getMaxButtonNumber() const
{
    return buttonCount - 1;  // buttonCount is the highest index + 1
}

// ============================================================================
// UTILITY AND DIAGNOSTIC METHODS
// ============================================================================

/**
 * @brief Get the current averaged ADC reading
 *
 * Calculates and returns the average of all samples in the circular buffer.
 * Includes all valid samples, including zero values which are valid ADC readings.
 *
 * @return Averaged ADC value from all samples in buffer
 */
int ADCButtons::getAverageADC()
{
    int sum = 0;
    int validSamples = 0;

    // Use all available samples
    for (int i = 0; i < samples; i++)
    {
        sum += values[i]; // Include all values, even 0
        validSamples++;
    }

    return (validSamples > 0) ? (sum / validSamples) : 0;
}

/**
 * @brief Get the minimum ADC value recorded since last reset
 *
 * Returns the minimum ADC value seen since object creation or last calibration reset.
 * Useful for understanding the ADC range and calibrating button thresholds.
 *
 * @return Minimum ADC value recorded
 */
int ADCButtons::getMinValue()
{
    return min;
}

/**
 * @brief Get the maximum ADC value recorded since last reset
 *
 * Returns the maximum ADC value seen since object creation or last calibration reset.
 * Useful for understanding the ADC range and calibrating button thresholds.
 *
 * @return Maximum ADC value recorded
 */
int ADCButtons::getMaxValue()
{
    return max;
}

/**
 * @brief Get the configured ADC pin number
 *
 * Returns the ADC pin number that was configured during object construction.
 *
 * @return ADC pin number being used for button reading
 */
int ADCButtons::getAdcPin() const
{
    return adcPin;
}

/**
 * @brief Reset the min/max calibration values
 *
 * Resets the minimum and maximum ADC values to their initial state.
 * Useful when recalibrating the system or when environmental conditions change.
 * After calling this, new min/max values will be tracked from subsequent readings.
 */
void ADCButtons::resetCalibration()
{
    min = (1 << resolution) - 1; // Set to max possible ADC value
    max = 0;
}
