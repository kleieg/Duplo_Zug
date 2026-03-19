/**
 * @file DuploHub.h
 * @brief High-level controller for the LEGO DUPLO Train Hub running on ESP32.
 *
 * Declares the `DuploHub` class, which wraps Legoino primitives in a thread-safe, event-driven
 * interface tailored for the dual-core ESP32 environment. Responsibilities include BLE lifecycle
 * management, command queuing, sensor event processing, and record/replay support.
 *
 * @author Ralf Zühlsdorff
 * @date 2025
 *
 * @copyright
 * MIT License
 */

#ifndef DUPLO_HUB_H
#define DUPLO_HUB_H

#include <array>
#include "myLegoHub.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

/**
 * @brief Commands exchanged between the application core and the BLE worker task.
 */
enum CommandType
{
    CMD_ACTIVATE_RGB_LIGHT,
    CMD_ACTIVATE_BASE_SPEAKER,
    CMD_MOTOR_SPEED,
    CMD_STOP_MOTOR,
    CMD_SET_LED_COLOR,
    CMD_SET_HUB_NAME,
    CMD_PLAY_SOUND,
    CMD_ACTIVATE_COLOR_SENSOR,
    CMD_ACTIVATE_SPEED_SENSOR,
    CMD_ACTIVATE_VOLTAGE_SENSOR
};

/**
 * @brief POD payload stored in `commandQueue` for BLE task execution.
 */
typedef struct
{
    CommandType type; ///< Kind of command the BLE task shall execute.
    long timestamp;   ///< Creation timestamp used for record/replay ordering.
    union
    {
        struct
        {
            int speed; ///< Requested motor speed in the range `-100..100`.
        } motor;
        struct
        {
            DuploEnums::DuploColor color; ///< LED color to forward to the hub.
        } led;
        struct
        {
            char name[32]; ///< Fixed-size hub name buffer for queue-safe transfer.
        } hubName;
        struct
        {
            int soundId; ///< DUPLO sound identifier to play.
        } sound;
    } data;
} HubCommand;

/**
 * @brief Event types sent from the BLE worker task back to the application core.
 */
enum ResponseType
{
    Detected_Color,
    Detected_Speed,
    Detected_Voltage
};

/**
 * @brief POD payload stored in `responseQueue` for sensor and status events.
 */
typedef struct
{
    ResponseType type; ///< Event category carried by this queue item.
    union
    {
        struct
        {
            DuploEnums::DuploColor detectedColor; ///< Debounced color sensor result.
        } colorResponse;
        struct
        {
            int detectedSpeed; ///< Normalized speed sensor reading.
        } speedResponse;
        struct
        {
            float detectedVoltage; ///< Measured train battery voltage.
        } voltageResponse;
    } data;
} HubResponse;

/** @brief Minimum voltage delta required before reporting a new voltage event. */
#define VOLTAGE_THRESHOLD 0.1f
/** @brief Minimum speed delta required before reporting a new speed event. */
#define SPEED_THRESHOLD 5

/**
 * @class DuploHub
 * @brief Thread-safe high-level DUPLO train controller for the project firmware.
 *
 * `DuploHub` isolates BLE timing, queue handling, connection lifecycle and sensor routing from
 * the Arduino main loop. The main application pushes commands into the hub and consumes debounced
 * sensor events without talking to Legoino directly.
 */
class DuploHub
{
private:
    myLegoHub hub;                     ///< Underlying DUPLO/Legoino adapter instance.
    byte motorPort;                    ///< Configured motor port used for drive commands.
    std::string hubMACAddress;         ///< Optional preferred hub address for reconnects.
    bool wasConnected;                 ///< Historical marker for connection-dependent behavior.
    static DuploHub *instance;         ///< Singleton-style back-reference for static callbacks.
    volatile bool connectionState;     ///< Cached connection state shared with the main loop.
    volatile bool connectingState;     ///< Cached scan/connect-in-progress state.
    SemaphoreHandle_t connectionMutex; ///< Protects access to the cached connection flags.
    QueueHandle_t commandQueue;        ///< Main-loop to BLE-task command queue.
    QueueHandle_t responseQueue;       ///< BLE-task to main-loop event queue.
    TaskHandle_t bleTaskHandle;        ///< FreeRTOS handle of the BLE worker task.

    // Fixed command buffer for record / replay processing.
    // Recording stops automatically when this capacity is reached.
    static constexpr size_t COMMAND_BUFFER_CAPACITY = 32;
    std::array<HubCommand, COMMAND_BUFFER_CAPACITY> commandBuffer{};
    size_t commandBufferCount = 0; ///< Number of valid entries currently stored for replay.

    // Replay state management
    bool replaying = false;             ///< `true` while recorded commands are being replayed.
    size_t replayIndex = 0;             ///< Index of the next replay command.
    int64_t replayStartTime = 0;        ///< Timestamp when the current replay run started.
    int64_t originalStartTime = 0;      ///< Timestamp of the first recorded command before normalization.
    int64_t sequenceDuration = 0;       ///< Total replay sequence duration after normalization.
    int replayCycle = 0;                ///< Reserved replay cycle counter for future looped replay modes.
    bool recordCommandsEnabled = false; ///< `true` while outgoing commands are captured for replay.

    ConnectionCallback onConnectedCallback;          ///< Application hook for BLE connect events.
    ConnectionCallback onDisconnectedCallback;       ///< Application hook for BLE disconnect events.
    DetectedColorCallback detectedColorCallback;     ///< Application hook for debounced color updates.
    DetectedSpeedCallback detectedSpeedCallback;     ///< Application hook for speed updates.
    DetectedVoltageCallback detectedVoltageCallback; ///< Application hook for voltage updates.

    // Internal task management
    /**
     * @brief Create FreeRTOS primitives required for hub operation.
     */
    void initFreeRTOS();

    /**
     * @brief Tear down FreeRTOS objects created by the hub.
     */
    void cleanupFreeRTOS();

    /**
     * @brief Update cached connection flags under mutex protection.
     *
     * @param connected Current connection state reported by Legoino.
     * @param connecting True while an outbound connection attempt is in progress.
     */
    void updateConnectionState(bool connected, bool connecting);

    /**
     * @brief Pull commands from the queue and forward them to the hub.
     */
    void processCommandQueue();

    /**
     * @brief Flush pending commands and responses.
     */
    void clearQueues();

    // Command queue wrapper for recording functionality
    /**
     * @brief Enqueue a command, optionally recording it for replay.
     *
     * @param cmd Command to be sent to the hub task.
     * @param timeout Maximum time the caller may block waiting for space in the queue.
     * @return `pdTRUE` on success, or `errQUEUE_FULL` if the queue is saturated.
     */
    BaseType_t sendCommand(const HubCommand &cmd, TickType_t timeout);

    /**
     * @brief Lazily create the timer that debounces color sensor readings.
     *
     * @return True when the timer exists and is ready to start.
     */
    static bool ensureColorTimerInitialized();

    /**
     * @brief FreeRTOS entry point for the BLE management task.
     */
    static void bleTaskWrapper(void *parameter);

    /**
     * @brief Worker routine executed by the BLE management task.
     */
    void bleTaskFunction();

    /**
     * @brief Spawn the BLE task if it is not currently running.
     */
    void startBLETask();

    /**
     * @brief Request the BLE task to stop and wait for cleanup.
     */
    void stopBLETask();

    /**
     * @brief Advance connection state machine internal to the BLE task.
     */
    void updateBLE();

    /**
     * @brief Ensure the BLE task is alive before queuing work.
     */
    void ensureBLETaskRunning();

protected:
    // Sensor callback registration for lpf2hub
    /**
     * @brief Callback signature used by the Legoino color sensor bridge.
     * @param hub Hub instance that emitted the event.
     * @param portNumber Hub port that produced the reading.
     * @param deviceType Device type associated with the reading.
     * @param pData Raw payload buffer received from the LEGO protocol stack.
     */
    typedef void (*ColorSensorCallback)(void *hub, byte portNumber, DeviceType deviceType, uint8_t *pData);

    /**
     * @brief FreeRTOS-safe entry point for color sensor notifications.
     */
    static void staticColorSensorCallback(void *hub, byte portNumber, DeviceType deviceType, uint8_t *pData);

    /**
     * @brief Timer hook that emits a color event once readings stabilize.
     */
    static void colorStabilityTimerCallback(void *arg);

    /**
     * @brief Callback signature used by the Legoino speed sensor bridge.
     * @param hub Hub instance that emitted the event.
     * @param portNumber Hub port that produced the reading.
     * @param deviceType Device type associated with the reading.
     * @param pData Raw payload buffer received from the LEGO protocol stack.
     */
    typedef void (*SpeedSensorCallback)(void *hub, byte portNumber, DeviceType deviceType, uint8_t *pData);

    /**
     * @brief FreeRTOS-safe entry point for speed sensor notifications.
     */
    static void staticSpeedSensorCallback(void *hub, byte portNumber, DeviceType deviceType, uint8_t *pData);

    /**
     * @brief Callback signature used by the Legoino voltage sensor bridge.
     * @param hub Hub instance that emitted the event.
     * @param portNumber Hub port that produced the reading.
     * @param deviceType Device type associated with the reading.
     * @param pData Raw payload buffer received from the LEGO protocol stack.
     */
    typedef void (*VoltageSensorCallback)(void *hub, byte portNumber, DeviceType deviceType, uint8_t *pData);

    /**
     * @brief FreeRTOS-safe entry point for voltage sensor notifications.
     */
    static void staticVoltageSensorCallback(void *hub, byte portNumber, DeviceType deviceType, uint8_t *pData);

public:
    /**
     * @brief Default constructor.
     */
    DuploHub();

    /**
     * @brief Constructor with motor port selection.
     * @param port Motor port.
     */
    DuploHub(byte port);

    /**
     * @brief Destructor (cleans up FreeRTOS objects).
     */
    ~DuploHub();

    /**
     * @brief Initialize the hub and BLE connection.
     */
    void init();

    /**
     * @brief Initialize the hub with a specific BLE address.
     * @param address BLE address as string.
     */
    void init(const std::string &address);

    /**
     * @brief Check if BLE task is running (thread-safe).
     * @return true if BLE task is running.
     */
    bool isBLETaskRunning();

    /**
     * @brief Check if hub is connected (thread-safe).
     * @return true if connected.
     */
    bool isConnected();

    /**
     * @brief Check if hub is connecting (thread-safe).
     * @return true if connecting.
     */
    bool isConnecting();

    /**
     * @brief Check if hub is disconnected (thread-safe).
     * @return true if disconnected.
     */
    bool isDisconnected();

    /**
     * @brief Set the hub name.
     * @param name New hub name.
     */
    void setHubName(const char *name);

    /**
     * @brief Get the BLE address of the hub.
     * @return BLE address as string.
     */
    std::string getHubAddress();

    /**
     * @brief Get the hub name.
     * @return Hub name as string.
     */
    std::string getHubName();

    /**
     * @brief List all device ports.
     */
    void listDevicePorts();

    /**
     * @brief Set the motor port.
     * @param port Motor port.
     */
    void setMotorPort(byte port);

    /**
     * @brief Get the configured motor port.
     * @return Motor port.
     */
    byte getMotorPort();

    /**
     * @brief Enable or disable command recording for replay functionality.
     *
     * Recording uses a fixed-size buffer with capacity `COMMAND_BUFFER_CAPACITY`.
     * When full, recording is disabled automatically.
     *
     * @param enable True to start recording commands, false to stop recording.
     */
    void recordCommands(bool enable);

    /**
     * @brief Start replaying all previously recorded commands with normalized timestamps
     * The first command timestamp is set to 0 and all other timestamps are adjusted
     * to maintain relative timing differences
     */
    void replayCommands();

    /**
     * @brief Get the next command to replay if it's time to execute it
     * @return Pointer to the next command if ready, nullptr if no command ready or replay finished
     */
    const HubCommand *getNextReplayCommand();

    /**
     * @brief Check if replay is currently active
     * @return true if replaying, false otherwise
     */
    bool isReplayActive() const;

    /**
     * @brief Check if command recording is currently active.
     * @return true if recording is enabled, false otherwise.
     */
    bool isRecordingCommandsEnabled() const;

    /**
     * @brief Stop the current replay operation
     */
    void stopReplay();

    /**
     * @brief Set the motor speed.
     * @param speed Motor speed (-100..100).
     */
    void setMotorSpeed(int speed);

    /**
     * @brief Stop the motor immediately.
     */
    void stopMotor();

    /**
     * @brief Activate the base speaker port device.
     */
    void activateBaseSpeaker();

    /**
     * @brief Play a sound by sound ID.
     * @param soundId The sound ID to play.
     */
    void playSound(int soundId);

    /**
     * @brief Activate the RGB LED port device.
     */
    void activateRgbLight();

    /**
     * @brief Set the LED color.
     * @param color The color to set (DuploColor).
     */
    void setLedColor(DuploEnums::DuploColor color);

    /**
     * @brief Play a sound directly (low-level).
     * @param sound Sound ID.
     */
    void playSoundDirect(byte sound);

    /**
     * @brief Activate color sensor (legacy method).
     */
    void activateColorSensor();

    /**
     * @brief Activate speed sensor (legacy method).
     */
    void activateSpeedSensor();

    /**
     * @brief Activate voltage sensor.
     */
    void activateVoltageSensor();

    /**
     * @brief Register callback for hub connected event.
     * @param callback Function to call on connection.
     */
    void setOnConnectedCallback(ConnectionCallback callback);

    /**
     * @brief Register callback for hub disconnected event.
     * @param callback Function to call on disconnection.
     */
    void setOnDisconnectedCallback(ConnectionCallback callback);

    /**
     * @brief Register callback for detected color event.
     * @param callback Function to call on color detection.
     */
    void setDetectedColorCallback(DetectedColorCallback callback);

    /**
     * @brief Register callback for detected speed event.
     * @param callback Function to call on speed detection.
     */
    void setDetectedSpeedCallback(DetectedSpeedCallback callback);

    /**
     * @brief Register callback for detected voltage event.
     * @param callback Function to call on voltage detection.
     */
    void setDetectedVoltageCallback(DetectedVoltageCallback callback);

    /**
     * @brief Process response queue events in main task.
     */
    void processResponseQueue();

    /**
     * @brief BLE task event processing (call in main loop).
     */
    void update();
};

#endif // DUPLO_HUB_H
