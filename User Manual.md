# DUPLO Train Controller NG - Quick Guide

## 1. Power On And Connect

1. Power on the controller or wake it with a short press on the rotary encoder.
2. Power on the DUPLO train.
3. The controller automatically searches for the train.
4. As soon as the controller LED signals green, the train is ready for operation.

## 2. Controller LED Meaning

- Yellow blinking: controller is starting
- Blue blinking: connection is established, train initialization still in progress
- Green blinking: train is ready for commands
- Green short pulses: ready, but currently idle
- Violet blinking: recording active
- Yellow blinking: playback active
- Solid red: emergency stop active
- White blinking: train connection was lost
- 5x green: controller has just woken from sleep

Rule of thumb: the train is truly command-ready only when green is shown.

## 3. Controls

- Rotate encoder: set reverse or forward speed
- Short encoder press: wake controller from sleep
- STOP: emergency stop on/off; active recording or playback is stopped first
- REC: start or stop recording
- PLAY: play the latest recording once
- LIGHT: change train light color
- SOUND: cycle sounds
- WATER: trigger water action

## 4. Sleep And Wake

- After 5 minutes without input, the controller enters sleep mode.
- In sleep, the LED is off.
- Wake-up is only possible with the encoder button.
- The front buttons do not wake the controller.

## 5. Upload Mode

- During startup, hold the encoder continuously for at least 2 seconds.
- A 10-second firmware upload window then opens.
- A short tap does not start upload mode.

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
