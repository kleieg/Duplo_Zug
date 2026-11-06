# DUPLO Train Controller NG - Quick Guide

## 1. Power On And Connect

1. Power on the controller or wake it with a short press on the rotary encoder.
2. Power on the DUPLO train.
3. The controller automatically searches for the train.
4. As soon as the controller LED signals green, the train is ready for operation.

## 2. Controller LED Meaning

- Steady white (about 1.5 s): controller is starting
- Short blue flash every ~1.5 s: searching for the train (not connected)
- Fast blue blinking: connection is established, train initialization still in progress
- Green blinking: train is ready for commands
- Green short pulses: ready, but currently idle
- Violet blinking: recording active
- Yellow blinking: playback active
- 3x red: train stopped at a red track brick
- 5x fast red: train battery low (repeats every 30 seconds)
- 3x white: train connection was lost
- 3x green / blue / yellow: green / blue / yellow track brick detected
- 5x green: controller has just woken from sleep

Rule of thumb: the train is truly command-ready only when green is shown.
Short blink bursts are events; after a burst the LED returns to the current mode
(for example back to violet blinking while recording is still active).

## 3. Controls

- Rotate encoder: set reverse or forward speed (first click from standstill starts at the minimum speed)
- Press encoder: stop the train (normal stop, nothing locked; also ends a playback). In sleep: wake the controller
- Hold encoder 2 s: LED blinks red fast, release to put the controller to sleep right away
- STOP: stop the train (same as pressing the encoder; nothing is locked, also ends a playback)
- REC: start or stop recording
- PLAY: play the latest recording once
- LIGHT: change train light color
- SOUND: cycle sounds
- WATER: trigger water action

## 4. Sleep And Wake

- After 5 minutes without input, the controller enters sleep mode.
- Sleep now: hold the encoder for 2 seconds until the LED blinks red fast, then release.
- Before sleeping, the train is stopped and disconnected.
- In sleep, the LED is off.
- Wake-up is only possible with the encoder button.
- The front buttons do not wake the controller.

## 5. Firmware Upload

- Upload works while the controller is awake (USB auto-reset, no button needed).
- In sleep the USB connection is off: press the encoder button to wake the controller, then upload.
- Recovery if a firmware does not boot: hold the board's BOOT button and press RESET, then upload.

## 6. Track Color Bricks

- Yellow: horn
- Red: stop train
- Green: stop and switch direction
- White: train light on or off
- Blue: water action and then continue at last speed

## 7. Typical Issues

- No connection: move train closer to the controller or restart the train
- Train connected but not responding: wait for green ready state
- Controller does not wake: press encoder, not STOP

## 8. Current Status

- Reconnect after train power cycle: typically 2 to 3 seconds
- After reconnect, the train responds immediately as soon as the LED signals green

Status: 19.03.2026
