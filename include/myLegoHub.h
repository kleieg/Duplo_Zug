/**
 * @file myLegoHub.h
 * @brief Thin wrapper around `Lpf2Hub` adding DUPLO-specific helpers.
 *
 * Declares `myLegoHub`, a small extension of Legoino's base hub with convenience helpers for
 * activating DUPLO ports and parsing sensor data. `DuploHub` builds on this abstraction to offer
 * higher-level features.
 *
 * @author Ralf Zühlsdorff
 * @date 2025
 *
 * @copyright
 * MIT License
 */
#ifndef MY_LEGO_HUB_H
#define MY_LEGO_HUB_H

#include "Lpf2Hub.h"
#include "LegoinoCommon.h"

namespace DuploEnums
{
    /**
     * @brief DUPLO-specific Powered Up device identifiers reported by the hub.
     */
    enum struct DuploTrainDeviceType
    {
        DUPLO_TRAIN_BASE_MOTOR = 41,
        DUPLO_TRAIN_BASE_SPEAKER = 42,
        DUPLO_TRAIN_BASE_COLOR_SENSOR = 43,
        DUPLO_TRAIN_BASE_SPEEDOMETER = 44,
        VOLTAGE_SENSOR = 20,
        LIGHT = 8
    };

    /**
     * @brief Logical ports used by the DUPLO train hub firmware.
     *
     * These values match the port numbers expected by the LEGO Powered Up protocol.
     */
    enum struct DuploTrainHubPort
    {
        MOTOR = 0x00,
        LED = 0x11,
        SPEAKER = 0x01,
        COLOR = 0x12,
        SPEEDOMETER = 0x13,
        VOLTAGE = 0x14
    };

    /**
     * @brief Sound identifiers understood by the DUPLO train hub.
     */
    enum DuploSound
    {
        BRAKE = 3,
        STATION_DEPARTURE = 5,
        WATER_REFILL = 7,
        HORN = 9,
        STEAM = 10
    };

    /**
     * @brief Color IDs supported by the hub LED and color sensor parser.
     */
    enum DuploColor
    {
        BLACK = 0,
        PINK = 1,
        PURPLE = 2,
        BLUE = 3,
        LIGHT_BLUE = 4,
        CYAN = 5,
        GREEN = 6,
        YELLOW = 7,
        ORANGE = 8,
        RED = 9,
        WHITE = 10
    };

}
/**
 * @brief Callback signature for hub connection-state changes.
 */
typedef void (*ConnectionCallback)();

/**
 * @brief Callback signature for stable color detections.
 * @param color Parsed DUPLO color value.
 */
typedef void (*DetectedColorCallback)(DuploEnums::DuploColor);

/**
 * @brief Callback signature for speed sensor updates.
 * @param detectedSpeed Normalized train speed.
 */
typedef void (*DetectedSpeedCallback)(int detectedSpeed);

/**
 * @brief Callback signature for voltage sensor updates.
 * @param detectedVoltage Measured hub voltage in volts.
 */
typedef void (*DetectedVoltageCallback)(float detectedVoltage);

/**
 * @class myLegoHub
 * @brief DUPLO-focused facade over Legoino's `Lpf2Hub`.
 *
 * The class exposes only the higher-level helpers this project needs, keeping DUPLO-specific
 * message layouts in a single place instead of scattering them through `DuploHub`.
 */
class myLegoHub : public Lpf2Hub
{
public:
    /** @brief Enable the DUPLO speaker port. */
    void activateBaseSpeaker();

    /**
     * @brief Start playback of a DUPLO sound effect.
     * @param sound DUPLO sound identifier to send to the hub.
     */
    void playSound(byte sound);

    /** @brief Enable the on-hub RGB LED port. */
    void activateRgbLight();

    /**
     * @brief Apply a color to the on-hub RGB LED.
     * @param color FastLED/Legoino color identifier accepted by the hub.
     */
    void setLedColor(Color color);

    /**
     * @brief Map the raw speedometer count into a normalized range.
     * @param rawSpeed Raw speed reading as delivered by Legoino.
     * @param maxRawSpeed Positive full-scale reference value for normalization.
     * @return Normalized speed in the range `-100..100`.
     */
    int MapSpeedometer(int rawSpeed, int maxRawSpeed = 255);

    /**
     * @brief Convert raw voltage sensor bytes into volts.
     * @param pData Raw payload buffer received from the hub.
     * @return Measured voltage in volts.
     */
    float parseVoltage(uint8_t *pData);
};

#endif // MY_LEGO_HUB_H
