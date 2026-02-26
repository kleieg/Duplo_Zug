/**
 * @file TrainController.cpp
 * @brief Main application for LEGO DUPLO Train control using DuploHub on ESP32.
 *
 * This file implements the main application logic for controlling a LEGO DUPLO train
 * using the DuploHub class and ESP32 dual-core architecture. It demonstrates a modular,
 * event-driven approach to BLE-based train control, with robust handling of motor, LED,
 * and sound commands, as well as real-time sensor feedback and user input.
 *
 * Architecture:
 * - TrainController: Main application logic (this file)
 * - DuploHub: Hardware abstraction layer with multi-task BLE management
 * - Lpf2Hub: Low-level LEGO Powered Up protocol implementation
 *
 * Features:
 * - Dual-core, multi-task architecture (BLE on Core 0, main loop on Core 1)
 * - Thread-safe command queuing for motor, LED, and sound control
 * - Real-time status monitoring, logging, and error handling
 * - Automatic BLE connection management and recovery
 * - Modular callback system for sensor and event handling
 * - Record and replay of command sequences for automated demos
 * - Emergency stop and user input via debounced buttons and rotary encoder
 *
 * Demo Sequence:
 * - LED color changes (GREEN, RED, etc.)
 * - Motor control (forward, backward, stop)
 * - Sound playback (HORN, BELL, WATER_REFILL, etc.)
 *
 * @author Ralf Zühlsdorff
 * @date 2025
 *
 * @copyright
 * MIT License
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

/*
NEXT GENERATION DUPLO TRAIN CONTROLLER

ONLY ON ESP32-S3 DEVKITC-1 WITH DUAL CORE

*/

#include "DuploHub.h"
#include "SystemMemory.h"

#undef DEBUG // Enable debug logging
#include "debug.h"
#include "ADCButton.h"
#include "StatusLED.h"

#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_sleep.h"

#include <ESP32RotaryEncoder.h>

#define DELAY_TIME 100 // delay in ms

// Pin declaration
// All buttons are wired through a resistor ladder to ADC pin 7 and decoded by voltage level.
// Current calibration (12-bit ADC values) for the GUI button helpers:
// - Record: ~450-750  → starts/stops recording the command stream
// - Play:   ~1000-1400 → toggles replay mode and command playback
// - Stop:   ~1650-2000 → toggles the emergency stop or halts replay immediately
// - Water:  ~2200-2550 → plays the water refill sound effect
// - Light:  ~2800-3150 → cycles the hub LED through the DUPLO color palette
// - Sound:  ~3500-3900 → advances to the next horn/brake sound sample
// Re-run calibrateButtons() if your hardware uses different resistor values.

#define ADC_PIN 7 // Button ADC pin (GPIO 7)

// Speed is defined via rotary encoder
#define ENCODER_A 12 // Encoder A pin
#define ENCODER_B 13 // Encoder B pin
#define ENCODER_BTN 11 // Encoder button pin

// Create rotary encoder instance
RotaryEncoder rotaryEncoder = RotaryEncoder(ENCODER_A, ENCODER_B); //, ENCODER_BTN);

// Onboard RGB LED
#define DATA_PIN 48

// define low voltage threshold for voltage sensor
#define LOW_VOLTAGE_THRESHOLD 5.0f // Voltage threshold for low battery warning

StatusLED statusLed(DATA_PIN); // Pin 48 for ESP32-S3-DevKitC-1

// TrainController instance with DuploHub for BLE communication
DuploHub duploHub;

// record user action time of last user action
unsigned long lastUserActionTime = 0;
#define maxIdleTime 300000 // 300 seconds until going to sleep

static int detectedSpeed = 0;
static float detectedVoltage = 0.0f;
static int detectedColor = 0;
static bool emergencyStop = false;

static bool recording = false;
static bool playback = false;
static bool replay = false; // true when a sequence is being replayed

ADCButtons guiButtons = ADCButtons(ADC_PIN);

static void configureEncoderButtonWakeup(bool enableHold);
static const char *wakeupCauseToString(esp_sleep_wakeup_cause_t cause);
static bool encoderButtonIsRtcWakeCapable();
static void configureEncoderButtonLightSleepWakeup();
static void startWakeupGreenBlink();
static void indicateEnteringSleep();

static bool wakeBlinkActive = false;
static unsigned long wakeBlinkEndMs = 0;

static bool uploadModeCheckActive = false;
static unsigned long uploadModeCheckStartMs = 0;
static unsigned long encoderBtnHoldStartMs = 0;

// Button numbers for GUI buttons
enum GUIButtons
{
    BUTTON_RECORD,
    BUTTON_PLAY,
    BUTTON_STOP,
    BUTTON_WATER,
    BUTTON_LIGHT,
    BUTTON_SOUND
};

// mapping between Duplo Colors and CRGB Colors
// index corresponds to DuploColor enum values
// 0 = BLACK, 1 = PINK, 2 = PURPLE, etc.
CRGB::HTMLColorCode rgbColors[] = {
    CRGB::Black,
    CRGB::Pink,
    CRGB::Purple,
    CRGB::Blue,
    CRGB::LightBlue,
    CRGB::Cyan,
    CRGB::Green,
    CRGB::Yellow,
    CRGB::Orange,
    CRGB::Red,
    CRGB::White};

/**
 * @brief Handles button presses for sound, light, water, and emergency stop.
 *
 * Cycles through sounds and LED colors, triggers water sound, and toggles emergency stop.
 */
void handleButtons(int btn_no, bool pressed)
{

    DEBUG_LOG("TrainController: Button %d is %s", btn_no, pressed ? "pressed" : "released");

    static int color = 0; // Color index for LED cycling
    static int duploSounds[] = {
        DuploEnums::DuploSound::HORN,
        DuploEnums::DuploSound::BRAKE,
        DuploEnums::DuploSound::WATER_REFILL,
        DuploEnums::DuploSound::STATION_DEPARTURE,
        DuploEnums::DuploSound::STEAM};
    static int sound = 0; // Sound index for sound playbook

    if (pressed == false || !duploHub.isConnected())
        return;

    lastUserActionTime = millis();

    // If replay is active, only STOP and PLAY (replay) buttons are allowed
    if (replay && btn_no != BUTTON_STOP && btn_no != BUTTON_PLAY) {
        DEBUG_LOG("TrainController: Ignoring button %d during replay", btn_no);
        return;
    }

    switch (btn_no)
    {
    case BUTTON_SOUND:
        sound++;
        if (sound == 2) sound = 3; // skip WATER_REFILL sound
        if (sound == 5) sound = 0;
        duploHub.playSound((DuploEnums::DuploSound)duploSounds[sound]);
        DEBUG_LOG("TrainController: Sound button pressed, playing sound %d", sound);
        delay(DELAY_TIME);
        break;
    case BUTTON_LIGHT:
        color = (color + 1) % 11; // Cycle through DuploColor enum values
        duploHub.setLedColor((DuploEnums::DuploColor)color);
        DEBUG_LOG("TrainController: Light button pressed, color %d", color);
        delay(DELAY_TIME);
        break;
    case BUTTON_WATER:
        duploHub.playSound((DuploEnums::DuploSound)duploSounds[2]); // Play WATER_REFILL sound
        DEBUG_LOG("TrainController: Water button pressed, playing WATER_REFILL");
        delay(DELAY_TIME);
        break;
    case BUTTON_RECORD:
        DEBUG_LOG("TrainController: start or stop recording");
        recording = !recording; // Toggle recording state
        // stop playback if recording is active
        if (recording)
        {
            DEBUG_LOG("TrainController: Recording started");
            statusLed.setColor(CRGB::Red); // Blink red when recording
            statusLed.setBlinking(true, 500, 1000);
            duploHub.recordCommands(true); // Start recording commands
            delay(DELAY_TIME);             // Allow time for sound to play
        }
        else
        {
            DEBUG_LOG("TrainController: Recording stopped");
            duploHub.recordCommands(false); // Stop recording commands
            statusLed.setOff();             // LED off when recording stops
            delay(DELAY_TIME); // Allow time for sound to play
        }
        break;
    case BUTTON_PLAY:
        DEBUG_LOG("TrainController: start or stop playback");
        // disable playback when recording is active
        if (recording)
        {
            DEBUG_LOG("TrainController: Playback disabled while recording is active");
            return; // Ignore playback if recording is active
        }
        playback = !playback; // Toggle playback state  
        if (playback)
        {
            duploHub.replayCommands();
            statusLed.setColor(CRGB::Yellow);           // Blink yellow during replay
            statusLed.setBlinking(true, 500, 1000);
            replay = true; // Set replay mode
        }
        else
        {
            duploHub.stopReplay();
            statusLed.setOff();
            replay = false; // End replay mode
        }
        DEBUG_LOG("TrainController: Playback %s", playback ? "started" : "stopped");
        delay(DELAY_TIME);
        break;
    case BUTTON_STOP:
        emergencyStop = !emergencyStop; // Toggle emergency stop state
        // If replay is active, stop replay and playback immediately
        if (replay) {
            duploHub.stopReplay();
            statusLed.setOff();
            playback = false;
            replay = false;
            DEBUG_LOG("TrainController: STOP pressed during replay, replay stopped");
            return;
        }
        if (emergencyStop)
        {
            statusLed.setColor(CRGB::Red); // Solid red when stop is active
            statusLed.setBlinking(false);
            duploHub.playSound((DuploEnums::DuploSound::BRAKE)); // Play brake sound
            delay(DELAY_TIME);                                   // Allow time for sound to play
            duploHub.setLedColor(DuploEnums::DuploColor::RED);
            delay(DELAY_TIME);
            duploHub.stopMotor();
            delay(DELAY_TIME); // Stop the motor immediately
            rotaryEncoder.setEncoderValue(0);
        }
        else
        {
            duploHub.playSound((DuploEnums::DuploSound::STATION_DEPARTURE)); // Play departure sound
            delay(DELAY_TIME);                                               // Allow time for sound to play
            duploHub.setLedColor(DuploEnums::DuploColor::GREEN);             // Set LED to green
            statusLed.setOff(); // LED off when stop is released
            delay(DELAY_TIME); // Allow time for sound to play
        }
        DEBUG_LOG("TrainController: Emergency stop %s", emergencyStop ? "activated" : "deactivated");
        delay(DELAY_TIME);
        break;
    default:
        DEBUG_LOG("TrainController: Unknown button %d", btn_no);
        return; // Ignore unknown buttons
    }
}

// Sets the speed of the currently selected train
#define MIN_SPEED 10
#define MAX_SPEED 100
#define SPEED_INC 5
#define POTI_DELAY 250

static int lastSpeed = 0; // holds current mapping of poti setting to speed

/**
 * @brief Handles potentiometer input and sets train speed accordingly.
 *
 * Stops the train if emergency stop is active or if the potentiometer is in the zero zone.
 */
static void handleEncoder(long speed)
{
    static long lastCall = millis();

    DEBUG_LOG("TrainController: Set motor speed to %d", speed);

    if (emergencyStop)
    {
        duploHub.setMotorSpeed(0);        // Stop the motor immediately
        rotaryEncoder.setEncoderValue(0); // Reset encoder value
        delay(DELAY_TIME);
        DEBUG_LOG("TrainController: Emergency stop activated, motor speed set to 0");
        return; // Exit if emergency stop is active
    }

    if (millis() - lastCall > POTI_DELAY)
    {
        lastCall = millis(); // Update last call time

        // if emergencyStop triggered or poti in zero zone
        if (speed > -MIN_SPEED && speed < MIN_SPEED)
        {
            duploHub.setMotorSpeed(0);
            lastSpeed = 0; // Reset lastSpeed to avoid repeated calls
        }
        else
        {
            // train speed changed
            lastUserActionTime = millis();
            duploHub.setMotorSpeed(speed);
            lastSpeed = speed;
        }
        delay(DELAY_TIME);
        DEBUG_LOG("TrainController: Set motor speed to %d", lastSpeed);
    }
}

// Function to process the responseQueue and print detected color
/**
 * @brief Callback for detected color events from the color sensor.
 * @param color The detected color (DuploEnums::DuploColor).
 */
static void detectedColorCb(DuploEnums::DuploColor color)
{
    if (replay || recording) {
        DEBUG_LOG("TrainController: Color sensor callback ignored during recording and replay");
        return;
    }

    DEBUG_LOG("TrainController:  Detected Color: %d", color);

    detectedColor = color; // Store the detected color
    switch (color)
    {
    case DuploEnums::DuploColor::BLACK:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::BLACK);   
        duploHub.setLedColor(DuploEnums::DuploColor::BLACK);
        statusLed.setColor(CRGB::Black);
        delay(DELAY_TIME);
        break;
    case DuploEnums::DuploColor::RED:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::RED);
        emergencyStop = !emergencyStop; // Toggle emergency stop state
        statusLed.setColor(CRGB::Red);
        statusLed.setBlinking(true, 250, 250, 3); // Set LED color to white
        delay(DELAY_TIME);
        duploHub.playSound((DuploEnums::DuploSound::BRAKE)); // Play brake sound
        delay(DELAY_TIME);                                   // Allow time for sound to play
        duploHub.setLedColor(DuploEnums::DuploColor::RED);
        delay(DELAY_TIME);
        break;
    case DuploEnums::DuploColor::YELLOW:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::YELLOW);
        duploHub.setLedColor(DuploEnums::DuploColor::YELLOW);
        statusLed.setColor(CRGB::Yellow);
        statusLed.setBlinking(true, 250, 250, 3); // Set LED color to white
        delay(DELAY_TIME);
        duploHub.playSound((DuploEnums::DuploSound::HORN)); // Play horn sound
        delay(DELAY_TIME);                                  // Allow time for sound to play
        break;
    case DuploEnums::DuploColor::BLUE:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::BLUE);
        duploHub.setLedColor(DuploEnums::DuploColor::BLUE);
        statusLed.setColor(CRGB::Blue);
        statusLed.setBlinking(true, 250, 250, 3); // Set LED color to white
        delay(DELAY_TIME);
        duploHub.stopMotor();
        delay(DELAY_TIME);                                          // Stop the motor
        duploHub.playSound((DuploEnums::DuploSound::WATER_REFILL)); // Play water refill sound
        delay(DELAY_TIME);                                          // Allow time for sound to play
        duploHub.setMotorSpeed(lastSpeed);                          // Restore the last speed
        delay(DELAY_TIME);
        break;
    case DuploEnums::DuploColor::WHITE:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::WHITE);
        for (int i = 0; i < 3; i++)
        {
            duploHub.setLedColor(DuploEnums::DuploColor::WHITE);
            statusLed.setColor(CRGB::White);
            statusLed.setBlinking(true, 250, 250, 3); // Set LED color to white
        }
        break;
    default:
        DEBUG_LOG("TrainController:  Default Color action: %d", color);
        duploHub.setLedColor(color);
    }
}

// Function to process the responseQueue and print detected voltage
/**
 * @brief Callback for detected voltage events from the voltage sensor.
 * @param voltage The detected voltage value.
 */
// Only execute callback if time since last execution > CALC_VOLTAGE_DELAY
#ifndef CALC_VOLTAGE_DELAY
#define CALC_VOLTAGE_DELAY 5000 // ms, default if not defined elsewhere
#endif
static void detectedVoltageCb(float voltage)
{
    static unsigned long lastExec = 0;
    unsigned long now = millis();
    if (now - lastExec < CALC_VOLTAGE_DELAY) {
        return;
    }
    lastExec = now;
    DEBUG_LOG("TrainController:  Detected Voltage: %.2f V", voltage);
    detectedVoltage = voltage;
}

// Function to process the responseQueue and print detected color
/**
 * @brief Callback for detected speed events from the speed sensor.
 * @param speed The detected speed value.
 */
static void detectedSpeedCb(int speed)
{
    DEBUG_LOG("TrainController:  Detected Speed: %d", speed);
    detectedSpeed = speed;
}

// Callback function when hub connects
/**
 * @brief Callback function called when the hub is connected.
 *
 * Activates all relevant ports and registers sensor callbacks.
 */
String duploHubAddress = "";

static void onHubConnected()
{
    DEBUG_LOG("TrainController: Hub instance connected, starting initialization...");
    statusLed.setColor(CRGB::Blue);           // Set color to blue
    statusLed.setBlinking(true, 200, 300, 3); // Blink blue 3x
    delay(1800);                              // Wait for 3 blinks (3 * (200+300) ms)
    statusLed.setOff();                       // Turn LED off after blinking

    // reset user activity timer to start value
    lastUserActionTime = millis();

#ifdef DEBUG
    duploHub.listDevicePorts(); // List connected devices on the hub
    delay(1000);
#endif

    duploHub.activateRgbLight();
    delay(200);
    duploHub.activateBaseSpeaker();
    delay(200);
    duploHub.activateColorSensor();
    delay(200);
    duploHub.activateSpeedSensor();
    delay(200);
    duploHub.activateVoltageSensor();
    delay(200);
    DEBUG_LOG("DuploHub: All ports activated");

    duploHub.setDetectedColorCallback(detectedColorCb);
    delay(200);
    duploHub.setDetectedSpeedCallback(detectedSpeedCb);
    delay(200);
    duploHub.setDetectedVoltageCallback(detectedVoltageCb);
    delay(200);
    DEBUG_LOG("DuploHub: All callbacks registered");

    duploHub.setHubName("DuploTrain_1");
    delay(200);
    
    duploHubAddress = String(duploHub.getHubAddress().c_str());

    DEBUG_LOG("TrainController: Connected to hub: %s", duploHub.getHubName().c_str());
    DEBUG_LOG("TrainController: Hub address: %s", duploHubAddress.c_str());
}

// Callback function when hub disconnects
/**
 * @brief Callback function called when the hub is disconnected.
 */
static void onHubDisconnected()
{
    DEBUG_LOG("TrainController: Hub disconnected - stopping all operations");
    statusLed.setColor(CRGB::White);          // Set color to white
    statusLed.setBlinking(true, 200, 300, 3); // Blink white 3x
    delay(1800);                              // Wait for 3 blinks (3 * (200+300) ms)
    statusLed.setOff();                       // Turn LED off after blinking
}
/**
 * @brief Execute the next recorded command when its playback timestamp arrives.
 *
 * Pulls commands from the hub's replay buffer in chronological order, skipping execution if the
 * emergency stop is active. When the sequence completes, the LED and replay flags are reset.
 */
void replayNextCommand()
{
    const HubCommand *cmd = duploHub.getNextReplayCommand();
    if (cmd == nullptr)
    {
        // If replay is no longer active, stop playback and update LED
        if (!duploHub.isReplayActive()) {
            playback = false;
            replay = false;
            statusLed.setBlinking(false); // Stop blinking first
            statusLed.setOff();           // LED off when replay ends
            DEBUG_LOG("TrainController: No more commands to replay, stopping replay");
        } else {
            DEBUG_LOG("TrainController: Not the time to replay command");
        }
        return;
    }
    DEBUG_LOG("TrainController: Replaying command type %d", cmd->type);
    if (!emergencyStop)
    {
        // Execute the command based on its type
        switch (cmd->type)
        {
        case CMD_MOTOR_SPEED:
            duploHub.setMotorSpeed(cmd->data.motor.speed);
            break;
        case CMD_STOP_MOTOR:
            duploHub.stopMotor();
            break;
        case CMD_SET_LED_COLOR:
            duploHub.setLedColor(cmd->data.led.color);
            break;
        case CMD_PLAY_SOUND:
            duploHub.playSound(cmd->data.sound.soundId);
            break;
        case CMD_SET_HUB_NAME:
            duploHub.setHubName(cmd->data.hubName.name);
            break;
        case CMD_ACTIVATE_RGB_LIGHT:
            duploHub.activateRgbLight();
            break;
        case CMD_ACTIVATE_BASE_SPEAKER:
            duploHub.activateBaseSpeaker();
            break;
        case CMD_ACTIVATE_COLOR_SENSOR:
            duploHub.activateColorSensor();
            break;
        case CMD_ACTIVATE_SPEED_SENSOR:
            duploHub.activateSpeedSensor();
            break;
        case CMD_ACTIVATE_VOLTAGE_SENSOR:
            duploHub.activateVoltageSensor();
            break;
        default:
            DEBUG_LOG("TrainController: Unknown command type %d in replay", cmd->type);
            break;
        }
        DEBUG_LOG("TrainController: Replayed command type %d", cmd->type);
    }
}

/**
 * @brief Arduino setup function. Initializes serial, memory info, buttons, DuploHub, and callbacks.
 */
void setup()
{
    // Deep sleep can leave GPIO hold enabled across reset. Disable it early so inputs behave normally.
    gpio_deep_sleep_hold_dis();
    rtc_gpio_hold_dis((gpio_num_t)ENCODER_BTN);

#ifdef DEBUG
    Serial.begin(115200);
    delay(1000);

    //  SerialMUTEX();
    DEBUG_LOG("TrainController (2 Core) : Starting up...");
    DEBUG_LOG("");

    printMemoryInfo();

    {
        const esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
        DEBUG_LOG("TrainController: Wakeup cause=%d (%s)", (int)wakeupCause, wakeupCauseToString(wakeupCause));
    }

    DEBUG_LOG("TrainController: ENCODER_BTN GPIO=%d, RTC-wake-capable=%s", ENCODER_BTN, encoderButtonIsRtcWakeCapable() ? "yes" : "no");
#endif

    configureEncoderButtonWakeup(false);

    // Initialize onboard RGB LED
    statusLed.begin();                     // Initialize FastLED,,,,z
    statusLed.setBrightness(100);          // Set brightness
    
    const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

    // Update mode is entered only when encoder button is held LOW continuously for >= 2 seconds
    // shortly after boot/wakeup. Short presses will not trigger the 10-second upload window.
    uploadModeCheckActive = true;
    uploadModeCheckStartMs = millis();
    encoderBtnHoldStartMs = 0;

    // If we woke from sleep, start green blink 5x as a visual confirmation.
    if (wakeCause == ESP_SLEEP_WAKEUP_EXT0 || wakeCause == ESP_SLEEP_WAKEUP_EXT1 || wakeCause == ESP_SLEEP_WAKEUP_GPIO) {
        startWakeupGreenBlink();
    }
    
    if (!wakeBlinkActive) {
        statusLed.setColor(CRGB::Yellow);         // Set color to yellow during init
        statusLed.setBlinking(true, 200, 300); // Blink: 200ms on, 300ms off
    }

    // setup push buttons and register callback
    guiButtons.addButton(BUTTON_RECORD, 600);         // Button 1: Record sequence
    guiButtons.addButton(BUTTON_PLAY, 1220);          // Button 2: Play recorded sequence
    guiButtons.addButton(BUTTON_STOP, 1815);          // Button 3: Emergency Stop
    guiButtons.addButton(BUTTON_WATER, 2375);         // Button 4: Make sound fill up water
    guiButtons.addButton(BUTTON_LIGHT, 2965);         // Button 5: Change lights
    guiButtons.addButton(BUTTON_SOUND, 3750);         // Button 2: Sound
    guiButtons.registerButtonCallback(handleButtons); // Register button callback

    // initialize rotary encoder and register callback
    rotaryEncoder.setEncoderType(EncoderType::FLOATING);
    rotaryEncoder.setBoundaries(-MAX_SPEED, MAX_SPEED, false);
    rotaryEncoder.setStepValue(SPEED_INC);
    rotaryEncoder.setEncoderValue(0);
    rotaryEncoder.onTurned(&handleEncoder);
    // This is where the inputs are configured and the interrupts get attached
    rotaryEncoder.begin();

    // Initialize DuploHub instance
    duploHub.init();
    delay(200);
    // Register connection event callbacks
    duploHub.setOnConnectedCallback(onHubConnected);
    delay(200);
    duploHub.setOnDisconnectedCallback(onHubDisconnected);
    delay(200); // Allow time for BLE task to initialize

    DEBUG_LOG("TrainController: Ready - BLE task running, waiting for hub connection...");
    if (!wakeBlinkActive) {
        statusLed.setColor(CRGB::Green);       // Set color to green after initialization
        statusLed.setBlinking(false);          // Solid green
    }

    // Start idle timer from boot so sleep works even when no user action is detected yet
    // (e.g. no hub connected -> button callbacks may early-return).
    lastUserActionTime = millis();
    DEBUG_LOG("TrainController: Idle timer started (maxIdleTime=%d ms)", maxIdleTime);
}

static void startWakeupGreenBlink()
{
    constexpr int blinkTimes = 5;
    constexpr int onMs = 120;
    constexpr int offMs = 120;

    wakeBlinkActive = true;
    wakeBlinkEndMs = millis() + (unsigned long)blinkTimes * (unsigned long)(onMs + offMs) + 200;

    statusLed.setColor(CRGB::Green);
    statusLed.setBlinking(true, onMs, offMs, blinkTimes);
}

static void indicateEnteringSleep()
{
    // WS2812 LEDs keep their last color while the MCU sleeps, so explicitly show “sleep now”.
    statusLed.setBlinking(false);
    statusLed.setColor(CRGB::Red);
    statusLed.setBlinking(true, 120, 120, 2);

    const unsigned long until = millis() + 800;
    while ((long)(millis() - until) < 0) {
        statusLed.update();
        delay(10);
    }

    statusLed.setOff();
    statusLed.update();
    delay(50);
}

static const char *wakeupCauseToString(esp_sleep_wakeup_cause_t cause)
{
    switch (cause)
    {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_ALL:
        return "ALL";
    case ESP_SLEEP_WAKEUP_EXT0:
        return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
        return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
        return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP:
        return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:
        return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:
        return "UART";
    case ESP_SLEEP_WAKEUP_WIFI:
        return "WIFI";
    case ESP_SLEEP_WAKEUP_COCPU:
        return "COCPU";
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        return "COCPU_TRAP_TRIG";
    case ESP_SLEEP_WAKEUP_BT:
        return "BT";
    default:
        return "(unknown)";
    }
}

static bool encoderButtonIsRtcWakeCapable()
{
    const gpio_num_t wakeGpio = (gpio_num_t)ENCODER_BTN;
    return rtc_gpio_is_valid_gpio(wakeGpio);
}

static void configureEncoderButtonWakeup(bool enableHold)
{
    const gpio_num_t wakeGpio = (gpio_num_t)ENCODER_BTN;

    if (!rtc_gpio_is_valid_gpio(wakeGpio)) {
        DEBUG_LOG("TrainController: ENCODER_BTN GPIO %d is not RTC IO -> deep-sleep wake not possible", (int)wakeGpio);
        return;
    }

    // Keep RTC peripherals powered for stable button biasing during deep sleep.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Normal GPIO configuration for runtime reads.
    pinMode(ENCODER_BTN, INPUT_PULLUP);

    // Configure RTC pull-up for stable LOW-active button wake.
    rtc_gpio_hold_dis(wakeGpio);
    rtc_gpio_init(wakeGpio);
    rtc_gpio_set_direction(wakeGpio, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(wakeGpio);
    rtc_gpio_pulldown_dis(wakeGpio);

    // EXT1 wake on LOW level (single pin in ALL_LOW mode).
    esp_err_t err = esp_sleep_enable_ext1_wakeup((1ULL << ENCODER_BTN), ESP_EXT1_WAKEUP_ALL_LOW);
    if (err != ESP_OK) {
        DEBUG_LOG("TrainController: esp_sleep_enable_ext1_wakeup(GPIO%d, ALL_LOW) failed: %d", (int)wakeGpio, (int)err);
    } else {
        DEBUG_LOG("TrainController: Deep-sleep wake configured on GPIO%d (EXT1 ALL_LOW)", (int)wakeGpio);
    }

    if (enableHold) {
        rtc_gpio_hold_en(wakeGpio);
        gpio_deep_sleep_hold_en();
    }
}

static void configureEncoderButtonLightSleepWakeup()
{
    const gpio_num_t wakeGpio = (gpio_num_t)ENCODER_BTN;
    pinMode(ENCODER_BTN, INPUT_PULLUP);

    // Light sleep GPIO wake works on any GPIO (RTC + digital IO).
    esp_err_t err = gpio_wakeup_enable(wakeGpio, GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK) {
        DEBUG_LOG("TrainController: gpio_wakeup_enable(%d) failed: %d", (int)wakeGpio, (int)err);
        return;
    }
    err = esp_sleep_enable_gpio_wakeup();
    if (err != ESP_OK) {
        DEBUG_LOG("TrainController: esp_sleep_enable_gpio_wakeup() failed: %d", (int)err);
    }
}

/**
 * @brief Periodically logs the status of the BLE task, hub connection, and CPU temperature.
 * @param duploHub Reference to the DuploHub instance.
 */
void checkStatus(DuploHub &duploHub)
{
    static unsigned long lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 300000)
    { // Every 30 seconds
        DEBUG_LOG("TrainController Status - BLE Task: %s, Hub Connected: %s",
                  duploHub.isBLETaskRunning() ? "Running" : "Stopped",
                  duploHub.isConnected() ? "Yes" : "No");
        float cpuTemp = getCPUTemperature();
        DEBUG_LOG("CPU Temperature: %.2f °C", cpuTemp);

        if(detectedVoltage < LOW_VOLTAGE_THRESHOLD && !(recording || playback))
        {
            DEBUG_LOG("TrainController: Low voltage detected: %.2f V", detectedVoltage);
            statusLed.setColor(CRGB::Red); // Set LED to red for low voltage
            statusLed.setBlinking(true, 100, 100, 5); // Blink red
        }
    
        lastStatusUpdate = millis();
    }
}
/**
 * @brief Enter deep sleep after the inactivity timer expires.
 *
 * Invoked from the main loop when no user interaction has been detected for the configured
 * duration. Currently jumps straight into deep sleep because the hardware reset circuit already
 * indicates the transition.
 */
void esp_goto_sleep()
{
        DEBUG_LOG("TrainController: No user action for %d ms, entering deep sleep...", maxIdleTime);

    // Re-apply wake configuration right before sleeping in case runtime code changed GPIO routing.
    configureEncoderButtonWakeup(true);

    DEBUG_LOG("TrainController: ENCODER_BTN level before sleep=%d", digitalRead(ENCODER_BTN));
#ifdef DEBUG
    Serial.flush();
#endif
    indicateEnteringSleep();

    if (encoderButtonIsRtcWakeCapable()) {
        // Enter deep sleep (wake via EXT0)
        esp_deep_sleep_start();
    }

    // Fallback: if ENCODER_BTN isn't RTC IO, EXT0 can't wake from deep sleep.
    // Use light sleep with GPIO wake so the button still wakes the controller.
    DEBUG_LOG("TrainController: Falling back to light sleep (GPIO wake)");
    configureEncoderButtonLightSleepWakeup();
    esp_light_sleep_start();
    DEBUG_LOG("TrainController: Woke from light sleep");
    startWakeupGreenBlink();
    lastUserActionTime = millis();
}

// main loop
/**
 * @brief Arduino main loop. Handles input, updates train state, and processes hub responses.
 */
void loop()
{
    statusLed.update(); // Handle blinking

    // Keep UI state in sync when recording auto-stops in DuploHub (e.g. fixed buffer full).
    if (recording && !duploHub.isRecordingCommandsEnabled()) {
        recording = false;
        statusLed.setOff();
        DEBUG_LOG("TrainController: Recording auto-stopped (record buffer full), LED turned off");
    }

    // Finalize non-blocking wake blink: ensure we end up solid green.
    if (wakeBlinkActive && (long)(millis() - wakeBlinkEndMs) >= 0) {
        wakeBlinkActive = false;
        statusLed.setBlinking(false);
        statusLed.setColor(CRGB::Green);
    }

    // Enter upload mode only after continuous >=2s button hold during startup phase.
    if (uploadModeCheckActive) {
        const unsigned long now = millis();
        const bool btnLow = (digitalRead(ENCODER_BTN) == LOW);

        if (btnLow) {
            if (encoderBtnHoldStartMs == 0) {
                encoderBtnHoldStartMs = now;
            } else if (now - encoderBtnHoldStartMs >= 2000) {
                uploadModeCheckActive = false;
                statusLed.setColor(CRGB::Blue);
                statusLed.setBlinking(true, 100, 100); // Fast blue blink = upload mode
                DEBUG_LOG("=== UPLOAD MODE ===");
                DEBUG_LOG("Ready for firmware upload - waiting 10 seconds...");
                for (int i = 0; i < 100; i++) {
                    statusLed.update();
                    delay(100); // 10 seconds total
                }
                DEBUG_LOG("Upload window closed, continuing normal boot...");
            }
        } else {
            encoderBtnHoldStartMs = 0;
            // After a short startup phase with no long hold, disable check completely.
            if (now - uploadModeCheckStartMs > 3000) {
                uploadModeCheckActive = false;
            }
        }
    }

    // Periodic idle countdown log for debugging sleep timing.
    static unsigned long lastIdleLog = 0;
    if (millis() - lastIdleLog >= 5000) {
        lastIdleLog = millis();
        if (lastUserActionTime == 0) {
            DEBUG_LOG("TrainController: Idle timer not started (lastUserActionTime=0)");
        } else {
            const unsigned long idleMs = millis() - lastUserActionTime;
            const long remaining = (long)maxIdleTime - (long)idleMs;
            DEBUG_LOG("TrainController: Idle=%lu ms, remaining=%ld ms", idleMs, remaining);
        }
    }

    // Check for idle time to enter deep sleep
    if(lastUserActionTime != 0 && (millis() - lastUserActionTime) > maxIdleTime)
    {
        esp_goto_sleep();
    }

 
    if (duploHub.isConnected())
    {
        guiButtons.updateButtons();

        // Handle replay commands if replay is active
        if (duploHub.isReplayActive())
        {
            replayNextCommand();
        }

         duploHub.processResponseQueue();
    }

    // Check Duplo Hub State
    checkStatus(duploHub);

    delay(20); // Add a small delay to reduce CPU load and prevent guru meditation error
} // End of loop
