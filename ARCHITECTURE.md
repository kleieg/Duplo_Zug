# Technical Architecture

## System Summary

The DUPLO Train Controller NG uses an ESP32-S3 dual-core layout to separate BLE timing from user-interface logic. The firmware is structured in three layers:

1. `TrainController`: application logic, inputs, UI/LED, sleep/wake, record/replay, user feedback
2. `DuploHub`: thread-safe BLE abstraction with command and response queues; the only place that talks to Legoino
3. `myLegoHub` plus Legoino/NimBLE: DUPLO-specific protocol helpers on top of Legoino's `Lpf2Hub`

`myLegoHub` inherits publicly from `Lpf2Hub` and only adds DUPLO-specific byte sequences (speaker, LED, sounds, disconnect). An additional facade that wraps every Legoino call was considered and deliberately not done: all Legoino calls are confined to `DuploHub.cpp`, so a library change would already be local.

The implementation emphasizes low perceived latency:

- connect callback work is not blocking the BLE task
- post-connect initialization is staged in the main loop
- reconnect scanning uses short scan windows
- the controller distinguishes between `connected` and `ready`

## Runtime Topology

### Core assignment

- Core 0: BLE stack and `DuploHub` BLE task (supervised by the task watchdog, see below)
- Core 1: Arduino loop and application state machine
- esp_timer task: color sensor debounce timer

### Queue model

- `commandQueue` (10 entries): main loop to BLE task; the BLE task waits on it (no polling), so a command goes out immediately; the DUPLO hub silently drops commands that arrive while it is still busy, so after each command the BLE task waits for the hub's acknowledgement for that port (Port Output Command Feedback 0x82, or Port Input Format 0x47 after a port setup; typically 40-110 ms, timeout 250 ms) and sends a discarded command once more; commands without acknowledgement (hub name) use a fixed 200 ms; tile handlers send the motor command first; a write the BLE stack rejects (no free buffer) is retried in `Lpf2Hub::WriteValue()` instead of being lost
- `responseQueue` (100 entries): BLE task to main loop (sensor events)

### Shared synchronization

- `connectionMutex`: protects cached connection and connecting flags
- atomic flags: connect/disconnect events from the BLE task are only flagged there and handled in the main loop, so UI state and the status LED are only touched from core 1
- atomic sleep handshake (`sleepRequested`, `linkShutdownComplete`) between main loop and BLE task
- ESP timer plus spinlock: debounced color sensor stabilization

### Task watchdog

The BLE task is registered with the ESP task watchdog (15 s timeout, reset on timeout) and feeds it every loop iteration. A hung BLE task resets the controller instead of leaving it connected but unresponsive. The boot log prints the reset reason (`6` = task watchdog). 15 s instead of the SDK default of 5 s because a connect attempt alone may block for 5 s plus service discovery.

## Connection Lifecycle

### Initial connect

1. `setup()` initializes inputs, LED and starts `DuploHub`
2. `DuploHub` creates FreeRTOS primitives and starts the BLE task on Core 0
3. The BLE task scans for a DUPLO hub
4. On BLE link-up the BLE task flags the event; the main loop runs `onHubConnected()`
5. The main loop runs a non-blocking hub initialization state machine
6. Only after all required hub devices are activated does the controller enter the `ready` state

### Ready semantics

The implementation explicitly separates:

- BLE link connected
- hub initialized and ready for commands

This distinction exists because the DUPLO hub requires a short activation sequence after BLE connection before LED, sound and sensor commands are usable.

### Reconnect behavior

- short reconnect scan window (2 s) in `DuploHub`
- throttled scan restarts while disconnected
- shorter BLE supervision timeout (2 s) for faster loss detection
- on disconnect a running replay ends and sensor readings are reset

Observed on hardware: reconnect after train power-cycle in about 2 to 3 seconds.

## Post-Connect Initialization

The post-connect sequence is staged from the main loop instead of the BLE callback context, because blocking there would stall command processing.

Steps:

1. register application callbacks
2. activate hub RGB light
3. activate speaker
4. activate color sensor
5. activate speed sensor
6. activate voltage sensor
7. set hub name (`HUB_NAME`)
8. wait short settle period
9. set `hubReady = true`

## Inputs

### Front buttons (ADC ladder)

- six buttons on one ADC pin (GPIO 7), decoded by voltage range
- a press is reported after 3 settling samples plus 3 averaged samples, one sample per loop pass: about 120 to 150 ms hold time (`ADC_BUTTON_SAMPLES`)
- STOP: plain stop (see below); REC/PLAY: record and replay; LIGHT, SOUND, WATER: train light and sounds

### Dial (rotary encoder)

- read by the ESP32-S3 hardware pulse counter (`PcntEncoder`, x4 quadrature decoding with glitch filter), polled from the main loop; no interrupts or timer callbacks
- one speed step (5) per click; a click counts after 3 of its 4 edges, and the reference re-aligns to the detent after 300 ms at rest, so single lost edges neither lose a click nor accumulate
- speed range +/-100; the stop zone below `MIN_SPEED` (25, measured minimum to move the train) is skipped: from standstill the first click starts at +/-25, one click down from +/-25 stops
- speed commands are rate-limited to one per 250 ms, the latest value is always applied
- the knob value follows speed changes from other sources (color bricks, replay, stops)
- `DIAL_DIRECTION` inverts the turning direction if needed

### Push to start

If the train has been standing without a target speed for at least 1 s and the speed sensor then reports a speed of at least 10 (pushed by hand), the controller starts it at +/-`MIN_SPEED` in the push direction (sensor sign = motor sign). The 1 s arming time keeps a train that is still rolling out after a stop from restarting itself. Not during replay; a running water stop is cancelled.

### Dial button (GPIO 11)

- in sleep: wake source
- press: plain stop (immediately on press)
- hold 2 s: fast red blinking, the controller sleeps on release (sleeping while held would immediately wake it again)
- the press that woke the controller is ignored until released
- the pin is a plain digital input at runtime; the RTC wake configuration is applied only right before sleeping

### Plain stop

STOP button and dial press do the same: stop the motor, end a running replay, set the knob to 0. Nothing is locked; turning the dial drives again. A running recording continues and records the stop. There is no latching emergency stop.

### Input gating

Buttons and dial are ignored until the hub is connected and ready. During replay only STOP and PLAY are accepted.

## Sensor Processing

### Color sensor

The sensor runs in mode 1 "C TAG" (`COLOR_SENSOR_MODE` in `DuploHub.cpp`): the hub reports one event per action tile (tile color, followed ~1 ms later by `0xff` = end of tag) and nothing for carpet, floor or track. Each event is pushed into `responseQueue` immediately (no debounce, no same-color filter, so two equal tiles in a row both count); `0xff` is ignored.

In the alternative mode 0 "COLOR" the sensor classifies any surface and is noisy, so there:

- raw color changes are collected in BLE context
- a one-shot ESP timer waits for 80 ms stability
- only stable transitions (different from the last stable color) are pushed into `responseQueue`

`COLOR_RAW_DIAG` logs every raw sensor message and the debounce decisions (off by default).

Color bricks are handled entirely by the controller (the hub does nothing on its own while connected; ignored during record and replay). The motor command is always sent first, sound and LED after it. Tiles only count while the train is driving (target speed set and the speed sensor reports movement), so standing, lifted or pushed trains do not react to carpet or floor. Only the five action colors react; black (track gaps) and other colors are ignored. The color sensor runs in mode 1 "C TAG" (one event per action tile, see Color sensor above). RED stops; GREEN reverses directly, further GREEN detections are ignored for 1 s so the reversing train does not reverse again on the same tile; BLUE stops for 2.5 s with the water sound and then continues at the previous speed (dial or STOP during the pause win); YELLOW horn; WHITE toggles the train light.

### Speed sensor

- only changes larger than 5 (raw) are reported; readings up to 5 count as standing
- speed is normalized to `-100..100`

### Stop sync with the hub's safety stop

The DUPLO hub cuts the motor by itself when the train is blocked, lifted or pressed down. The controller keeps its target speed and knob in sync:

- a speed report of 0 while a target speed is set resets target and knob to 0 immediately (stop sent)
- when the controller's own command passes through 0 (direction change while moving, GREEN brick), one 0 report is expected and ignored (expires after 1.5 s); during a BLUE water stop the target is 0, so the stop is not treated as "stopped by itself"
- a train that never starts sends no report: a drive command from standstill must produce a movement report within 1 s, once the sensor has reported at least once
- not applied during replay

### Voltage sensor

- voltage events are rate-limited in the application
- below 5 V a low-battery warning (5 fast yellow blinks) is shown every 30 s; no warning without a valid reading

## Record and Replay

`DuploHub` stores outgoing commands with timestamps in a fixed buffer.

- maximum 32 recorded commands; recording stops automatically when the buffer is full
- replay queues the recorded commands again with their original timing, runs once, then ends
- PLAY with an empty recording does nothing
- a running replay counts as activity for the sleep timer; it ends on stop, disconnect or sleep

## Status LED

One RGB LED. Persistent modes are continuous patterns; events are finite blink bursts. A burst temporarily overrides the mode; afterwards `updateModeIndication()` restores the current mode.

Modes (priority order):

- violet blinking: replay
- steady violet: recording
- short blue flash every ~1.5 s: no train connected, searching
- fast blue blinking: connected, still initializing
- off (plus green heartbeat, 2 short blinks every 8 s, when idle and ready)

Bursts:

- 3x green: ready / green brick; 3x blue / yellow: blue / yellow brick; 1-2x white: white brick
- 3x red: red brick
- 5x fast yellow: train battery low
- 3x white: connection lost
- 5x green: woke from sleep
- fast red while the dial is held: sleep on release; 2x red: entering sleep

Boot: steady white while `setup()` runs (about 1.5 s); a hang during boot leaves it white.

## Power Management

### Sleep

- entered after 5 minutes without input, or by holding the dial for 2 s
- before sleeping: end record/replay, stop the motor and actively disconnect the hub (`CMD_STOP_AND_DISCONNECT`, reconnect scanning suppressed), 2x red indication
- deep sleep with EXT1 wake on the dial button (LOW); light sleep with GPIO wake as fallback if the pin is not RTC-capable

### Wake

- only by the dial button (the front buttons share one analog pin and cannot provide a clean digital wake level)
- 5x green acknowledgment
- firmware upload needs no button while awake; from sleep, wake with the dial first (USB is off in sleep); recovery: BOOT + RESET

## Project-Owned Modules

- [src/TrainController.cpp](src/TrainController.cpp): application state machine, inputs, LED and UX logic
- [src/DuploHub.cpp](src/DuploHub.cpp): BLE task, queues, sensors, record/replay, sleep handshake
- [src/myLegoHub.cpp](src/myLegoHub.cpp): DUPLO-specific protocol helpers on top of Legoino
- [src/PcntEncoder.cpp](src/PcntEncoder.cpp): rotary encoder via the hardware pulse counter
- [src/ADCButton.cpp](src/ADCButton.cpp): ADC ladder button handling
- [src/StatusLED.cpp](src/StatusLED.cpp): single RGB LED with steady, blinking and burst patterns
- [src/SystemMemory.cpp](src/SystemMemory.cpp): diagnostics helpers
- [src/debug.cpp](src/debug.cpp): mutex-protected debug output
- [src/diagnostics/InputMonitor.cpp](src/diagnostics/InputMonitor.cpp): separate firmware (`esp32-s3-input-monitor`) printing raw ADC button values

## Vendored Dependencies

The repository vendors third-party code under `lib/`.

- `lib/Legoino`: protocol library used by the project, with project-local adjustments
- `lib/NimBLE-Arduino`: BLE stack used by Legoino

Project documentation and code comments are maintained primarily for project-owned files. Vendored code is only updated where project-specific modifications are made.

The vendored Legoino/NimBLE versions tie the project to Arduino-ESP32 2.x (`espressif32@6.4.0`, ESP-IDF 4.4). `PcntEncoder` also uses the legacy `driver/pcnt.h` API of that version. A move to Arduino-ESP32 3.x / ESP-IDF 5 would require adapting `DuploHub.cpp`/`myLegoHub` and `PcntEncoder`.
