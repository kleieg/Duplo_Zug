# Changelog

All notable changes to the DUPLO Train Controller project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed (behavior)

- Status LED: boot is shown as steady white instead of yellow.
- Status LED: recording is steady violet, replay is blinking violet (was yellow blinking); the low-battery warning is 5x fast yellow (was red), so red only relates to stopping and sleep.
- README: LED list updated, color brick table with the controller LED feedback; stale upload-mode line removed.
- Status LED: while no train is connected (searching), a short blue flash every ~1.5 s is shown; previously the LED was off after boot until a train connected.

- STOP is now a plain stop, identical to pressing the dial: motor stop, replay ends, knob to 0, silent, nothing locked. The latching emergency stop (steady red LED, brake sound + red train light, ignoring dial/color bricks/RECORD/PLAY) was removed. A running recording now continues and records the stop.

### Added

- Push to start: a train that has been standing for at least 1 s without target speed starts at +/-`MIN_SPEED` in the push direction when it is pushed by hand (any movement reported by the speed sensor). Not during replay; cancels a running water stop.
- Dial (encoder) button while awake: press = normal stop (immediate, nothing locked, also ends a replay; STOP stays the latching emergency stop), hold 2 s = fast red blinking, sleep on release. The press that wakes the controller is ignored until released.
- Encoder button pin is now a plain digital input at runtime; the RTC wake configuration is applied only right before sleeping.

- BLE task is supervised by the ESP task watchdog (15 s, reset on timeout): a hung BLE task now resets the controller instead of leaving it connected but unresponsive. Boot log shows the reset reason (`6` = task watchdog).

### Changed

- Command pacing uses the hub's acknowledgements instead of a fixed 200 ms: after each command the BLE task waits for the Port Output Command Feedback (0x82) or Port Input Format confirmation (0x47) for that port (observed 40-110 ms, timeout 250 ms) and resends a command the hub reports as discarded. Legoino forwards these messages via a new `setPortAckCallback()` (project-local change); the speaker setup is split into two confirmed steps. Diagnostic logging `LWP_FEEDBACK_DIAG` in `Lpf2Hub.cpp` is available but off.

### Fixed

- Two tiles of the same color in a row: the second was not detected in C TAG mode. C TAG sends one event per tile (color, then `0xff`), and our COLOR-mode debounce suppressed a repeated color because the `0xff` end marker never reset its "last stable color". C TAG events now bypass the debounce and are passed on immediately (also ~80 ms faster); C TAG is the default mode.
- False color tile reactions (in the air, on carpet or stone floor): tiles now only count while the train is driving (target speed set and the speed sensor reports movement). GREEN no longer starts a standing train at 40. The color sensor is switched to mode 1 "C TAG" (color tag, presumably the hub's own action tile detection) for evaluation; `COLOR_SENSOR_MODE` in `DuploHub.cpp` (0 = previous COLOR mode).
- Tile reactions were random (stop without sound, sound without stop, nothing): the DUPLO hub silently drops commands that arrive within ~20 ms of each other. Command pacing is back to 200 ms for all commands (the motor command is sent first, so the reaction itself is not delayed). Black and non-action colors no longer send train light commands (they flooded the hub on track gaps).
- Sensors/LED/speaker could stay inactive after connecting (no color/speed reports): Legoino silently skips activating a port the hub has not announced yet, and the hub drops port setups that arrive too soon after each other. The old 200 ms pauses hid both; activation now waits until the hub has announced the device (max. 2 s, logged otherwise) and keeps a 200 ms pause after each of the five one-time activations (runtime commands stay at 20 ms).
- Color tiles were missed or reacted to too late (train passed GREEN tiles, went back and forth, BLUE barely stopped). Causes on the controller side: 250 ms color debounce, motor commands queued behind LED/sound commands, fixed 200 ms pauses after LED/sound writes, and the BLE task polling its queue only every 50 ms. Now: 80 ms debounce, motor command first, BLE task waits on the queue, uniform 20 ms pacing with retry of rejected BLE writes (in `Lpf2Hub::WriteValue()`, project-local change). GREEN reverses directly with a 1 s cool-down; BLUE is a real 2.5 s water stop that then continues at the previous speed.
- Controller hung at boot on battery (steady yellow LED, no function): `Serial.flush()` on the USB-CDC serial waits until a USB host has read all output, which never happens without USB. All `Serial.flush()` calls removed; serial writes wait at most 2 ms. Present since debug output was enabled in `TrainController.cpp` (faa0fb4).
- Encoder: occasional lost clicks at any speed. The dial is now read by the ESP32-S3 hardware pulse counter (`PcntEncoder`, glitch filter, polled from the main loop) instead of the interrupt-based ESP32RotaryEncoder library (removed from `lib_deps`). A click counts after 3 of its 4 edges and the reference re-aligns to the detent when the dial rests, so single lost edges no longer lose a click. `DIAL_DIRECTION` inverts the turning direction if needed.
- When the train stands still although a speed is set, the controller resets its target speed and the knob to 0, so the next dial click starts from standstill instead of the old speed:
  - the DUPLO hub stopped the motor (train blocked, lifted, pressed down): reset immediately on the speed sensor's 0 report, unless the controller expected that report because its own command passes through 0 (direction change while moving, GREEN/BLUE bricks);
  - a train that never starts (blocked from standstill) sends no report: reset if no movement is reported within 1 s (`START_TIMEOUT_MS`), once the speed sensor has proven to work;
  - not applied during replay. Speed readings up to 5 now count as standing (was 10), matching the sensor's reporting threshold, so a slowly starting train is not reported as 0.
- Encoder: the dial skips the stop zone below `MIN_SPEED`; the first click from standstill starts the train at +/-`MIN_SPEED`, the next click down stops it (previously the first click did nothing).
- Encoder: `ENCODER_STEPS_PER_DETENT` 8 → 4; with 8 the dial needed two clicks per speed step (the old comment had the meaning reversed).
- Buttons: averaging/settling reduced from 10 to 3 samples, so a press registers after ~0.12-0.15 s instead of ~0.5 s.
- Status LED bursts showed the last blink for only one loop cycle (~20 ms); bursts now run for N full on/off cycles.
- PLAY with an empty recording no longer leaves the controller in a stuck "replay" state (buttons locked, no sleep).
- Encoder: fast knob turns no longer get lost; speed commands are still rate-limited (250 ms) but the latest value is always applied, so train speed matches the knob.
- Encoder: callback (esp_timer task) no longer blocks with `delay()` or touches app state; handling moved to the main loop.
- Sleep: before entering sleep the controller ends record/replay, stops the motor and actively disconnects the hub; a running replay counts as activity.
- Color sensor: tile actions are ignored during emergency stop; emergency stop and RED tile reset the last speed so a later BLUE tile cannot restart the train; GREEN syncs the knob to the reversed speed.
- Hub connect/disconnect callbacks (BLE task, core 0) only set flags; LED and state handling runs in the main loop (no cross-core races on `StatusLED`/FastLED).
- Low-voltage warning only fires with a valid voltage reading (was triggered by the initial 0 V), is checked every 30 s, and the reading is cleared on disconnect.
- `HubCommand::timestamp` is now `int64_t` (was a truncated 32-bit `long`).

### Changed

- Status LED: steady red now means emergency stop only; the RED track brick shows a 3x red burst. A single `updateModeIndication()` shows the current mode (emergency stop, replay, recording, initializing) whenever no burst plays, so the emergency-stop indication is restored after disconnect, wake, low-voltage or tile bursts. Heartbeat and low-voltage bursts wait instead of cutting off other bursts.
- RECORD and PLAY are ignored during emergency stop.
- Emergency stop (button and RED brick) queues the motor stop before the brake sound and train LED, so the train stops ~400 ms earlier.
- `DuploHub`: one `queueCommand()` helper for all commands; replay re-queues the recorded command via `replayCommand()` instead of a duplicated switch in `TrainController`.
- `TrainController`: removed blocking `delay()` calls from button and color handlers (the BLE task paces hub writes); hub name is the `HUB_NAME` constant; sound button cycle starts with HORN.
- `ADCButtons`: sampling state is per instance, single `ensureCapacity()` resize routine, sample buffer sized from the validated sample count.
- `platformio.ini`: shared `[env]` section; serial ports are auto-detected instead of hard-coded.
- Repository: `.DS_Store`, KiCad auto-backups and `fp-info-cache` are no longer tracked; GitHub Actions builds both environments on every push.
- Refactored command construction in `DuploHub` to a unified helper path for queue commands.
- Optimized `StatusLED` updates with a dirty-flag to avoid unnecessary `FastLED.show()` calls.
- Corrected water-button behavior in `TrainController` (removed unrelated color mutation, fixed log semantics).
- Renamed memory utility implementation from `SytemMemory.cpp` to `SystemMemory.cpp`.

### Removed

- Startup "upload mode" (hold encoder 2 s for a 10 s upload window): uploads work via USB auto-reset while awake; from sleep, wake with the encoder button first.
- Removed `DuploHub::update()` and `ensureBLETaskRunning()` (uncalled since the July 2025 refactoring; the handle check could never detect a hung or crashed BLE task).
- Removed unused `wasConnected`, `replayCycle`, `sequenceDuration`, the `DuploHub(byte port)` constructor, `setMotorPort()`/`getMotorPort()` (the motor always uses the DUPLO `MOTOR` port) and the never-implemented `playSoundDirect()` declaration.
- Removed unreachable, non-thread-safe direct-hub fallbacks for a missing command queue; they now log an error like the other commands.
- Removed unused globals and dead callback paths in `TrainController`.
- Removed unimplemented low-level API declarations from `DuploHub` header.

### Documentation

- `ARCHITECTURE.md` updated to the current design (inputs, stop sync, LED mode/burst model, sleep/wake, watchdog, modules, framework constraint).
- Removed `Refactorings.md`; the planned Legoino facade consolidation via `myLegoHub` was dropped (all Legoino calls are confined to `DuploHub.cpp`, an extra wrapper layer would add nothing).
- `ToDo.md` reset; remaining ideas were reviewed and either done, dropped or listed in the session notes.

## [2.2.0] - 2026-02-25

### Added

- Wake diagnostics at startup (`esp_sleep_get_wakeup_cause`) with detailed debug output.
- Visual wake confirmation: controller RGB LED blinks green 5 times after sleep wake.
- Sleep-entry indication with red blink sequence before LED off.
- Guarded firmware upload window: requires continuous encoder button hold (≥2s) during startup, then opens 10-second upload mode.

### Changed

- Sleep timeout standardized to 5 minutes (`300000 ms`) for regular operation.
- Encoder wake configuration updated to RTC **EXT1 ALL_LOW** (LOW-active button).
- Deep-sleep wake path hardened with explicit RTC GPIO setup and hold management.
- Fallback wake behavior added: if RTC wake is not possible on the configured pin, firmware falls back to light sleep GPIO wake.
- BLE reconnect/initial connection attempt logging is throttled to one message every 10 seconds.
- Command recording switched to a fixed buffer of **32 commands** (deterministic memory usage).
- Recording now stops automatically when the command buffer reaches capacity.
- `TrainController` recording LED/state now auto-resets when recording is stopped by buffer-full condition.

### Documentation

- Updated `README.md` with current sleep/wake behavior, upload-mode trigger logic, LED feedback states, monitoring notes, and troubleshooting guidance.
- Updated `ARCHITECTURE.md` with dedicated power-management and wake strategy section.
- Updated `hardware/Hardware.md` with encoder wake role and reset-vs-wake clarification.

## [2.1.0] - 2025-01-20

### Added

- **DuploHubExtended class** with bidirectional sensor data support
- **Sensor callback system** for color, distance, and button events
- **Second FreeRTOS queue** (sensorQueue) for BLE → Main task communication
- **Static callback wrappers** for seamless Lpf2Hub integration
- **Thread-safe sensor activation** via command queue system
- **TrainController_Extended.cpp** demonstrating color-based speed control
- **Real-time sensor processing** with ~50ms latency
- **Emergency distance detection** with automatic motor stopping
- **Button-based manual control** for train operation

### Enhanced
- **Architecture documentation** with bidirectional data flow diagrams
- **API reference** extended with sensor callback examples
- **Performance characteristics** for dual-queue communication system
- **Memory usage analysis** for sensor data processing
- **Complete sensor processing chain analysis** with 8-layer detailed flow
- **Processing timeline documentation** showing 75ms sensor-to-action latency
- **Layer-by-layer code examples** from hardware to application callbacks

### Technical Details
- Sensor data flows BLE Task (Core 0) → Main Task (Core 1) via sensorQueue
- Static callback integration bridges Lpf2Hub callbacks to queue system
- Non-blocking sensor data processing with configurable queue sizes
- Thread-safe sensor activation commands via existing command queue
- Complete backward compatibility with existing DuploHub API

## [2.0.0] - 2025-01-20

### Added
- **Multi-task architecture** with dedicated BLE task running on Core 0
- **Thread-safe command queue system** for inter-task communication
- **FreeRTOS integration** with proper synchronization primitives
- **Automatic connection recovery** with background reconnection logic
- **System health monitoring** with task status checking
- **Professional logging system** with component-specific prefixes
- **TrainController.cpp** as main application (renamed from TrainHub.cpp)
- **DuploHub class** as hardware abstraction layer
- **Comprehensive documentation** including README, Quick Reference, and Architecture guide

### Changed
- **Complete refactoring** from single-threaded to multi-threaded architecture
- **Connection management** moved to background task for non-blocking operation
- **Command execution** now uses queue-based system instead of direct calls
- **Error handling** improved with graceful degradation and recovery
- **Performance optimization** with dual-core ESP32 utilization

### Improved
- **Responsiveness**: Main loop never blocks on BLE operations
- **Reliability**: Automatic task recovery and connection management
- **Maintainability**: Clean separation of concerns between layers
- **Debuggability**: Detailed logging and status monitoring
- **Code quality**: Thread-safe design patterns throughout

## [1.0.0] - 2025-01-19

### Added
- **Initial project structure** with basic DUPLO train control
- **DuploHub class** with basic connection management
- **Connection callbacks** for hub connect/disconnect events
- **Motor control** with speed setting and stop functionality
- **LED control** with color changes
- **Basic train operation flow**
- **Serial monitoring** with basic status output

### Technical Details
- Single-threaded architecture using Arduino loop
- Direct Lpf2Hub integration for BLE communication
- Basic connection state tracking
- Simple application state machine

### Dependencies
- Legoino library for LEGO Powered Up protocol
- NimBLE-Arduino for Bluetooth LE communication
- ESP32 Arduino framework

## [0.1.0] - 2025-01-18 (Initial Development)

### Added
- **Project initialization** with PlatformIO setup
- **Basic Lpf2Hub integration** from Legoino library
- **ESP32 platform configuration**
- **Initial connection attempts** to DUPLO train hub
- **Proof of concept** motor control commands

### Development Notes
- Started as simple example modification
- Explored Legoino library capabilities
- Established basic BLE connectivity
- Verified ESP32 compatibility with LEGO hubs

---

## Upgrade Guide

### From 1.0.0 to 2.0.0

The 2.0.0 release introduces breaking changes due to the multi-task architecture:

#### Required Code Changes

1. **Update setup() function**:
   ```cpp
   // Old (1.0.0)
   void setup() {
       duploHub.init();
   }
   
   // New (2.0.0)
   void setup() {
       duploHub.startBLETask();  // Start background BLE task
   }
   ```

2. **Update main loop**:
   ```cpp
   // Old (1.0.0)
   void loop() {
       if (!duploHub.isConnected()) {
           duploHub.init();
       }
       // ... direct control code
   }
   
   // New (2.0.0)
   void loop() {
       duploHub.update();  // Only handle callbacks
       // ... application logic (no direct BLE calls)
   }
   ```

   > Note (later versions): `DuploHub::update()` has been removed. The BLE task handles connection
   > state and callbacks on its own; `loop()` only calls `processResponseQueue()`.

3. **Thread-safe commands** (automatically handled by backward compatibility):
   ```cpp
   // These calls are now automatically thread-safe
   duploHub.setMotorSpeed(50);
   duploHub.setLedColor(RED);
   ```

#### Benefits After Upgrade
- Non-blocking BLE operations
- Automatic connection recovery
- Better system responsiveness
- Professional status monitoring
- Improved error handling

## Compatibility Matrix

| Version | ESP32 Core | Arduino Framework | PlatformIO | NimBLE | Legoino |
|---------|------------|-------------------|------------|---------|---------|
| 2.0.0   | ≥2.0.0     | ≥2.0.0           | ≥6.0.0     | ≥1.4.0  | ≥1.8.0  |
| 1.0.0   | ≥1.0.6     | ≥1.0.6           | ≥5.0.0     | ≥1.3.0  | ≥1.7.0  |
| 0.1.0   | ≥1.0.4     | ≥1.0.4           | ≥4.0.0     | ≥1.2.0  | ≥1.6.0  |

## Known Issues

### Version 2.0.0
- Initial BLE scan may take up to 10 seconds on first boot
- Memory usage increased due to FreeRTOS objects (~2KB additional)
- Debug output may be verbose during development

### Version 1.0.0
- BLE operations could block main loop for several seconds
- No automatic recovery from connection failures
- Limited error handling and status reporting

## Contributors

- **System Architecture**: Multi-task design and FreeRTOS integration
- **Hardware Abstraction**: DuploHub class design and thread-safe API
- **Documentation**: Comprehensive guides and API documentation
- **Testing**: Hardware validation with DUPLO train systems

## Acknowledgments

- **Cornelius Munz** - Original Legoino library
- **ESP32 Community** - Arduino framework and examples
- **LEGO Group** - DUPLO and Powered Up specifications
