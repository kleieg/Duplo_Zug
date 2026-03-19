# Technical Architecture

## System Summary

The DUPLO Train Controller NG uses an ESP32-S3 dual-core layout to separate BLE timing from user-interface logic. The firmware is structured in three layers:

1. `TrainController`: application logic, UI, sleep/wake, replay/record, user feedback
2. `DuploHub`: thread-safe BLE abstraction with command and response queues
3. `myLegoHub` plus Legoino/NimBLE: DUPLO-specific BLE protocol access

The current implementation emphasizes low perceived latency:

- connect callback work is no longer blocking the BLE task
- post-connect initialization is staged in the main loop
- reconnect scanning uses short scan windows
- the controller distinguishes between `connected` and `ready`

## Runtime Topology

### Core assignment

- Core 0: BLE stack and `DuploHub` task
- Core 1: Arduino loop and application state machine

### Queue model

- `commandQueue`: main loop to BLE task
- `responseQueue`: BLE task to main loop

### Shared synchronization

- `connectionMutex`: protects cached connection and connecting flags
- ESP timer plus spinlock: debounced color sensor stabilization

## Connection Lifecycle

### Initial connect

1. `TrainController::setup()` initializes the UI and starts `DuploHub`
2. `DuploHub` creates FreeRTOS primitives and starts a BLE worker task on Core 0
3. The BLE task starts scanning for a DUPLO hub
4. Once BLE link-up is detected, the application receives `onHubConnected()`
5. The application starts a non-blocking hub initialization state machine
6. Only after all required hub devices are activated does the controller enter the `ready` state

### Ready semantics

The implementation explicitly separates:

- BLE link connected
- hub initialized and ready for commands

This distinction exists because the DUPLO hub requires a short activation sequence after BLE connection before LED, sound and sensor commands are usable.

### Reconnect behavior

Current reconnect tuning:

- short reconnect scan window in `DuploHub`
- throttled scan restarts while disconnected
- shorter BLE supervision timeout for faster loss detection

Observed runtime result on hardware:

- reconnect after train power-cycle: about 2 to 3 seconds
- train reacts immediately once green ready indication appears

## Post-Connect Initialization

The post-connect sequence is intentionally staged from the main loop instead of the BLE callback context.

Reason:

- connection callbacks are triggered from the BLE task
- blocking delays there would stall command processing and make the train appear connected but unresponsive

Current staged initialization steps:

1. register application callbacks
2. activate hub RGB light
3. activate speaker
4. activate color sensor
5. activate speed sensor
6. activate voltage sensor
7. set hub name
8. wait short settle period
9. set `hubReady = true`

## Sensor Processing

### Color sensor

The color sensor is noisy on reflective surfaces. To reduce false triggers:

- raw color changes are collected in BLE context
- a one-shot ESP timer waits for 250 ms stability
- only stable transitions are pushed into `responseQueue`

### Voltage sensor

- voltage events are rate-limited in the application
- low voltage warning is visualized on the controller LED

### Speed sensor

- significant changes are forwarded to the application
- speed is normalized to `-100..100`

## Record and Replay

`DuploHub` stores outgoing commands in a fixed-size static buffer.

Current behavior:

- maximum 32 recorded commands
- recording stops automatically when buffer is full
- replay runs once, then terminates cleanly
- user input is restricted while replay is active

## UI State Model

### Controller status LED

- yellow blinking: boot/startup
- blue blinking: connected, still initializing
- green blink burst: ready achieved
- green short heartbeat: idle and ready
- violet blinking: recording
- yellow blinking: replay
- red steady: emergency stop
- white blink burst: disconnect

### Input gating

Buttons and encoder commands are ignored until both conditions are true:

- hub connected
- hub ready

This prevents the user from issuing commands into a not-yet-initialized hub.

## Power Management

### Sleep

- 5 minutes inactivity timeout
- red pre-sleep indication
- deep sleep preferred via encoder RTC wake
- light sleep fallback via GPIO wake if RTC wake is not available

### Wake

- wake only by encoder button
- green wake blink acknowledgment
- startup upload mode requires continuous encoder hold for at least 2 seconds

## Project-Owned Modules

- [src/TrainController.cpp](src/TrainController.cpp): application state machine and UX logic
- [src/DuploHub.cpp](src/DuploHub.cpp): BLE worker, queues, sensors, replay/record
- [src/myLegoHub.cpp](src/myLegoHub.cpp): DUPLO-specific message helpers
- [src/ADCButton.cpp](src/ADCButton.cpp): ADC ladder button handling
- [src/StatusLED.cpp](src/StatusLED.cpp): single RGB LED state machine
- [src/SystemMemory.cpp](src/SystemMemory.cpp): diagnostics helpers
- [src/debug.cpp](src/debug.cpp): mutex-protected debug output

## Vendored Dependencies

The repository vendors third-party code under `lib/`.

- `lib/Legoino`: protocol library used by the project, with project-local adjustments
- `lib/NimBLE-Arduino`: BLE stack used by Legoino

Project documentation and code comments are maintained primarily for project-owned files. Vendored code is only updated where project-specific modifications are made.

## Current Open Architectural Topics

- continue facade consolidation through `myLegoHub`
- optional preferred-train pairing strategy
- possible further cleanup of debug/logging granularity
