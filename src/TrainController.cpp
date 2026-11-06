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
 * - Stop (STOP button or dial press) and user input via debounced buttons and rotary encoder
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

//  #undef DEBUG // Enable debug logging
#define DEBUG // Enable debug logging
#include "debug.h"
#include "ADCButton.h"
#include "StatusLED.h"

#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_system.h"

#include "PcntEncoder.h"

#include <atomic>

// BLE name assigned to the connected train hub during initialization.
#define HUB_NAME "DuploTrain_1"

// Pin declaration
// All buttons are wired through a resistor ladder to ADC pin 7 and decoded by voltage level.
// Current calibration (12-bit ADC values) for the GUI button helpers:
// Measured nominal values with the assembled resistor ladder are approximately:
// - Record: ~670
// - Play:   ~1320
// - Stop:   ~1930
// - Water:  ~2500
// - Light:  ~3090
// - Sound:  ~3690
//
// Detection ranges are therefore derived from the midpoints between neighboring
// nominal values, but leave a dead zone of about 240 ADC counts between buttons
// for robust separation.
// Re-run calibrateButtons() if your hardware uses different resistor values.

#define ADC_PIN 7 // Button ADC pin (GPIO 7)

// Speed is defined via rotary encoder
#define ENCODER_A 12               // Encoder A pin
#define ENCODER_B 13               // Encoder B pin
#define ENCODER_BTN 11             // Encoder button pin
// Turning direction of the dial: set to -1 if turning right/left is reversed.
#define DIAL_DIRECTION 1

// Rotary encoder read by the hardware pulse counter (one speed step per click, see PcntEncoder.h).
// The push button is handled separately (wake, stop, hold-to-sleep).
PcntEncoder rotaryEncoder(ENCODER_A, ENCODER_B);

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

// Application-visible sensor and controller state mirrored from callbacks and UI actions.
static int detectedSpeed = 0;
static float detectedVoltage = 0.0f;
static int detectedColor = 0;

/**
 * @brief Plain stop (STOP button and dial press): stop motor, end replay, knob to 0.
 */
static void normalStop();

// High-level operating modes used by the main loop and status LED logic.
static bool recording = false;
static bool replay = false; // true when a sequence is being replayed
static bool hubReady = false;

// Last motor speed commanded by the controller (0 = stopped). Only touched from the main loop.
static int lastSpeed = 0;

// Pending dial value (polled from the pulse counter in the main loop) and hand-over flags from
// the BLE task (core 0): the connection callbacks only set these flags, all UI and state work
// happens in loop() on core 1.
static std::atomic<long> pendingEncoderValue{0};
static std::atomic<bool> encoderValuePending{false};
static std::atomic<bool> hubConnectedEventPending{false};
static std::atomic<bool> hubDisconnectedEventPending{false};

// A press is reported after `ADC_BUTTON_SAMPLES` settling samples plus `ADC_BUTTON_SAMPLES`
// averaged samples, one sample per loop pass (~20 ms): 3 -> ~120-150 ms hold time
// (was 10 -> ~0.4-0.5 s). Increase if presses get misdetected while the ladder capacitor settles.
#define ADC_BUTTON_SAMPLES 3
ADCButtons guiButtons = ADCButtons(ADC_PIN, ADC_BUTTON_SAMPLES);

/**
 * @brief Configure deep-sleep wake handling for the encoder button.
 * @param enableHold `true` to retain RTC configuration across deep sleep.
 */
static void configureEncoderButtonWakeup(bool enableHold);

/**
 * @brief Configure the encoder button as a normal digital input for runtime use.
 *
 * The RTC wake configuration is only applied right before sleeping (see `esp_goto_sleep()`).
 */
static void configureEncoderButtonRuntime();

// Dial (encoder) button: press = normal stop, hold = sleep now, press while asleep = wake.
#define ENCODER_BTN_DEBOUNCE_MS 30
#define ENCODER_BTN_HOLD_TO_SLEEP_MS 2000

// The press that woke the controller is ignored until it is released, so it neither stops
// nor (if held a bit longer) immediately puts the controller back to sleep.
static bool encoderBtnIgnoreUntilRelease = false;

/**
 * @brief Convert an ESP sleep wake reason into a readable string.
 * @param cause Wakeup reason reported by the ESP-IDF sleep subsystem.
 * @return Static string representation of the wake cause.
 */
static const char *wakeupCauseToString(esp_sleep_wakeup_cause_t cause);

/**
 * @brief Check whether the encoder button pin supports RTC wake.
 * @return `true` when the configured pin can be used for RTC-based wakeup.
 */
static bool encoderButtonIsRtcWakeCapable();

/**
 * @brief Configure the encoder button as a light-sleep wake source.
 */
static void configureEncoderButtonLightSleepWakeup();

/**
 * @brief Start the green blink sequence used to acknowledge wakeup.
 */
static void startWakeupGreenBlink();

/**
 * @brief Show the visual transition that indicates imminent sleep entry.
 */
static void indicateEnteringSleep();

/**
 * @brief Decide whether the idle ready-heartbeat may currently be shown.
 * @return `true` if no higher-priority status indication is active.
 */
static bool shouldShowReadyHeartbeat();

/**
 * @brief Drive the periodic ready-heartbeat blink pattern.
 */
static void updateReadyHeartbeat();

/**
 * @brief Advance the non-blocking post-connect hub initialization.
 */
static void updateHubInitialization();

static bool wakeBlinkActive = false;
static unsigned long wakeBlinkEndMs = 0;

// Idle LED state bookkeeping.
static bool readyHeartbeatActive = false;
static unsigned long lastReadyHeartbeatMs = 0;

/**
 * @brief Steps of the deferred post-connect initialization sequence.
 *
 * The BLE callback only arms this state machine. The main loop then walks the hub through
 * device activation and callback registration without blocking BLE housekeeping.
 */
enum class HubInitializationStep
{
    Idle,
    RegisterCallbacks,
    ActivateRgbLight,
    ActivateBaseSpeaker,
    ActivateColorSensor,
    ActivateSpeedSensor,
    ActivateVoltageSensor,
    SetHubName,
    WaitForReady
};

// Runtime state for the non-blocking hub initialization state machine.
static bool hubInitializationActive = false;
static HubInitializationStep hubInitializationStep = HubInitializationStep::Idle;
static unsigned long hubInitializationStepStartedMs = 0;

#define READY_HEARTBEAT_INTERVAL_MS 8000UL
#define READY_HEARTBEAT_ON_MS 120
#define READY_HEARTBEAT_OFF_MS 180
#define READY_HEARTBEAT_BLINKS 2

#define HUB_INIT_STEP_INTERVAL_MS 80UL
#define HUB_INIT_READY_SETTLE_MS 1200UL

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
 * @brief Handles button presses for sound, light, water, record/play and stop.
 *
 * Cycles through sounds and LED colors, triggers water sound, and stops the train.
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

    if (pressed == false || !duploHub.isConnected() || !hubReady)
        return;

    lastUserActionTime = millis();

    // If replay is active, only STOP and PLAY (replay) buttons are allowed
    if (replay && btn_no != BUTTON_STOP && btn_no != BUTTON_PLAY)
    {
        DEBUG_LOG("TrainController: Ignoring button %d during replay", btn_no);
        return;
    }

    switch (btn_no)
    {
    case BUTTON_SOUND:
        // Play the current entry first so the cycle starts with HORN, then advance.
        duploHub.playSound((DuploEnums::DuploSound)duploSounds[sound]);
        DEBUG_LOG("TrainController: Sound button pressed, playing sound %d", sound);
        sound++;
        if (sound == 2)
            sound = 3; // skip WATER_REFILL sound (has its own button)
        if (sound == 5)
            sound = 0;
        break;
    case BUTTON_LIGHT:
        color = (color + 1) % 11; // Cycle through DuploColor enum values
        duploHub.setLedColor((DuploEnums::DuploColor)color);
        DEBUG_LOG("TrainController: Light button pressed, color %d", color);
        break;
    case BUTTON_WATER:
        duploHub.playSound((DuploEnums::DuploSound)duploSounds[2]); // Play WATER_REFILL sound
        DEBUG_LOG("TrainController: Water button pressed, playing WATER_REFILL");
        break;
    case BUTTON_RECORD:
        DEBUG_LOG("TrainController: start or stop recording");
        recording = !recording; // Toggle recording state
        // stop playback if recording is active
        if (recording)
        {
            DEBUG_LOG("TrainController: Recording started");
            duploHub.recordCommands(true); // Start recording commands
        }
        else
        {
            DEBUG_LOG("TrainController: Recording stopped");
            duploHub.recordCommands(false); // Stop recording commands
        }
        break;
    case BUTTON_PLAY:
        DEBUG_LOG("TrainController: start or stop playback");
        if (replay)
        {
            DEBUG_LOG("TrainController: PLAY ignored because replay is already active");
            return;
        }
        // disable replay when recording is active
        if (recording)
        {
            DEBUG_LOG("TrainController: Playback disabled while recording is active");
            return; // Ignore playback if recording is active
        }
        duploHub.replayCommands();
        replay = duploHub.isReplayActive(); // stays false if nothing was recorded
        DEBUG_LOG("TrainController: Playback %s", replay ? "started" : "skipped (nothing recorded)");
        break;
    case BUTTON_STOP:
        // Same plain stop as the dial press: nothing is locked, turning the dial drives again.
        normalStop();
        break;
    default:
        DEBUG_LOG("TrainController: Unknown button %d", btn_no);
        return; // Ignore unknown buttons
    }
}

// Speed range of the dial. Below MIN_SPEED the DUPLO motor barely moves, so the dial skips that
// zone: from standstill the first click jumps to +/-MIN_SPEED, the next click down returns to 0.
// Raise MIN_SPEED if the train does not start reliably at the first click.
// 25 = measured minimum on the current train (10 and 20 did not move it reliably).
#define MIN_SPEED 25
#define MAX_SPEED 100
#define SPEED_INC 5
#define POTI_DELAY 250

// --- Keeping the target speed in sync with a train that stopped by itself ---------------------
// The DUPLO hub cuts the motor by itself when the train is blocked, lifted or pressed down. The
// speed sensor then reports 0. Such a report while a target speed is set resets the target to 0
// immediately, unless the controller itself caused a pass through 0 (direction change, stop and
// restart) and therefore expected it. A train that never starts sends no report at all; that case
// is caught by a start timeout.

// A drive command from standstill must produce a movement report within this time.
#define START_TIMEOUT_MS 1000
// An expected pass-through-0 report that never arrives is forgotten after this time.
#define EXPECT_ZERO_TIMEOUT_MS 1500

static bool speedSensorSeen = false;           ///< sensor has reported at least once (it works)
static bool expectZeroReport = false;          ///< next 0 report is caused by our own command
static unsigned long expectZeroSinceMs = 0;    ///< when the expectation was set
static unsigned long startPendingSinceMs = 0;  ///< 0 = not waiting for the train to start moving
static int lastDriveDirection = 0;             ///< sign of the last non-zero target (+1/-1)

/**
 * @brief Record a new non-zero target speed and what the speed sensor should report next.
 *
 * Call right before commanding a non-zero speed (dial, color bricks).
 *
 * @param newTarget Target speed being commanded (0 is ignored).
 * @param stopsFirst `true` if the command sequence stops the motor before driving on (BLUE/GREEN
 *        bricks), so a moving train passes through 0 even without a direction change.
 */
static void noteTargetSpeed(int newTarget, bool stopsFirst = false)
{
    if (newTarget == 0)
    {
        return;
    }

    const int newDirection = (newTarget > 0) ? 1 : -1;
    const bool moving = (detectedSpeed != 0);
    const bool reversing = (lastDriveDirection != 0 && newDirection != lastDriveDirection);
    lastDriveDirection = newDirection;

    if (moving && (reversing || stopsFirst))
    {
        // The train will slow down through 0 before driving on: ignore that one 0 report.
        expectZeroReport = true;
        expectZeroSinceMs = millis();
        startPendingSinceMs = 0;
    }
    else if (!moving)
    {
        // Starting from standstill: movement must be reported within START_TIMEOUT_MS.
        expectZeroReport = false;
        startPendingSinceMs = millis();
    }
    // Moving and continuing in the same direction: nothing special to expect.
}

/**
 * @brief Reset target speed and knob to 0 after the train stopped by itself.
 */
static void resetTargetBecauseTrainStopped(const char *reason)
{
    DEBUG_LOG("TrainController: Train %s - target %d reset to 0", reason, lastSpeed);
    duploHub.stopMotor();
    lastSpeed = 0;
    expectZeroReport = false;
    startPendingSinceMs = 0;
    rotaryEncoder.setEncoderValue(0);
    encoderValuePending.store(false);
}

/**
 * @brief Apply the latest encoder value as train speed (main loop).
 *
 * Speed commands are rate-limited to one per `POTI_DELAY`, but the newest value is kept pending
 * instead of being dropped, so the train always ends up at the speed the knob was turned to.
 * The stop zone below `MIN_SPEED` is skipped so every click changes something (see `MIN_SPEED`).
 */
static void processEncoder()
{
    static unsigned long lastSpeedCommandMs = 0;

    // Poll the hardware pulse counter; a changed value becomes the pending target.
    if (rotaryEncoder.update())
    {
        pendingEncoderValue.store(rotaryEncoder.getEncoderValue());
        encoderValuePending.store(true);
    }

    if (!encoderValuePending.load())
    {
        return;
    }

    // Turns while not ready are discarded and the knob is re-centered,
    // so the knob position and the train speed stay in sync.
    if (!hubReady)
    {
        encoderValuePending.store(false);
        rotaryEncoder.setEncoderValue(0);
        return;
    }

    lastUserActionTime = millis();

    if (millis() - lastSpeedCommandMs < POTI_DELAY)
    {
        return; // keep pending, applied on a later loop iteration
    }

    // Clear the flag before reading the value: a turn arriving in between sets it again.
    encoderValuePending.store(false);
    const long requestedSpeed = -pendingEncoderValue.load();
    int newSpeed = (int)requestedSpeed;

    // Skip the stop zone: leaving standstill jumps to +/-MIN_SPEED, coming down from a running
    // speed goes straight to 0. The knob is moved along so the next click continues from there.
    if (requestedSpeed != 0 && requestedSpeed > -MIN_SPEED && requestedSpeed < MIN_SPEED)
    {
        if (lastSpeed == 0)
        {
            newSpeed = (requestedSpeed > 0) ? MIN_SPEED : -MIN_SPEED;
        }
        else
        {
            newSpeed = 0;
        }
        rotaryEncoder.setEncoderValue(-newSpeed); // encoder value = -speed
    }

    if (newSpeed == lastSpeed)
    {
        return;
    }

    noteTargetSpeed(newSpeed);
    duploHub.setMotorSpeed(newSpeed);
    lastSpeed = newSpeed;
    lastSpeedCommandMs = millis();
    DEBUG_LOG("TrainController: Set motor speed to %d", lastSpeed);
}

// Function to process the responseQueue and print detected color
/**
 * @brief Callback for detected color events from the color sensor.
 * @param color The detected color (DuploEnums::DuploColor).
 */
static void detectedColorCb(DuploEnums::DuploColor color)
{
    static bool whiteLightEnabled = false;

    if (replay || recording)
    {
        DEBUG_LOG("TrainController: Color sensor callback ignored during recording and replay");
        return;
    }

    DEBUG_LOG("TrainController:  Detected Color: %d", color);

    detectedColor = color; // Store the detected color

    switch (color)
    {
    case DuploEnums::DuploColor::BLACK:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::BLACK);
        duploHub.setLedColor(DuploEnums::DuploColor::BLACK); // train light off; controller LED unchanged
        break;

    case DuploEnums::DuploColor::RED:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::RED);
        // Event burst like the other tiles.
        statusLed.setColor(CRGB::Red);
        statusLed.setBlinking(true, 250, 250, 3);
        duploHub.stopMotor();                                // Stop first, then sound and LED
        duploHub.playSound((DuploEnums::DuploSound::BRAKE)); // Play brake sound
        duploHub.setLedColor(DuploEnums::DuploColor::RED);
        lastSpeed = 0; // a later BLUE tile must not restart the train
        rotaryEncoder.setEncoderValue(0);
        encoderValuePending.store(false);
        break;

    case DuploEnums::DuploColor::GREEN:
    {
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::GREEN);
        const int reverseSpeed = (lastSpeed == 0) ? -40 : -lastSpeed;
        duploHub.setLedColor(DuploEnums::DuploColor::GREEN);
        statusLed.setColor(CRGB::Green);
        statusLed.setBlinking(true, 250, 250, 3);
        noteTargetSpeed(reverseSpeed, true); // stop + reverse passes through 0
        duploHub.stopMotor();
        duploHub.setMotorSpeed(reverseSpeed);
        lastSpeed = reverseSpeed;
        rotaryEncoder.setEncoderValue(-reverseSpeed); // keep knob in sync (encoder = -speed)
        encoderValuePending.store(false);
        break;
    }

    case DuploEnums::DuploColor::YELLOW:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::YELLOW);
        duploHub.setLedColor(DuploEnums::DuploColor::YELLOW);
        statusLed.setColor(CRGB::Yellow);
        statusLed.setBlinking(true, 250, 250, 3);
        duploHub.playSound((DuploEnums::DuploSound::HORN)); // Play horn sound
        break;

    case DuploEnums::DuploColor::BLUE:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::BLUE);
        duploHub.setLedColor(DuploEnums::DuploColor::BLUE);
        statusLed.setColor(CRGB::Blue);
        statusLed.setBlinking(true, 250, 250, 3);
        noteTargetSpeed(lastSpeed, true); // stop, water, continue: may pass through 0
        duploHub.stopMotor();
        duploHub.playSound((DuploEnums::DuploSound::WATER_REFILL)); // Play water refill sound
        duploHub.setMotorSpeed(lastSpeed);                          // Restore the last speed
        break;

    case DuploEnums::DuploColor::WHITE:
        DEBUG_LOG("TrainController:  Detected Color: %d", DuploEnums::DuploColor::WHITE);
        whiteLightEnabled = !whiteLightEnabled;
        duploHub.setLedColor(whiteLightEnabled ? DuploEnums::DuploColor::WHITE : DuploEnums::DuploColor::BLACK);
        statusLed.setColor(CRGB::White);
        statusLed.setBlinking(true, 250, 250, whiteLightEnabled ? 2 : 1);
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

/**
 * @brief Handle voltage sensor updates with rate limiting.
 * @param voltage Latest measured hub voltage in volts.
 */
static void detectedVoltageCb(float voltage)
{
    static unsigned long lastExec = 0;
    unsigned long now = millis();
    if (now - lastExec < CALC_VOLTAGE_DELAY)
    {
        return;
    }
    lastExec = now;
    DEBUG_LOG("TrainController:  Detected Voltage: %.2f V", voltage);
    detectedVoltage = voltage;
}

/**
 * @brief Callback for detected speed events from the speed sensor.
 *
 * The sensor only reports changes, so `detectedSpeed` holds the last reported value (non-zero
 * while driving at constant speed, 0 after the train stopped). A 0 report while a target speed is
 * set means the hub stopped the train by itself (blocked, lifted, pressed down) and resets the
 * target immediately, unless our own command caused it (see `noteTargetSpeed()`).
 *
 * @param speed The detected speed value (normalized, 0 = stopped).
 */
static void detectedSpeedCb(int speed)
{
    DEBUG_LOG("TrainController:  Detected Speed: %d", speed);
    detectedSpeed = speed;
    speedSensorSeen = true;

    if (speed != 0)
    {
        startPendingSinceMs = 0; // the train is moving
        return;
    }

    if (!hubReady || replay || lastSpeed == 0)
    {
        return;
    }

    if (expectZeroReport)
    {
        // Our own pass through 0 (direction change, stop and restart): now it has to start again.
        expectZeroReport = false;
        startPendingSinceMs = millis();
        return;
    }

    resetTargetBecauseTrainStopped("stopped by itself (blocked/lifted)");
}

/**
 * @brief Time-based part of the stop sync (main loop): a train that never starts sends no report.
 *
 * Resets the target if a drive command from standstill produced no movement report within
 * `START_TIMEOUT_MS` (only once the speed sensor has proven to work), and forgets an expected
 * pass-through-0 report that never arrived.
 */
static void checkTrainStalled()
{
    if (!hubReady || replay || lastSpeed == 0)
    {
        expectZeroReport = false;
        startPendingSinceMs = 0;
        return;
    }

    const unsigned long now = millis();

    if (expectZeroReport && now - expectZeroSinceMs >= EXPECT_ZERO_TIMEOUT_MS)
    {
        expectZeroReport = false; // the sensor skipped the 0; the train is moving on
    }

    if (startPendingSinceMs != 0 && speedSensorSeen && now - startPendingSinceMs >= START_TIMEOUT_MS)
    {
        resetTargetBecauseTrainStopped("did not start (blocked)");
    }
}

// Last known BLE address of the currently connected train hub.
String duploHubAddress = "";

/**
 * @brief Connection callbacks; run in the BLE task on core 0.
 *
 * They only flag the event. The actual handling runs in the main loop so that UI state and the
 * status LED are never touched from two cores at once.
 */
static void onHubConnectedFromBleTask()
{
    hubConnectedEventPending.store(true);
}

static void onHubDisconnectedFromBleTask()
{
    hubDisconnectedEventPending.store(true);
}

/**
 * @brief Arm the post-connection initialization after the hub is connected (main loop).
 *
 * Activates all required hub peripherals, refreshes the activity timer, and
 * restores the controller feedback state for normal operation.
 */
static void onHubConnected()
{
    DEBUG_LOG("TrainController: Hub instance connected, scheduling initialization...");
    lastUserActionTime = millis();
    hubReady = false;
    hubInitializationActive = true;
    hubInitializationStep = HubInitializationStep::RegisterCallbacks;
    hubInitializationStepStartedMs = 0;
    readyHeartbeatActive = false;
    // Blue "initializing" blinking is shown by updateModeIndication() while init runs.
}

// Callback function when hub disconnects
/**
 * @brief Callback function called when the hub is disconnected.
 */
static void onHubDisconnected()
{
    DEBUG_LOG("TrainController: Hub disconnected - stopping all operations");
    hubReady = false;
    hubInitializationActive = false;
    hubInitializationStep = HubInitializationStep::Idle;
    hubInitializationStepStartedMs = 0;
    readyHeartbeatActive = false;
    detectedVoltage = 0.0f; // no valid reading until the next connection reports one
    detectedSpeed = 0;      // train is assumed standing until the sensor reports movement
    // Replay only advances while connected; end it so it cannot keep the controller awake.
    if (replay)
    {
        duploHub.stopReplay();
        replay = false;
    }
    statusLed.setColor(CRGB::White);
    statusLed.setBlinking(true, 200, 300, 3);
}

/**
 * @brief Handle connection events flagged by the BLE task (main loop).
 *
 * Disconnect is handled first; a pending connect is only acted upon if the hub is still
 * connected, so a quick connect/disconnect or disconnect/reconnect sequence ends in the right state.
 */
static void processHubConnectionEvents()
{
    if (hubDisconnectedEventPending.exchange(false))
    {
        onHubDisconnected();
    }
    if (hubConnectedEventPending.exchange(false) && duploHub.isConnected())
    {
        onHubConnected();
    }
}

/**
 * @brief Progress hub setup without blocking the BLE worker task.
 */
static void updateHubInitialization()
{
    if (!hubInitializationActive)
    {
        return;
    }

    if (!duploHub.isConnected())
    {
        hubReady = false;
        hubInitializationActive = false;
        hubInitializationStep = HubInitializationStep::Idle;
        hubInitializationStepStartedMs = 0;
        return;
    }

    const unsigned long now = millis();
    const unsigned long requiredDelay = (hubInitializationStep == HubInitializationStep::WaitForReady)
                                            ? HUB_INIT_READY_SETTLE_MS
                                            : HUB_INIT_STEP_INTERVAL_MS;

    if (hubInitializationStepStartedMs != 0 && (now - hubInitializationStepStartedMs) < requiredDelay)
    {
        return;
    }

    // Advance exactly one initialization stage per loop iteration once its pacing delay elapsed.
    switch (hubInitializationStep)
    {
    case HubInitializationStep::RegisterCallbacks:
#ifdef DEBUG
        duploHub.listDevicePorts();
#endif
        duploHub.setDetectedColorCallback(detectedColorCb);
        duploHub.setDetectedSpeedCallback(detectedSpeedCb);
        duploHub.setDetectedVoltageCallback(detectedVoltageCb);
        DEBUG_LOG("TrainController: Hub callbacks registered");
        hubInitializationStep = HubInitializationStep::ActivateRgbLight;
        break;

    case HubInitializationStep::ActivateRgbLight:
        duploHub.activateRgbLight();
        hubInitializationStep = HubInitializationStep::ActivateBaseSpeaker;
        break;

    case HubInitializationStep::ActivateBaseSpeaker:
        duploHub.activateBaseSpeaker();
        hubInitializationStep = HubInitializationStep::ActivateColorSensor;
        break;

    case HubInitializationStep::ActivateColorSensor:
        duploHub.activateColorSensor();
        hubInitializationStep = HubInitializationStep::ActivateSpeedSensor;
        break;

    case HubInitializationStep::ActivateSpeedSensor:
        duploHub.activateSpeedSensor();
        hubInitializationStep = HubInitializationStep::ActivateVoltageSensor;
        break;

    case HubInitializationStep::ActivateVoltageSensor:
        duploHub.activateVoltageSensor();
        hubInitializationStep = HubInitializationStep::SetHubName;
        break;

    case HubInitializationStep::SetHubName:
        duploHub.setHubName(HUB_NAME);
        hubInitializationStep = HubInitializationStep::WaitForReady;
        break;

    case HubInitializationStep::WaitForReady:
        duploHubAddress = String(duploHub.getHubAddress().c_str());
        DEBUG_LOG("TrainController: Connected to hub: %s", duploHub.getHubName().c_str());
        DEBUG_LOG("TrainController: Hub address: %s", duploHubAddress.c_str());
        DEBUG_LOG("TrainController: Hub initialization complete - commands are ready");
        hubReady = true;
        hubInitializationActive = false;
        hubInitializationStep = HubInitializationStep::Idle;
        readyHeartbeatActive = true;
        lastReadyHeartbeatMs = now;
        statusLed.setColor(CRGB::Green);
        statusLed.setBlinking(true, 120, 180, 3);
        break;

    case HubInitializationStep::Idle:
    default:
        hubInitializationActive = false;
        break;
    }

    hubInitializationStepStartedMs = now;
}
/**
 * @brief Execute the next recorded command when its playback timestamp arrives.
 *
 * Pulls commands from the hub's replay buffer in chronological order. When the sequence
 * completes, the replay flag is reset.
 */
void replayNextCommand()
{
    const HubCommand *cmd = duploHub.getNextReplayCommand();
    if (cmd == nullptr)
    {
        // If replay is no longer active, stop replay state and update LED
        if (!duploHub.isReplayActive())
        {
            replay = false;
            DEBUG_LOG("TrainController: No more commands to replay, stopping replay");
        }
        else
        {
            DEBUG_LOG("TrainController: Not the time to replay command");
        }
        return;
    }
    DEBUG_LOG("TrainController: Replaying command type %d", cmd->type);

    // The recorded command is queued as-is; the BLE task executes it like a live command.
    duploHub.replayCommand(*cmd);

    // Keep speed bookkeeping and the knob in sync with what the train is doing.
    if (cmd->type == CMD_MOTOR_SPEED)
    {
        lastSpeed = cmd->data.motor.speed;
        rotaryEncoder.setEncoderValue(-lastSpeed);
    }
    else if (cmd->type == CMD_STOP_MOTOR)
    {
        lastSpeed = 0;
        rotaryEncoder.setEncoderValue(0);
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
    // USB-CDC serial: on battery (no USB host) output is never read. Never block on it:
    // a write waits at most 2 ms for buffer space (a reading host drains it much faster),
    // and Serial.flush() must not be used at all (in this Arduino-ESP32 version it waits
    // until a host has read everything, so without USB it hangs forever).
    Serial.setTxTimeoutMs(2);
    delay(1000);

    SerialMUTEX();
    DEBUG_LOG("TrainController (2 Core) : Starting up...");
    DEBUG_LOG("");

    printMemoryInfo();

    {
        const esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
        DEBUG_LOG("TrainController: Wakeup cause=%d (%s)", (int)wakeupCause, wakeupCauseToString(wakeupCause));
        // ESP_RST_TASK_WDT (6) indicates the BLE task watchdog reset the controller.
        DEBUG_LOG("TrainController: Reset reason=%d", (int)esp_reset_reason());
    }

    DEBUG_LOG("TrainController: ENCODER_BTN GPIO=%d, RTC-wake-capable=%s", ENCODER_BTN, encoderButtonIsRtcWakeCapable() ? "yes" : "no");
#endif

    configureEncoderButtonRuntime();
    encoderBtnIgnoreUntilRelease = (digitalRead(ENCODER_BTN) == LOW); // still held after wake

    // Initialize onboard RGB LED
    statusLed.begin();            // Initialize FastLED,,,,z
    statusLed.setBrightness(100); // Set brightness

    const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

    // If we woke from sleep, start green blink 5x as a visual confirmation.
    if (wakeCause == ESP_SLEEP_WAKEUP_EXT0 || wakeCause == ESP_SLEEP_WAKEUP_EXT1 || wakeCause == ESP_SLEEP_WAKEUP_GPIO)
    {
        startWakeupGreenBlink();
    }

    if (!wakeBlinkActive)
    {
        statusLed.setColor(CRGB::Yellow);      // Set color to yellow during init
        statusLed.setBlinking(true, 200, 300); // Blink: 200ms on, 300ms off
    }

    // setup push buttons and register callback
    guiButtons.addButtonRange(BUTTON_RECORD, 430, 875);  // Button 1: Record sequence (nominal ~670)
    guiButtons.addButtonRange(BUTTON_PLAY, 1115, 1505);  // Button 2: Play recorded sequence (nominal ~1320)
    guiButtons.addButtonRange(BUTTON_STOP, 1745, 2095);  // Button 3: Stop (nominal ~1930)
    guiButtons.addButtonRange(BUTTON_WATER, 2335, 2675); // Button 4: Make sound fill up water (nominal ~2500)
    guiButtons.addButtonRange(BUTTON_LIGHT, 2915, 3270); // Button 5: Change lights (nominal ~3090)
    guiButtons.addButtonRange(BUTTON_SOUND, 3510, 3870); // Button 6: Sound (nominal ~3690)
    guiButtons.registerButtonCallback(handleButtons);    // Register button callback

    // Initialize the rotary encoder (hardware pulse counter, polled in processEncoder()).
    rotaryEncoder.setBoundaries(-MAX_SPEED, MAX_SPEED);
    rotaryEncoder.setStepValue(SPEED_INC);
    rotaryEncoder.setDirection(DIAL_DIRECTION);
    rotaryEncoder.setEncoderValue(0);
    if (!rotaryEncoder.begin())
    {
        DEBUG_LOG("TrainController: ERROR - pulse counter setup for the encoder failed");
    }

    // Initialize DuploHub instance
    duploHub.init();
    delay(200);
    // Register connection event callbacks
    duploHub.setOnConnectedCallback(onHubConnectedFromBleTask);
    delay(200);
    duploHub.setOnDisconnectedCallback(onHubDisconnectedFromBleTask);
    delay(200); // Allow time for BLE task to initialize

    DEBUG_LOG("TrainController: Ready - BLE task running, waiting for hub connection...");
    if (!wakeBlinkActive)
    {
        statusLed.setOff();
    }

    // Start idle timer from boot so sleep works even when no user action is detected yet
    // (e.g. no hub connected -> button callbacks may early-return).
    lastUserActionTime = millis();
    DEBUG_LOG("TrainController: Idle timer started (maxIdleTime=%d ms)", maxIdleTime);
}

/**
 * @brief Trigger the green wakeup confirmation blink sequence.
 */
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

/**
 * @brief Check whether the train battery is known to be low.
 * @return `true` only if a valid voltage reading exists and it is below the threshold.
 *
 * `detectedVoltage == 0` means "no reading yet" (not connected or sensor not reported).
 */
static bool isLowVoltage()
{
    return hubReady && detectedVoltage > 0.0f && detectedVoltage < LOW_VOLTAGE_THRESHOLD;
}

/**
 * @brief Check whether the green ready-heartbeat may be displayed.
 * @return `true` when the controller is idle and no warning or mode indicator has priority.
 */
static bool shouldShowReadyHeartbeat()
{
    const bool noLowVoltageWarningPending = !isLowVoltage();

    return !wakeBlinkActive &&
           hubReady &&
           !recording &&
           !replay &&
           noLowVoltageWarningPending;
}

/**
 * @brief Update the periodic ready-heartbeat indicator.
 */
static void updateReadyHeartbeat()
{
    const unsigned long now = millis();
    const bool showHeartbeat = shouldShowReadyHeartbeat();

    if (!showHeartbeat)
    {
        readyHeartbeatActive = false;
        return;
    }

    if (!readyHeartbeatActive)
    {
        readyHeartbeatActive = true;
        lastReadyHeartbeatMs = now - READY_HEARTBEAT_INTERVAL_MS;
    }

    // Never cut off another burst (tile feedback, warnings); the heartbeat simply waits.
    if (now - lastReadyHeartbeatMs >= READY_HEARTBEAT_INTERVAL_MS && !statusLed.isBurstActive())
    {
        statusLed.setColor(CRGB::Green);
        statusLed.setBlinking(true, READY_HEARTBEAT_ON_MS, READY_HEARTBEAT_OFF_MS, READY_HEARTBEAT_BLINKS);
        lastReadyHeartbeatMs = now;
    }
}

/**
 * @brief Persistent controller modes shown on the status LED.
 *
 * Modes are continuous patterns; events (tiles, ready, warnings, disconnect) are finite bursts.
 * A burst temporarily overrides the mode, which is restored as soon as the burst is over.
 */
enum class LedMode
{
    Unknown,       ///< Nothing applied yet / must be re-applied
    None,          ///< No mode: LED off (ready heartbeat bursts are added separately)
    Searching,     ///< No train connected, scanning: short blue flash every ~1.5 s
    Initializing,  ///< Hub connected, initialization running: blue blinking
    Recording,     ///< Recording: violet blinking
    Replay         ///< Replay: yellow blinking
};

static LedMode shownLedMode = LedMode::Unknown;

/**
 * @brief Determine the mode the status LED should show, by priority.
 */
static LedMode currentLedMode()
{
    if (replay)
        return LedMode::Replay;
    if (recording)
        return LedMode::Recording;
    if (!duploHub.isConnected())
        return LedMode::Searching;
    if (hubInitializationActive)
        return LedMode::Initializing;
    return LedMode::None;
}

/**
 * @brief Show the current mode on the status LED whenever no burst is playing.
 *
 * Single place that drives mode patterns, so a mode indication (e.g. recording) can no
 * longer be lost after a disconnect, wake or tile burst.
 */
static void updateModeIndication()
{
    if (wakeBlinkActive || statusLed.isBurstActive())
    {
        shownLedMode = LedMode::Unknown; // re-apply once the burst is over
        return;
    }

    const LedMode mode = currentLedMode();
    if (mode == shownLedMode)
    {
        return;
    }
    shownLedMode = mode;

    switch (mode)
    {
    case LedMode::Replay:
        statusLed.setColor(CRGB::Yellow);
        statusLed.setBlinking(true, 500, 1000);
        break;
    case LedMode::Recording:
        statusLed.setColor(CRGB::Purple);
        statusLed.setBlinking(true, 500, 1000);
        break;
    case LedMode::Searching:
        statusLed.setColor(CRGB::Blue);
        statusLed.setBlinking(true, 100, 1400);
        break;
    case LedMode::Initializing:
        statusLed.setColor(CRGB::Blue);
        statusLed.setBlinking(true, 150, 250);
        break;
    case LedMode::None:
    case LedMode::Unknown:
    default:
        statusLed.setOff();
        break;
    }
}

/**
 * @brief Show a short red blink sequence before entering sleep.
 */
static void indicateEnteringSleep()
{
    // WS2812 LEDs keep their last color while the MCU sleeps, so explicitly show “sleep now”.
    statusLed.setBlinking(false);
    statusLed.setColor(CRGB::Red);
    statusLed.setBlinking(true, 120, 120, 2);

    const unsigned long until = millis() + 800;
    while ((long)(millis() - until) < 0)
    {
        statusLed.update();
        delay(10);
    }

    statusLed.setOff();
    statusLed.update();
    delay(50);
}

/**
 * @brief Convert an ESP sleep wake reason into a human-readable label.
 * @param cause Wake reason reported by `esp_sleep_get_wakeup_cause()`.
 * @return Constant string describing the wake source.
 */
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

/**
 * @brief Check whether the configured encoder button pin is RTC-capable.
 * @return `true` if the pin supports RTC wake functions.
 */
static bool encoderButtonIsRtcWakeCapable()
{
    const gpio_num_t wakeGpio = (gpio_num_t)ENCODER_BTN;
    return rtc_gpio_is_valid_gpio(wakeGpio);
}

/**
 * @brief Configure deep-sleep wakeup on the encoder button.
 * @param enableHold `true` to keep RTC pad settings latched across deep sleep.
 */
static void configureEncoderButtonWakeup(bool enableHold)
{
    const gpio_num_t wakeGpio = (gpio_num_t)ENCODER_BTN;

    if (!rtc_gpio_is_valid_gpio(wakeGpio))
    {
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
    if (err != ESP_OK)
    {
        DEBUG_LOG("TrainController: esp_sleep_enable_ext1_wakeup(GPIO%d, ALL_LOW) failed: %d", (int)wakeGpio, (int)err);
    }
    else
    {
        DEBUG_LOG("TrainController: Deep-sleep wake configured on GPIO%d (EXT1 ALL_LOW)", (int)wakeGpio);
    }

    if (enableHold)
    {
        rtc_gpio_hold_en(wakeGpio);
        gpio_deep_sleep_hold_en();
    }
}

/**
 * @brief Configure light-sleep wakeup for the encoder button.
 */
static void configureEncoderButtonLightSleepWakeup()
{
    const gpio_num_t wakeGpio = (gpio_num_t)ENCODER_BTN;
    pinMode(ENCODER_BTN, INPUT_PULLUP);

    // Light sleep GPIO wake works on any GPIO (RTC + digital IO).
    esp_err_t err = gpio_wakeup_enable(wakeGpio, GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK)
    {
        DEBUG_LOG("TrainController: gpio_wakeup_enable(%d) failed: %d", (int)wakeGpio, (int)err);
        return;
    }
    err = esp_sleep_enable_gpio_wakeup();
    if (err != ESP_OK)
    {
        DEBUG_LOG("TrainController: esp_sleep_enable_gpio_wakeup() failed: %d", (int)err);
    }
}

/**
 * @brief Configure the encoder button as a normal digital input for runtime use.
 */
static void configureEncoderButtonRuntime()
{
    const gpio_num_t pin = (gpio_num_t)ENCODER_BTN;
    if (rtc_gpio_is_valid_gpio(pin))
    {
        rtc_gpio_hold_dis(pin);
        rtc_gpio_deinit(pin); // hand the pad back to the digital GPIO matrix so digitalRead() works
    }
    pinMode(ENCODER_BTN, INPUT_PULLUP);
}

#define STATUS_LOG_INTERVAL_MS 300000UL     // 5 minutes
#define VOLTAGE_WARNING_INTERVAL_MS 30000UL // 30 seconds

/**
 * @brief Periodically logs system status and shows the low-voltage warning.
 * @param duploHub Reference to the DuploHub instance.
 */
void checkStatus(DuploHub &duploHub)
{
    static unsigned long lastStatusUpdate = 0;
    static unsigned long lastVoltageWarning = 0;

    if (millis() - lastStatusUpdate > STATUS_LOG_INTERVAL_MS)
    {
        DEBUG_LOG("TrainController Status - BLE Task: %s, Hub Connected: %s",
                  duploHub.isBLETaskRunning() ? "Running" : "Stopped",
                  duploHub.isConnected() ? "Yes" : "No");
        float cpuTemp = getCPUTemperature();
        DEBUG_LOG("CPU Temperature: %.2f °C", cpuTemp);
        lastStatusUpdate = millis();
    }

    // Low-voltage burst; the current mode (e.g. recording) is restored afterwards.
    if (isLowVoltage() && !wakeBlinkActive && !statusLed.isBurstActive() &&
        millis() - lastVoltageWarning > VOLTAGE_WARNING_INTERVAL_MS)
    {
        DEBUG_LOG("TrainController: Low voltage detected: %.2f V", detectedVoltage);
        statusLed.setColor(CRGB::Red);            // Set LED to red for low voltage
        statusLed.setBlinking(true, 100, 100, 5); // Blink red
        lastVoltageWarning = millis();
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
    DEBUG_LOG("TrainController: Entering sleep (idle timeout %d ms or dial held)", maxIdleTime);

    // Never leave a moving train behind: end record/replay, stop the motor and close the link.
    if (replay)
    {
        duploHub.stopReplay();
        replay = false;
    }
    if (recording)
    {
        duploHub.recordCommands(false);
        recording = false;
    }
    const bool linkShutDown = duploHub.shutdownLinkForSleep(1500);
    DEBUG_LOG("TrainController: Motor stop + disconnect before sleep %s", linkShutDown ? "confirmed" : "timed out");
    lastSpeed = 0;
    rotaryEncoder.setEncoderValue(0);
    encoderValuePending.store(false);
    // The BLE task suppresses the disconnect edge while sleeping, so reset the app state here
    // (matters for the light-sleep fallback, which returns into loop()).
    onHubDisconnected();

    // Re-apply wake configuration right before sleeping in case runtime code changed GPIO routing.
    configureEncoderButtonWakeup(true);

    DEBUG_LOG("TrainController: ENCODER_BTN level before sleep=%d", digitalRead(ENCODER_BTN));
    // No Serial.flush() here: without a USB host it waits forever (see setup()). The ~800 ms
    // sleep indication below gives pending log output time to go out when a host is connected.
    indicateEnteringSleep();

    if (encoderButtonIsRtcWakeCapable())
    {
        // Preferred path: deepest power saving with RTC-backed wake on the encoder button.
        esp_deep_sleep_start();
    }

    // Fallback: GPIO wake keeps the controller usable on boards without RTC IO on this pin.
    DEBUG_LOG("TrainController: Falling back to light sleep (GPIO wake)");
    configureEncoderButtonLightSleepWakeup();
    esp_light_sleep_start();
    DEBUG_LOG("TrainController: Woke from light sleep");
    configureEncoderButtonRuntime();
    encoderBtnIgnoreUntilRelease = true; // the wake press must not stop or re-trigger sleep
    duploHub.resumeAfterSleep();         // allow the BLE task to scan and reconnect again
    startWakeupGreenBlink();
    lastUserActionTime = millis();
}

/**
 * @brief Plain stop, shared by the STOP button and the dial press.
 *
 * Stops the motor, ends a running replay and re-centers the knob. Nothing is locked: turning
 * the dial drives again. A running recording continues and records the stop. Ignored while
 * the hub is not ready.
 */
static void normalStop()
{
    if (!hubReady)
    {
        return;
    }
    if (replay)
    {
        duploHub.stopReplay();
        replay = false;
    }
    duploHub.stopMotor();
    lastSpeed = 0;
    rotaryEncoder.setEncoderValue(0);
    encoderValuePending.store(false);
    DEBUG_LOG("TrainController: Stop (STOP button or dial)");
}

/**
 * @brief Handle the dial (encoder) button: press = normal stop, hold = sleep now.
 *
 * The stop happens immediately on press. Sleep is armed after `ENCODER_BTN_HOLD_TO_SLEEP_MS`
 * (fast red blinking as feedback) and entered on release; sleeping while the button is still
 * held would wake the controller again right away (wake is LOW-level triggered).
 */
static void processEncoderButton()
{
    static bool rawPressed = false;
    static unsigned long rawChangedMs = 0;
    static bool pressed = false;
    static unsigned long pressStartMs = 0;
    static bool sleepArmed = false;

    const unsigned long now = millis();
    const bool raw = (digitalRead(ENCODER_BTN) == LOW);
    if (raw != rawPressed)
    {
        rawPressed = raw;
        rawChangedMs = now;
    }
    if (now - rawChangedMs < ENCODER_BTN_DEBOUNCE_MS)
    {
        return; // level not stable yet
    }

    if (raw == pressed)
    {
        // No edge: check whether a held press reached the sleep threshold.
        if (pressed && !sleepArmed && !encoderBtnIgnoreUntilRelease &&
            now - pressStartMs >= ENCODER_BTN_HOLD_TO_SLEEP_MS)
        {
            sleepArmed = true;
            statusLed.setColor(CRGB::Red);
            statusLed.setBlinking(true, 80, 80, 125); // fast red until released (max ~20 s)
            DEBUG_LOG("TrainController: Dial held - sleep on release");
        }
        return;
    }

    pressed = raw;
    if (pressed)
    {
        pressStartMs = now;
        lastUserActionTime = now;
        if (!encoderBtnIgnoreUntilRelease)
        {
            normalStop();
        }
        return;
    }

    // Released
    if (encoderBtnIgnoreUntilRelease)
    {
        encoderBtnIgnoreUntilRelease = false;
        return;
    }
    if (sleepArmed)
    {
        sleepArmed = false;
        esp_goto_sleep();
    }
}

// main loop
/**
 * @brief Arduino main loop. Handles input, updates train state, and processes hub responses.
 */
void loop()
{
    statusLed.update(); // Handle blinking

    // Keep UI state in sync when recording auto-stops in DuploHub (e.g. fixed buffer full).
    if (recording && !duploHub.isRecordingCommandsEnabled())
    {
        recording = false;
        DEBUG_LOG("TrainController: Recording auto-stopped (record buffer full)");
    }

    // Finalize non-blocking wake blink and hand control back to the ready heartbeat.
    if (wakeBlinkActive && (long)(millis() - wakeBlinkEndMs) >= 0)
    {
        wakeBlinkActive = false;
        statusLed.setOff();
    }

    // Periodic idle countdown log for debugging sleep timing.
    static unsigned long lastIdleLog = 0;
    if (millis() - lastIdleLog >= 5000)
    {
        lastIdleLog = millis();
        if (lastUserActionTime == 0)
        {
            DEBUG_LOG("TrainController: Idle timer not started (lastUserActionTime=0)");
        }
        else
        {
            const unsigned long idleMs = millis() - lastUserActionTime;
            const long remaining = (long)maxIdleTime - (long)idleMs;
            DEBUG_LOG("TrainController: Idle=%lu ms, remaining=%ld ms", idleMs, remaining);
        }
    }

    // A running replay counts as activity, so sleep never cuts it off.
    if (replay)
    {
        lastUserActionTime = millis();
    }

    // Check for idle time to enter deep sleep
    if (lastUserActionTime != 0 && (millis() - lastUserActionTime) > maxIdleTime)
    {
        esp_goto_sleep();
    }

    processHubConnectionEvents();

    guiButtons.updateButtons();

    processEncoder();

    processEncoderButton();

    checkTrainStalled();

    updateHubInitialization();

    if (duploHub.isConnected())
    {
        // Handle replay commands if replay is active
        if (duploHub.isReplayActive())
        {
            replayNextCommand();
        }

        duploHub.processResponseQueue();
    }

    // Check Duplo Hub State
    checkStatus(duploHub);

    updateReadyHeartbeat();

    updateModeIndication();

    delay(20); // Add a small delay to reduce CPU load and prevent guru meditation error
} // End of loop
