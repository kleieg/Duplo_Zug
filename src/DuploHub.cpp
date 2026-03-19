
/**
 * @file DuploHub.cpp
 * @brief Implementation of the DuploHub class for LEGO DUPLO Train Hub control on ESP32.
 *
 * This file provides the implementation for the DuploHub class, which encapsulates all
 * functionality required to control a LEGO DUPLO Train Hub using the ESP32 platform.
 *
 * Features:
 * - Thread-safe command queuing for motor, LED, and sound control
 * - Optimized BLE task responsiveness and reduced latency
 * - Automatic connection management and recovery
 * - Dual-core FreeRTOS architecture for robust, responsive operation
 * - Modular callback system for sensors and events
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

#include "Arduino.h"
#include "DuploHub.h"
#include "LegoinoCommon.h"
#include "esp_timer.h"
#include "esp_err.h"

#define DEBUG // Enable debug logging
#include "debug.h"

// Initialize static instance pointer
DuploHub *DuploHub::instance = nullptr;

namespace
{
    constexpr uint64_t COLOR_SETTLE_DELAY_US = 250000; // 250 ms debounce window

    /**
     * @brief Build a timestamped command object for the BLE worker queue.
     * @param type Command type to assign to the new queue item.
     * @return Initialized `HubCommand` containing the current timestamp.
     */
    HubCommand createHubCommand(CommandType type)
    {
        HubCommand cmd{};
        cmd.type = type;
        cmd.timestamp = esp_timer_get_time();
        return cmd;
    }

    // Shared state used to debounce the color sensor callback before emitting events
    portMUX_TYPE colorTimerMutex = portMUX_INITIALIZER_UNLOCKED;
    esp_timer_handle_t colorStabilityTimer = nullptr;
    int pendingColor = -1;
    int pendingRawColor = -1;
    int lastStableColor = -1;
}

/**
 * @brief Create the color stability debounce timer on first use.
 *
 * The timer delays emission of color sensor events by `COLOR_SETTLE_DELAY_US`, ensuring that only
 * stable readings reach the main application. The function is safe to call repeatedly; it merely
 * returns once initialization has succeeded.
 *
 * @return True when the timer handle exists and can be armed.
 */
bool DuploHub::ensureColorTimerInitialized()
{
    if (colorStabilityTimer != nullptr)
    {
        return true;
    }

    DEBUG_LOG("Created color stability timer ");
    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = &DuploHub::colorStabilityTimerCallback;
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.name = "color_stability";
    timerArgs.skip_unhandled_events = true;

    esp_err_t err = esp_timer_create(&timerArgs, &colorStabilityTimer);
    if (err != ESP_OK)
    {
        DEBUG_LOG("ERROR: Failed to create color stability timer (err=%d)", err);
        colorStabilityTimer = nullptr;
        return false;
    }

    return true;
}

/**
 * @brief Default constructor for DuploHub.
 *
 * Initializes the DuploHub instance with the default motor port and resets all state variables.
 */
DuploHub::DuploHub() : motorPort((byte)DuploEnums::DuploTrainHubPort::MOTOR), wasConnected(false), hubMACAddress(""),
                       connectionState(false), connectingState(false),
                       connectionMutex(nullptr), commandQueue(nullptr), bleTaskHandle(nullptr),
                       onConnectedCallback(nullptr), onDisconnectedCallback(nullptr),
                       detectedColorCallback(nullptr), detectedSpeedCallback(nullptr),
                       detectedVoltageCallback(nullptr)
{
    instance = this; // Set static instance pointer
}

/**
 * @brief Constructor for DuploHub with a specific motor port.
 * @param port The port number to use for the motor.
 */
DuploHub::DuploHub(byte port) : motorPort(port), wasConnected(false), hubMACAddress(""),
                                connectionState(false), connectingState(false),
                                connectionMutex(nullptr), commandQueue(nullptr), bleTaskHandle(nullptr),
                                onConnectedCallback(nullptr), onDisconnectedCallback(nullptr),
                                detectedColorCallback(nullptr), detectedSpeedCallback(nullptr),
                                detectedVoltageCallback(nullptr)
{
    instance = this; // Set static instance pointer
}

/**
 * @brief Destructor for DuploHub.
 *
 * Cleans up FreeRTOS resources and resets the static instance pointer.
 */
DuploHub::~DuploHub()
{
    cleanupFreeRTOS();
    if (instance == this)
    {
        instance = nullptr; // Clear static instance pointer
    }
}

/**
 * @brief Initialize FreeRTOS objects (mutexes and queues) for thread-safe operation.
 *
 * Creates mutexes and queues for command and response handling.
 */
void DuploHub::initFreeRTOS()
{
    // Create mutex for thread-safe access to connection state
    connectionMutex = xSemaphoreCreateMutex();
    if (connectionMutex == nullptr)
    {
        DEBUG_LOG("ERROR: Failed to create connection mutex");
    }

    // Create command queue for thread-safe communication
    commandQueue = xQueueCreate(10, sizeof(HubCommand)); // Queue size: 10 commands
    if (commandQueue == nullptr)
    {
        DEBUG_LOG("ERROR: Failed to create command queue");
    }

    responseQueue = xQueueCreate(100, sizeof(HubResponse)); // Queue size: 10 response entries
    if (responseQueue == nullptr)
    {
        DEBUG_LOG("ERROR: Failed to create response queue");
    }

    DEBUG_LOG("DuploHub: FreeRTOS objects initialized");
    Serial.flush();
}

/**
 * @brief Clean up FreeRTOS objects (mutexes and queues).
 *
 * Deletes all created mutexes and queues and stops the BLE task if running.
 */
void DuploHub::cleanupFreeRTOS()
{
    // Stop BLE task if running
    stopBLETask();

    // Delete mutex
    if (connectionMutex != nullptr)
    {
        vSemaphoreDelete(connectionMutex);
        connectionMutex = nullptr;
    }

    // Delete command queue
    if (commandQueue != nullptr)
    {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
        DEBUG_LOG("DuploHub: Command queue cleaned up");
    }

    // Cleanup response queue
    if (responseQueue != nullptr)
    {
        vQueueDelete(responseQueue);
        responseQueue = nullptr;
        DEBUG_LOG("DuploHub: Response queue cleaned up");
    }

    DEBUG_LOG("DuploHub: FreeRTOS objects cleaned up");
    Serial.flush();
}

/**
 * @brief Update the connection state variables in a thread-safe manner.
 * @param connected True if the hub is connected.
 * @param connecting True if the hub is in the process of connecting.
 */
void DuploHub::updateConnectionState(bool connected, bool connecting)
{
    if (connectionMutex != nullptr)
    {
        xSemaphoreTake(connectionMutex, portMAX_DELAY);
        connectionState = connected;
        connectingState = connecting;
        xSemaphoreGive(connectionMutex);
    }
}

/**
 * @brief Initialize the DuploHub (thread-safe, can be called from main loop).
 *
 * Sets up FreeRTOS resources and starts the BLE task.
 */
void DuploHub::init()
{
    DEBUG_LOG("DuploHub: Initializing ...");
    initFreeRTOS();
    delay(500);
    startBLETask();
    delay(500);
    DEBUG_LOG("DuploHub: Initialization complete");
}

/**
 * @brief Initialize the DuploHub with a specific BLE address (not recommended).
 * @param address The BLE address to use for the hub.
 *
 * Note: BLE task handles initialization; this is for future use.
 */
void DuploHub::init(const std::string &address)
{
    DEBUG_LOG("WARNING: init(address) should not be called directly - BLE task handles initialization");
    hubMACAddress = address;
    init();
}

/**
 * @brief Check if the hub is connected (thread-safe).
 * @return True if connected, false otherwise.
 */
bool DuploHub::isConnected()
{
    if (connectionMutex != nullptr)
    {
        xSemaphoreTake(connectionMutex, portMAX_DELAY);
        bool state = connectionState;
        xSemaphoreGive(connectionMutex);
        return state;
    }
    return hub.isConnected(); // Fallback if mutex not initialized
}

/**
 * @brief Check if the hub is currently connecting (thread-safe).
 * @return True if connecting, false otherwise.
 */
bool DuploHub::isConnecting()
{
    if (connectionMutex != nullptr)
    {
        xSemaphoreTake(connectionMutex, portMAX_DELAY);
        bool state = connectingState;
        xSemaphoreGive(connectionMutex);
        return state;
    }
    return hub.isConnecting(); // Fallback if mutex not initialized
}

/**
 * @brief Check if the hub is disconnected (thread-safe).
 * @return True if disconnected, false otherwise.
 */
bool DuploHub::isDisconnected()
{
    return !isConnecting() && !isConnected();
}

/**
 * @brief Get the BLE address of the hub.
 * @return The hub's BLE address as a string.
 */
std::string DuploHub::getHubAddress()
{
    return hub.getHubAddress().toString();
}

/**
 * @brief Get the name of the hub.
 * @return The hub's name as a string.
 */
std::string DuploHub::getHubName()
{
    return hub.getHubName();
}

/**
 * @brief Enable or disable command recording
 * @param enable True to start recording commands, false to stop
 */
void DuploHub::recordCommands(bool enable)
{
    recordCommandsEnabled = enable;
    if (enable)
    {
        commandBufferCount = 0;
        DEBUG_LOG("DuploHub: Command recording ENABLED. Fixed buffer (%zu) cleared.", COMMAND_BUFFER_CAPACITY);
    }
    else
    {
        DEBUG_LOG("DuploHub: Command recording DISABLED. Buffer contains %zu commands.", commandBufferCount);
    }
}

/**
 * @brief Start replaying all previously recorded commands with normalized timestamps
 * The first command timestamp is set to 0 and all other timestamps are adjusted
 * to maintain relative timing differences
 */
void DuploHub::replayCommands()
{
    if (commandBufferCount == 0)
    {
        DEBUG_LOG("DuploHub: No commands to replay. Buffer is empty.");
        return;
    }

    if (replaying)
    {
        DEBUG_LOG("DuploHub: Replay already in progress. Stopping current replay first.");
        stopReplay();
    }

    recordCommandsEnabled = false;

    // Normalize timestamps so replay compares only elapsed time since replay start.
    if (commandBufferCount > 0)
    {
        originalStartTime = commandBuffer[0].timestamp;

        for (size_t i = 0; i < commandBufferCount; ++i)
        {
            commandBuffer[i].timestamp -= originalStartTime; // Normalize to start at 0
        }

        // Calculate total sequence duration (time of last command + some buffer)
        if (commandBufferCount > 1)
        {
            sequenceDuration = commandBuffer[commandBufferCount - 1].timestamp + 1000000; // Add 1 second buffer
        }
        else
        {
            sequenceDuration = 1000000; // Default 1 second for single command
        }

        DEBUG_LOG("DuploHub: Normalized %zu commands. Sequence duration: %lld us", commandBufferCount, sequenceDuration);
    }

    // Initialize replay state
    replaying = true;
    replayIndex = 0;
    replayCycle = 0;
    replayStartTime = esp_timer_get_time();

    DEBUG_LOG("DuploHub: Started replay of %zu recorded commands. Use getNextReplayCommand() to retrieve commands.", commandBufferCount);
}

/**
 * @brief Get the next command to replay if it's time to execute it
 * @return Pointer to the next command if ready, nullptr if no command ready or replay finished
 */
const HubCommand *DuploHub::getNextReplayCommand()
{
    if (!replaying || commandBufferCount == 0)
    {
        return nullptr;
    }

    uint64_t currentTime = esp_timer_get_time();
    uint64_t totalElapsedTime = currentTime - replayStartTime;

    // Only play the sequence ONCE
    if (replayIndex >= commandBufferCount)
    {
        // End of sequence reached, stop replay
        DEBUG_LOG("DuploHub: Replay finished after one sequence");
        stopReplay();
        return nullptr;
    }

    const HubCommand &nextCmd = commandBuffer[replayIndex];
    if (totalElapsedTime >= nextCmd.timestamp)
    {
        replayIndex++;
        DEBUG_LOG("DuploHub: Retrieved replay command %zu/%zu (type: %d) time: %llu us",
                  replayIndex, commandBufferCount, nextCmd.type, totalElapsedTime);
        return &nextCmd;
    }
    return nullptr; // Not yet time to play this command
}

/**
 * @brief Check if command replay is currently active
 * @return true if replay is in progress, false otherwise
 */
bool DuploHub::isReplayActive() const
{
    return replaying;
}

/**
 * @brief Report whether command recording is currently enabled.
 * @return `true` when outgoing commands are being captured for replay.
 */
bool DuploHub::isRecordingCommandsEnabled() const
{
    return recordCommandsEnabled;
}

/**
 * @brief Stop the current command replay
 */
void DuploHub::stopReplay()
{
    if (replaying)
    {
        DEBUG_LOG("DuploHub: Stopping replay at command %zu/%zu, cycle %d", replayIndex, commandBufferCount, replayCycle + 1);
        replaying = false;
        replayIndex = 0;
        replayCycle = 0;
        replayStartTime = 0;
        sequenceDuration = 0;
    }
    else
    {
        DEBUG_LOG("DuploHub: No active replay to stop.");
    }
}

/**
 * @brief Wrapper around xQueueSend that optionally records commands to commandBuffer
 * @param cmd The command to send and optionally record
 * @param timeout Timeout for queue send operation
 * @return Result of xQueueSend operation
 */
BaseType_t DuploHub::sendCommand(const HubCommand &cmd, TickType_t timeout)
{
    // Record the queue payload before sending so replay uses the exact original command sequence.
    if (recordCommandsEnabled)
    {
        if (commandBufferCount < COMMAND_BUFFER_CAPACITY)
        {
            commandBuffer[commandBufferCount++] = cmd;
            DEBUG_LOG("DuploHub: Command %d recorded to buffer. Buffer size: %zu/%zu", cmd.type, commandBufferCount, COMMAND_BUFFER_CAPACITY);

            if (commandBufferCount >= COMMAND_BUFFER_CAPACITY)
            {
                recordCommandsEnabled = false;
                DEBUG_LOG("DuploHub: Command buffer full (%zu). Recording stopped automatically.", COMMAND_BUFFER_CAPACITY);
            }
        }
        else
        {
            recordCommandsEnabled = false;
            DEBUG_LOG("DuploHub: Command buffer already full (%zu). Recording remains stopped.", COMMAND_BUFFER_CAPACITY);
        }
    }

    // Actual command execution is deferred to the BLE worker task.
    return xQueueSend(commandQueue, &cmd, timeout);
}

/**
 * @brief Set the hub's name (thread-safe).
 * @param name The new name for the hub.
 */
void DuploHub::setHubName(const char *name)
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_SET_HUB_NAME);
        strncpy(cmd.data.hubName.name, name, sizeof(cmd.data.hubName.name) - 1);
        cmd.data.hubName.name[sizeof(cmd.data.hubName.name) - 1] = '\0'; // Ensure null termination

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue hub name command");
        }
    }
    else
    {
        char hubName[strlen(name) + 1];
        strcpy(hubName, name);
        hub.setHubName(hubName);
    }
}

/**
 * @brief Set the LED color on the hub (thread-safe).
 * @param color The color to set (DuploEnums::DuploColor).
 */
void DuploHub::setLedColor(DuploEnums::DuploColor color)
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_SET_LED_COLOR);
        cmd.data.led.color = (DuploEnums::DuploColor)color;

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue LED color command");
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Command queue is not initialized");
    }
}

/**
 * @brief Set the motor speed (thread-safe).
 * @param speed The speed to set for the motor.
 */
void DuploHub::setMotorSpeed(int speed)
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_MOTOR_SPEED);
        cmd.data.motor.speed = speed;

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue motor speed command");
        }
    }
    else
    {
        hub.setBasicMotorSpeed(motorPort, speed);
        DEBUG_LOG("DuploHub: setBasicMotorSpeed executed at: %lu", millis());
    }
}

/**
 * @brief Stop the motor (thread-safe).
 */
void DuploHub::stopMotor()
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_STOP_MOTOR);

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue stop motor command");
        }
    }
    else
    {
        hub.stopBasicMotor(motorPort);
    }
}

/**
 * @brief Play a sound on the Duplo Hub (thread-safe).
 * @param soundId The ID of the sound to play.
 */
void DuploHub::playSound(int soundId)
{
    DEBUG_LOG("DuploHub: playSound called with soundId: %d", soundId);
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_PLAY_SOUND);
        cmd.data.sound.soundId = soundId;

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue play sound command");
        }
        else
        {
            DEBUG_LOG("DuploHub: Sound command queued successfully");
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Command queue not initialized - cannot play sound");
    }
}

/**
 * @brief Activate the RGB light on the hub (thread-safe).
 */
void DuploHub::activateRgbLight()
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_ACTIVATE_RGB_LIGHT);

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue activate RGB light command");
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Command queue is not initialized");
    }
}

/**
 * @brief Activate the base speaker on the hub (thread-safe).
 */
void DuploHub::activateBaseSpeaker()
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_ACTIVATE_BASE_SPEAKER);

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue activate base speaker command");
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Command queue is not initialized");
    }
}

/**
 * @brief Activate the color sensor on the hub (thread-safe).
 */
void DuploHub::activateColorSensor()
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_ACTIVATE_COLOR_SENSOR);

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue activate color sensor command");
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Command queue is not initialized");
    }
}

/**
 * @brief Activate the speed sensor on the hub (thread-safe).
 */
void DuploHub::activateSpeedSensor()
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_ACTIVATE_SPEED_SENSOR);

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue activate speed sensor command");
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Command queue is not initialized");
    }
}

/**
 * @brief Activate the voltage sensor on the hub (thread-safe).
 */
void DuploHub::activateVoltageSensor()
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd = createHubCommand(CommandType::CMD_ACTIVATE_VOLTAGE_SENSOR);

        if (sendCommand(cmd, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue activate voltage sensor command");
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Command queue is not initialized");
    }
}

/**
 * @brief Static callback for color sensor events (used by activatePortDevice).
 * @param hub Pointer to the hub instance.
 * @param portNumber The port number of the sensor.
 * @param deviceType The type of device triggering the callback.
 * @param pData Pointer to the sensor data.
 */
void DuploHub::staticColorSensorCallback(void *hub, byte portNumber, DeviceType deviceType, uint8_t *pData)
{
    myLegoHub *myHub = (myLegoHub *)hub;
    (void)portNumber;

    if (deviceType != DeviceType::DUPLO_TRAIN_BASE_COLOR_SENSOR)
    {
        DEBUG_LOG("ERROR: Unsupported device type for color sensor callback");
        return;
    }

    int detectedColor = myHub->parseColor(pData);

    if (detectedColor < DuploEnums::DuploColor::BLACK || detectedColor > DuploEnums::DuploColor::WHITE)
    {
        return; // Ignore out-of-range values entirely
    }

    if (!ensureColorTimerInitialized())
    {
        // Fallback: emit immediately if timer setup failed
        if (instance != nullptr && instance->responseQueue != nullptr)
        {
            HubResponse response;
            response.type = ResponseType::Detected_Color;
            response.data.colorResponse.detectedColor = (DuploEnums::DuploColor)detectedColor;

            if (xQueueSend(instance->responseQueue, &response, pdMS_TO_TICKS(100)) != pdTRUE)
            {
                DEBUG_LOG("WARNING: Failed to queue color sensor response from static callback (timer init failure)");
            }
        }
        else
        {
            DEBUG_LOG("ERROR: Cannot access response queue from static color callback (timer init failure)");
        }
        return;
    }

    bool timerActive = esp_timer_is_active(colorStabilityTimer);
    bool restartTimer = false;

    // Store latest candidate color and only restart settling timer when something changed
    portENTER_CRITICAL(&colorTimerMutex);
    if (pendingColor != detectedColor)
    {
        pendingColor = detectedColor;
        pendingRawColor = pData[4];
        restartTimer = true;
    }
    else if (!timerActive)
    {
        // Timer already expired; ensure we arm it again to confirm stability
        pendingRawColor = pData[4];
        restartTimer = true;
    }
    portEXIT_CRITICAL(&colorTimerMutex);

    if (restartTimer)
    {
        esp_err_t stopErr = esp_timer_stop(colorStabilityTimer);
        if (stopErr != ESP_OK && stopErr != ESP_ERR_INVALID_STATE)
        {
            DEBUG_LOG("WARNING: Failed to stop color stability timer (err=%d)", stopErr);
        }

        esp_err_t startErr = esp_timer_start_once(colorStabilityTimer, COLOR_SETTLE_DELAY_US);
        if (startErr != ESP_OK)
        {
            DEBUG_LOG("ERROR: Failed to start color stability timer (err=%d)", startErr);
        }
    }
}
/**
 * @brief Timer hook that emits a stabilized color reading to the response queue.
 *
 * Invoked on the ESP timer task context after the debounce interval elapses. If the color sample
 * remains unchanged since the last stable notification, no queue message is produced.
 *
 * @param arg Unused. Present to satisfy the timer callback signature.
 */
void DuploHub::colorStabilityTimerCallback(void *arg)
{
    (void)arg;

    int colorToEmit = -1;
    int rawColorToEmit = -1;
    portENTER_CRITICAL(&colorTimerMutex);
    if (pendingColor >= DuploEnums::DuploColor::BLACK && pendingColor <= DuploEnums::DuploColor::WHITE)
    {
        if (pendingColor != lastStableColor)
        {
            colorToEmit = pendingColor;
            rawColorToEmit = pendingRawColor;
            lastStableColor = pendingColor;
        }
    }

    pendingColor = -1;
    pendingRawColor = -1;
    portEXIT_CRITICAL(&colorTimerMutex);

    if (colorToEmit == -1)
    {
        return;
    }

    DEBUG_LOG("(%ld) Static Color Callback - Stable Color: raw=%d parsed=%d", millis(), rawColorToEmit, colorToEmit);

    if (instance != nullptr && instance->responseQueue != nullptr)
    {
        HubResponse response;
        response.type = ResponseType::Detected_Color;
        response.data.colorResponse.detectedColor = (DuploEnums::DuploColor)colorToEmit;

        if (xQueueSend(instance->responseQueue, &response, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            DEBUG_LOG("WARNING: Failed to queue color sensor response from stability timer");
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Cannot access response queue from color stability timer");
    }
}

/**
 * @brief Static callback for speed sensor events (used by activatePortDevice).
 * @param hub Pointer to the hub instance.
 * @param portNumber The port number of the sensor.
 * @param deviceType The type of device triggering the callback.
 * @param pData Pointer to the sensor data.
 */
void DuploHub::staticSpeedSensorCallback(void *hub, byte portNumber, DeviceType deviceType, uint8_t *pData)
{
    static int lastSpeed = -1;

    myLegoHub *myHub = (myLegoHub *)hub;
    if (deviceType == DeviceType::DUPLO_TRAIN_BASE_SPEEDOMETER)
    {
        int detectedSpeed = myHub->parseSpeedometer(pData);
        if (abs(lastSpeed - detectedSpeed) > SPEED_THRESHOLD)
        { // Only report significant speed changes
            DEBUG_LOG("(%ld) Static Speed Callback - Last Speed: %d , Detected Speed: %d", millis(), lastSpeed, detectedSpeed);
            lastSpeed = detectedSpeed;

            // Access the singleton instance to get the response queue
            if (instance != nullptr && instance->responseQueue != nullptr)
            {
                HubResponse response;
                response.type = ResponseType::Detected_Speed;
                response.data.speedResponse.detectedSpeed = myHub->MapSpeedometer(detectedSpeed);

                if (xQueueSend(instance->responseQueue, &response, pdMS_TO_TICKS(100)) != pdTRUE)
                {
                    DEBUG_LOG("WARNING: Failed to queue speed sensor response from static callback");
                }
            }
            else
            {
                DEBUG_LOG("ERROR: Cannot access response queue from static speed callback");
            }
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Unsupported device type for speed sensor callback");
    }
}

/**
 * @brief Static callback for voltage sensor events (used by activatePortDevice).
 * @param hub Pointer to the hub instance.
 * @param portNumber The port number of the sensor.
 * @param deviceType The type of device triggering the callback.
 * @param pData Pointer to the sensor data.
 */
void DuploHub::staticVoltageSensorCallback(void *hub, byte portNumber, DeviceType deviceType, uint8_t *pData)
{
    static float lastVoltage = -1.0f;

    myLegoHub *myHub = (myLegoHub *)hub;
    if (deviceType == DeviceType::VOLTAGE_SENSOR)
    {
        float detectedVoltage = (float)myHub->parseVoltageSensor(pData);
        if (abs(lastVoltage - detectedVoltage) > VOLTAGE_THRESHOLD)
        { // Only report significant voltage changes
            DEBUG_LOG("(%ld) Static Voltage Callback - Last Voltage: %.2f , Detected Voltage: %.2f", millis(), lastVoltage, detectedVoltage);
            lastVoltage = detectedVoltage;

            // Access the singleton instance to get the response queue
            if (instance != nullptr && instance->responseQueue != nullptr)
            {
                HubResponse response;
                response.type = ResponseType::Detected_Voltage;
                response.data.voltageResponse.detectedVoltage = detectedVoltage;

                if (xQueueSend(instance->responseQueue, &response, pdMS_TO_TICKS(100)) != pdTRUE)
                {
                    DEBUG_LOG("WARNING: Failed to queue voltage sensor response from static callback");
                }
            }
            else
            {
                DEBUG_LOG("ERROR: Cannot access response queue from static voltage callback");
            }
        }
    }
    else
    {
        DEBUG_LOG("ERROR: Unsupported device type for voltage sensor callback");
    }
}

/**
 * @brief Set the motor port for the hub.
 * @param port The port number to assign to the motor.
 */
void DuploHub::setMotorPort(byte port)
{
    motorPort = port;
}

/**
 * @brief Get the current motor port number.
 * @return The port number assigned to the motor.
 */

byte DuploHub::getMotorPort()
{
    return motorPort;
}

/**
 * @brief List all device ports for the Duplo Train hub.
 */
void DuploHub::listDevicePorts()
{
    DEBUG_LOG("Listing Duplo Train device ports:");
    DEBUG_LOG("Motor Port: %d", (int)hub.getPortForDeviceType((byte)DuploEnums::DuploTrainDeviceType::DUPLO_TRAIN_BASE_MOTOR));
    DEBUG_LOG("Speaker Port: %d", (int)hub.getPortForDeviceType((byte)DuploEnums::DuploTrainDeviceType::DUPLO_TRAIN_BASE_SPEAKER));
    DEBUG_LOG("Color Sensor Port: %d", (int)hub.getPortForDeviceType((byte)DuploEnums::DuploTrainDeviceType::DUPLO_TRAIN_BASE_COLOR_SENSOR));
    DEBUG_LOG("Speed Sensor Port: %d", (int)hub.getPortForDeviceType((byte)DuploEnums::DuploTrainDeviceType::DUPLO_TRAIN_BASE_SPEEDOMETER));
    DEBUG_LOG("Voltage Port: %d", (int)hub.getPortForDeviceType((byte)DuploEnums::DuploTrainDeviceType::VOLTAGE_SENSOR));
}

/**
 * @brief Set the callback function to be called when the hub connects.
 * @param callback The function to call on connection.
 */
void DuploHub::setOnConnectedCallback(ConnectionCallback callback)
{
    onConnectedCallback = callback;
    DEBUG_LOG("DuploHub: OnConnected callback set");
}

/**
 * @brief Set the callback function to be called when the hub disconnects.
 * @param callback The function to call on disconnection.
 */
void DuploHub::setOnDisconnectedCallback(ConnectionCallback callback)
{
    onDisconnectedCallback = callback;
    DEBUG_LOG("DuploHub: OnDisconnected callback set");
}

/**
 * @brief Set the callback function for detected color events.
 * @param callback The function to call when a color is detected.
 */
void DuploHub::setDetectedColorCallback(DetectedColorCallback callback)
{
    detectedColorCallback = callback;
    DEBUG_LOG("DuploHub: DetectedColor callback set");
}

/**
 * @brief Set the callback function for detected speed events.
 * @param callback The function to call when a speed is detected.
 */
void DuploHub::setDetectedSpeedCallback(DetectedSpeedCallback callback)
{
    detectedSpeedCallback = callback;
    DEBUG_LOG("DuploHub: DetectedSpeed callback set");
}

/**
 * @brief Set the callback function for detected voltage events.
 * @param callback The function to call when a voltage is detected.
 */
void DuploHub::setDetectedVoltageCallback(DetectedVoltageCallback callback)
{
    detectedVoltageCallback = callback;
    DEBUG_LOG("DuploHub: DetectedVoltage callback set");
}

/**
 * @brief Start the BLE task on core 0 (if not already running).
 */
void DuploHub::startBLETask()
{
    if (bleTaskHandle == nullptr)
    {
        BaseType_t result = xTaskCreatePinnedToCore(
            bleTaskWrapper, // Task function
            "BLE_Task",     // Name
            4096,           // Stack size (4KB)
            this,           // Parameter (this instance)
            2,              // Priority (higher than main loop)
            &bleTaskHandle, // Task handle
            0               // CPU core 0
        );

        if (result == pdPASS)
        {
            DEBUG_LOG("DuploHub: BLE task started successfully");
        }
        else
        {
            DEBUG_LOG("ERROR: Failed to start BLE task");
            bleTaskHandle = nullptr;
        }
    }
    else
    {
        DEBUG_LOG("WARNING: BLE task is already running");
    }
}

/**
 * @brief Stop the BLE task if it is running.
 */
void DuploHub::stopBLETask()
{
    if (bleTaskHandle != nullptr)
    {
        vTaskDelete(bleTaskHandle);
        bleTaskHandle = nullptr;
        DEBUG_LOG("DuploHub: BLE task stopped");
    }
}

/**
 * @brief Check if the BLE task is currently running.
 * @return True if running, false otherwise.
 */
bool DuploHub::isBLETaskRunning()
{
    return bleTaskHandle != nullptr;
}

/**
 * @brief Ensure the BLE task is running; restart if not (auto-recovery).
 */
void DuploHub::ensureBLETaskRunning()
{
    if (!isBLETaskRunning())
    {
        DEBUG_LOG("DuploHub: BLE task not running, attempting to restart...");
        delay(200);
        startBLETask();
    }
}

/**
 * @brief Static wrapper for the BLE task (required for FreeRTOS).
 * @param parameter Pointer to the DuploHub instance.
 */
void DuploHub::bleTaskWrapper(void *parameter)
{
    DuploHub *instance = static_cast<DuploHub *>(parameter);
    instance->bleTaskFunction();
}

/**
 * @brief BLE task function that handles BLE operations and command processing.
 */
void DuploHub::bleTaskFunction()
{
    DEBUG_LOG("BLE Task: Started successfully");
    Serial.flush();

    unsigned long lastConnectionCheck = 0;
    const unsigned long CONNECTION_CHECK_INTERVAL = 1000; // Check connection every 1 second

    while (true)
    {
        unsigned long currentTime = millis();

        // Handle BLE operations at regular intervals
        if (currentTime - lastConnectionCheck >= CONNECTION_CHECK_INTERVAL)
        {
            updateBLE();
            lastConnectionCheck = currentTime;
        }

        // Command handling runs more often than reconnect logic to keep UI latency low.
        processCommandQueue();

        // Small delay to prevent task from hogging CPU
        vTaskDelay(pdMS_TO_TICKS(50)); // 50ms delay for good responsiveness
    }
}

/**
 * @brief Perform BLE operations, connection management, and callback invocation (runs in BLE task).
 */
void DuploHub::updateBLE()
{
    static bool wasEverConnected = false; // Track if we ever had a connection (BLE task context)
    static bool lastConnected = false;    // Track last connection state for callback
    static unsigned long lastConnectAttemptLog = 0;
    static unsigned long lastScanStartMs = 0;
    static bool scanAttemptActive = false;
    const unsigned long CONNECT_ATTEMPT_LOG_INTERVAL_MS = 10000;
    const uint32_t RECONNECT_SCAN_DURATION_S = 2;
    const unsigned long SCAN_RETRY_INTERVAL_MS = (RECONNECT_SCAN_DURATION_S * 1000UL) + 250UL;

    // Get current BLE states from Lpf2Hub
    bool hubConnected = hub.isConnected();
    bool hubConnecting = hub.isConnecting();
    bool hubDisconnected = !hubConnecting && !hubConnected;

    // Update thread-safe state variables
    updateConnectionState(hubConnected, hubConnecting);

    // Discovery is throttled so scans are not restarted on every BLE loop iteration.
    if (hubDisconnected)
    {
        const unsigned long now = millis();
        if (now - lastConnectAttemptLog >= CONNECT_ATTEMPT_LOG_INTERVAL_MS)
        {
            if (!wasEverConnected)
            {
                DEBUG_LOG("BLE Task: Attempting initial connection to hub...");
            }
            else
            {
                DEBUG_LOG("BLE Task: Attempting to reconnect to hub...");
            }
            lastConnectAttemptLog = now;
        }

        if (!scanAttemptActive || (now - lastScanStartMs) >= SCAN_RETRY_INTERVAL_MS)
        {
            if (hubMACAddress != "")
            {
                hub.init(hubMACAddress, RECONNECT_SCAN_DURATION_S);
                DEBUG_LOG("BLE Task: Using preferred hub address: %s (scan=%lus)", hubMACAddress.c_str(), RECONNECT_SCAN_DURATION_S);
            }
            else
            {
                hub.init(RECONNECT_SCAN_DURATION_S);
                DEBUG_LOG("BLE Task: Starting discovery scan (scan=%lus)", RECONNECT_SCAN_DURATION_S);
            }

            lastScanStartMs = now;
            scanAttemptActive = true;
        }
    }

    // Once Legoino reports a pending connection, try to complete it immediately.
    if (hubConnecting)
    {
        scanAttemptActive = false;
        hub.connectHub();
        if (hub.isConnected())
        {
            wasEverConnected = true;
            DEBUG_LOG("BLE Task: Connected to HUB");
            Serial.flush();
            DEBUG_LOG("BLE Task: Hub address: %s", hub.getHubAddress().toString().c_str());
            DEBUG_LOG("BLE Task: Hub name: %s", hub.getHubName().c_str());
        }
        else
        {
            DEBUG_LOG("BLE Task: Failed to connect to HUB");
            Serial.flush();
        }
    }

    // The application only sees connection edges, not every periodic BLE status poll.
    if (hubConnected && !lastConnected)
    {
        scanAttemptActive = false;
        DEBUG_LOG("BLE Task: Connection state changed: CONNECTED");
        clearQueues();
        if (onConnectedCallback != nullptr)
        {
            DEBUG_LOG("DuploHub: Calling onConnectedCallback from BLE task...");
            onConnectedCallback();
            DEBUG_LOG("DuploHub: onConnectedCallback finished");
        }
        else
        {
            DEBUG_LOG("DuploHub: onConnectedCallback is nullptr");
        }
    }
    else if (!hubConnected && lastConnected)
    {
        scanAttemptActive = false;
        lastScanStartMs = 0;
        clearQueues();
        DEBUG_LOG("BLE Task: Connection state changed: DISCONNECTED");
        if (onDisconnectedCallback != nullptr)
        {
            DEBUG_LOG("DuploHub: Calling onDisconnectedCallback from BLE task...");
            onDisconnectedCallback();
            DEBUG_LOG("DuploHub: onDisconnectedCallback finished");
        }
        else
        {
            DEBUG_LOG("DuploHub: onDisconnectedCallback is nullptr");
        }
    }
    lastConnected = hubConnected;
}

/**
 * @brief Process all commands from the command queue (runs in BLE task).
 */
void DuploHub::processCommandQueue()
{
    if (commandQueue == nullptr)
        return;

    HubCommand cmd;

    // Process all available commands (non-blocking)
    while (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE)
    {
        // Commands are dropped while disconnected because replaying stale writes would be unsafe.
        if (!hub.isConnected())
        {
            DEBUG_LOG("BLE Task: Skipping command - hub not connected");
            continue;
        }

        switch (cmd.type)
        {
        case CommandType::CMD_MOTOR_SPEED:
            DEBUG_LOG("BLE Task: Setting motor speed to %d", cmd.data.motor.speed);
            hub.setBasicMotorSpeed((byte)DuploEnums::DuploTrainHubPort::MOTOR, cmd.data.motor.speed);
            delay(20); // Short pacing delay so back-to-back protocol writes remain reliable.
            DEBUG_LOG("DuploHub: setBasicMotorSpeed completed at: %lu", millis());
            break;

        case CommandType::CMD_STOP_MOTOR:
            DEBUG_LOG("BLE Task: Stopping motor");
            hub.stopBasicMotor((byte)DuploEnums::DuploTrainHubPort::MOTOR);
            delay(20); // Short pacing delay so back-to-back protocol writes remain reliable.
            DEBUG_LOG("DuploHub: stopBasicMotor completed at: %lu", millis());
            break;

        case CommandType::CMD_SET_LED_COLOR:
            DEBUG_LOG("BLE Task: Setting LED color to %d", cmd.data.led.color);
            hub.setLedColor((Color)cmd.data.led.color);
            delay(200); // Property updates need a longer settle time than motor writes.
            DEBUG_LOG("DuploHub: setLEDColor completed at: %lu", millis());
            break;

        case CommandType::CMD_SET_HUB_NAME:
            DEBUG_LOG("BLE Task: Setting hub name to %s", cmd.data.hubName.name);
            hub.setHubName(cmd.data.hubName.name);
            delay(200); // Property updates need a longer settle time than motor writes.
            DEBUG_LOG("DuploHub: setHubName completed at: %lu", millis());
            break;

        case CommandType::CMD_PLAY_SOUND:
            DEBUG_LOG("BLE Task: Playing sound with ID %d", cmd.data.sound.soundId);
            hub.playSound((byte)cmd.data.sound.soundId);
            delay(200); // Property updates need a longer settle time than motor writes.
            DEBUG_LOG("DuploHub: playSound completed at: %lu", millis());
            break;

        case CommandType::CMD_ACTIVATE_RGB_LIGHT:
            DEBUG_LOG("BLE Task: Activating RGB light");
            hub.activateRgbLight();
            delay(200); // Activation commands are serialized to avoid missed port setup.
            DEBUG_LOG("DuploHub: activateRgbLight completed");
            break;

        case CommandType::CMD_ACTIVATE_BASE_SPEAKER:
            DEBUG_LOG("BLE Task: Activating base speaker");
            hub.activateBaseSpeaker();
            delay(200); // Activation commands are serialized to avoid missed port setup.
            DEBUG_LOG("DuploHub: activateBaseSpeaker completed");
            break;

        case CommandType::CMD_ACTIVATE_COLOR_SENSOR:
            DEBUG_LOG("BLE Task: Activating color sensor");
            hub.activatePortDevice((byte)DuploEnums::DuploTrainHubPort::COLOR, staticColorSensorCallback);
            delay(200);
            DEBUG_LOG("DuploHub: setColorSensorCallback completed");
            break;

        case CommandType::CMD_ACTIVATE_SPEED_SENSOR:
            DEBUG_LOG("BLE Task: Activating speed sensor");
            hub.activatePortDevice((byte)DuploEnums::DuploTrainHubPort::SPEEDOMETER, staticSpeedSensorCallback);
            delay(200);
            DEBUG_LOG("DuploHub: setSpeedSensorCallback completed");
            break;
        case CommandType::CMD_ACTIVATE_VOLTAGE_SENSOR:
            DEBUG_LOG("BLE Task: Activating voltage sensor");
            hub.activatePortDevice((byte)DuploEnums::DuploTrainHubPort::VOLTAGE, staticVoltageSensorCallback);
            delay(200);
            DEBUG_LOG("DuploHub: setVoltageSensorCallback completed");
            break;

        default:
            DEBUG_LOG("BLE Task: Unknown command type");
            break;
        }
    }
}

/**
 * @brief Process all responses from the response queue and invoke registered callbacks.
 */
void DuploHub::processResponseQueue()
{
    if (responseQueue == nullptr)
        return;

    HubResponse response;

    // Process all available responses (non-blocking)
    while (xQueueReceive(responseQueue, &response, 0) == pdTRUE)
    {
        switch (response.type)
        {
        case Detected_Color:
            DEBUG_LOG("Detected Color: %d", response.data.colorResponse.detectedColor);
            if (detectedColorCallback != nullptr)
            {
                detectedColorCallback(response.data.colorResponse.detectedColor);
                DEBUG_LOG("Detected Color Callback executed");
            }
            break;

        case Detected_Speed:
            DEBUG_LOG("Detected Speed: %d", response.data.speedResponse.detectedSpeed);
            if (detectedSpeedCallback != nullptr)
            {
                detectedSpeedCallback(response.data.speedResponse.detectedSpeed);
                DEBUG_LOG("Detected Speed Callback executed");
            }
            break;

        case Detected_Voltage:
            DEBUG_LOG("Detected Voltage: %.2f V", response.data.voltageResponse.detectedVoltage);
            if (detectedVoltageCallback != nullptr)
            {
                detectedVoltageCallback(response.data.voltageResponse.detectedVoltage);
                DEBUG_LOG("Detected Voltage Callback executed");
            }
            break;

        default:
            DEBUG_LOG("Unknown response type");
            break;
        }
    }
}

/**
 * @brief Main update method; ensures BLE task is running and updates connection state.
 */
void DuploHub::update()
{
    // Ensure BLE task is running (auto-recovery)
    static unsigned long lastTaskCheck = 0;
    if (millis() - lastTaskCheck > 5000)
    { // Check every 5 seconds
        ensureBLETaskRunning();
        lastTaskCheck = millis();
    }
    // No connection state change or callback logic here anymore; handled in BLE task
    wasConnected = isConnected();
}

/**
 * @brief Clear all pending commands and responses in the command and response queues.
 */
void DuploHub::clearQueues()
{
    if (commandQueue != nullptr)
    {
        HubCommand cmd;
        while (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE)
        {
            // Drop stale commands when the hub state changes.
        }
    }
    if (responseQueue != nullptr)
    {
        HubResponse response;
        while (xQueueReceive(responseQueue, &response, 0) == pdTRUE)
        {
            // Drop stale sensor notifications from a previous connection state.
        }
    }
}
