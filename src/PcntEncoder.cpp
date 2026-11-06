/**
 * @file PcntEncoder.cpp
 * @brief Rotary encoder input via the ESP32 hardware pulse counter (PCNT).
 *
 * @copyright
 * MIT License
 */

#include "PcntEncoder.h"

PcntEncoder::PcntEncoder(int pinA, int pinB, pcnt_unit_t unit) : pinA(pinA), pinB(pinB), unit(unit)
{
}

bool PcntEncoder::begin(uint16_t glitchFilterApbCycles)
{
    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);

    // Full quadrature (x4) decoding: both channels count on both edges, direction from the
    // other signal's level. A leading B counts up (same sign as the previous library).
    pcnt_config_t config = {};
    config.unit = unit;
    config.counter_h_lim = INT16_MAX;
    config.counter_l_lim = INT16_MIN;

    config.channel = PCNT_CHANNEL_0;
    config.pulse_gpio_num = pinA;
    config.ctrl_gpio_num = pinB;
    config.pos_mode = PCNT_COUNT_DEC;
    config.neg_mode = PCNT_COUNT_INC;
    config.lctrl_mode = PCNT_MODE_REVERSE;
    config.hctrl_mode = PCNT_MODE_KEEP;
    if (pcnt_unit_config(&config) != ESP_OK)
    {
        return false;
    }

    config.channel = PCNT_CHANNEL_1;
    config.pulse_gpio_num = pinB;
    config.ctrl_gpio_num = pinA;
    config.pos_mode = PCNT_COUNT_INC;
    config.neg_mode = PCNT_COUNT_DEC;
    if (pcnt_unit_config(&config) != ESP_OK)
    {
        return false;
    }

    if (glitchFilterApbCycles > 1023)
    {
        glitchFilterApbCycles = 1023;
    }
    pcnt_set_filter_value(unit, glitchFilterApbCycles);
    pcnt_filter_enable(unit);

    pcnt_counter_pause(unit);
    pcnt_counter_clear(unit);
    pcnt_counter_resume(unit);

    foldedEdges = 0;
    lastEdges = 0;
    detentEdges = 0;
    lastEdgeChangeMs = millis();
    started = true;
    return true;
}

void PcntEncoder::setBoundaries(long minValue, long maxValue)
{
    this->minValue = minValue;
    this->maxValue = maxValue;
    value = clampValue(value);
}

void PcntEncoder::setStepValue(long stepValue)
{
    this->stepValue = stepValue;
}

void PcntEncoder::setDirection(int direction)
{
    this->direction = (direction < 0) ? -1 : 1;
}

int32_t PcntEncoder::readEdges()
{
    int16_t count = 0;
    pcnt_get_counter_value(unit, &count);

    // Keep the 16-bit hardware counter far away from its limits (where it would reset).
    if (count > COUNTER_FOLD_LIMIT || count < -COUNTER_FOLD_LIMIT)
    {
        pcnt_counter_pause(unit);
        pcnt_get_counter_value(unit, &count);
        pcnt_counter_clear(unit);
        pcnt_counter_resume(unit);
        foldedEdges += count;
        count = 0;
    }

    return foldedEdges + count;
}

bool PcntEncoder::update()
{
    if (!started)
    {
        return false;
    }

    const unsigned long now = millis();
    const int32_t edges = readEdges() * direction;

    if (edges != lastEdges)
    {
        lastEdges = edges;
        lastEdgeChangeMs = now;
    }

    // Take one step per detent; a click counts after DETENT_THRESHOLD_EDGES of its edges.
    int32_t delta = edges - detentEdges;
    long steps = 0;
    while (delta >= DETENT_THRESHOLD_EDGES)
    {
        steps++;
        detentEdges += EDGES_PER_DETENT;
        delta -= EDGES_PER_DETENT;
    }
    while (delta <= -DETENT_THRESHOLD_EDGES)
    {
        steps--;
        detentEdges -= EDGES_PER_DETENT;
        delta += EDGES_PER_DETENT;
    }

    // At rest the dial sits in a detent: re-align so lost edges cannot accumulate.
    if (delta != 0 && now - lastEdgeChangeMs >= REST_REALIGN_MS)
    {
        detentEdges = edges;
    }

    if (steps == 0)
    {
        return false;
    }

    const long newValue = clampValue(value + steps * stepValue);
    if (newValue == value)
    {
        return false; // at a boundary
    }
    value = newValue;
    return true;
}

long PcntEncoder::getEncoderValue() const
{
    return value;
}

void PcntEncoder::setEncoderValue(long value)
{
    this->value = clampValue(value);
}

long PcntEncoder::clampValue(long value) const
{
    if (value < minValue)
    {
        return minValue;
    }
    if (value > maxValue)
    {
        return maxValue;
    }
    return value;
}
