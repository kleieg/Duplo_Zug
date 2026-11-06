/**
 * @file PcntEncoder.h
 * @brief Rotary encoder input via the ESP32 hardware pulse counter (PCNT).
 *
 * Counts every quadrature edge in hardware (with glitch filter), so no edge is lost while
 * interrupts or other tasks are busy. The value is read by polling `update()` from the main loop.
 *
 * Detent detection is tolerant: a step is taken after `DETENT_THRESHOLD_EDGES` of the
 * `EDGES_PER_DETENT` edges of a click, so a single lost edge does not lose the click. When the
 * dial rests, it physically sits in a detent, so the reference is re-aligned to the current count
 * after `REST_REALIGN_MS`; lost edges therefore cannot accumulate.
 *
 * Interface mirrors the previously used ESP32RotaryEncoder library (`setEncoderValue()`,
 * `getEncoderValue()`, boundaries, step value).
 *
 * @copyright
 * MIT License
 */

#pragma once

#include <Arduino.h>
#include "driver/pcnt.h"

class PcntEncoder
{
public:
    /**
     * @brief Create an encoder on two GPIOs.
     * @param pinA Encoder channel A GPIO.
     * @param pinB Encoder channel B GPIO.
     * @param unit Hardware pulse counter unit to use.
     */
    PcntEncoder(int pinA, int pinB, pcnt_unit_t unit = PCNT_UNIT_0);

    /**
     * @brief Configure pins (internal pull-ups) and the pulse counter, then start counting.
     * @param glitchFilterApbCycles Pulses shorter than this many APB cycles (80 MHz) are ignored;
     *        max 1023 (~12.8 us).
     * @return `true` if the pulse counter was configured successfully.
     */
    bool begin(uint16_t glitchFilterApbCycles = 1023);

    /**
     * @brief Limit the encoder value range (values are clamped, no wrap-around).
     */
    void setBoundaries(long minValue, long maxValue);

    /**
     * @brief Value change per detent (click).
     */
    void setStepValue(long stepValue);

    /**
     * @brief Turning direction: +1 (default) or -1 to invert.
     */
    void setDirection(int direction);

    /**
     * @brief Poll the pulse counter; call regularly from the main loop.
     * @return `true` if the encoder value changed since the last call.
     */
    bool update();

    /**
     * @brief Current encoder value.
     */
    long getEncoderValue() const;

    /**
     * @brief Override the encoder value (e.g. to sync the knob with the train speed).
     *        Does not count as a change for `update()`.
     */
    void setEncoderValue(long value);

private:
    static constexpr int32_t EDGES_PER_DETENT = 4;       ///< quadrature edges per click
    static constexpr int32_t DETENT_THRESHOLD_EDGES = 3; ///< edges needed to take a step
    static constexpr unsigned long REST_REALIGN_MS = 300; ///< idle time after which the dial is assumed to sit in a detent
    static constexpr int16_t COUNTER_FOLD_LIMIT = 16000; ///< fold the 16-bit hardware counter into the 32-bit total

    /**
     * @brief Total edge count since `begin()` (32 bit, hardware counter folded in when large).
     */
    int32_t readEdges();

    long clampValue(long value) const;

    int pinA;
    int pinB;
    pcnt_unit_t unit;
    bool started = false;

    int32_t foldedEdges = 0;       ///< edges moved out of the 16-bit hardware counter
    int32_t lastEdges = 0;         ///< edge count seen at the previous update()
    int32_t detentEdges = 0;       ///< edge count of the current detent position
    unsigned long lastEdgeChangeMs = 0;

    long value = 0;
    long minValue = -100;
    long maxValue = 100;
    long stepValue = 1;
    int direction = 1;
};
