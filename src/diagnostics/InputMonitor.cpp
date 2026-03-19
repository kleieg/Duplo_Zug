#include <Arduino.h>

namespace
{
    constexpr int ADC_PIN = 7;
    constexpr int ADC_BURST_SAMPLES = 21;
    constexpr unsigned long PRINT_INTERVAL_MS = 100;

    struct AdcSnapshot
    {
        int firstValue;
        int medianValue;
        int averageValue;
        int lastValue;
        int minValue;
        int maxValue;
    };

    void sortAscending(int *values, int count)
    {
        for (int outer = 1; outer < count; outer++)
        {
            const int key = values[outer];
            int inner = outer - 1;

            while (inner >= 0 && values[inner] > key)
            {
                values[inner + 1] = values[inner];
                inner--;
            }

            values[inner + 1] = key;
        }
    }

    AdcSnapshot sampleAdc()
    {
        int samples[ADC_BURST_SAMPLES];
        long sum = 0;

        AdcSnapshot snapshot = {0, 0, 0, 0, 4095, 0};

        for (int index = 0; index < ADC_BURST_SAMPLES; index++)
        {
            const int value = analogRead(ADC_PIN);
            samples[index] = value;

            if (index == 0)
            {
                snapshot.firstValue = value;
            }

            snapshot.lastValue = value;
            if (value < snapshot.minValue)
            {
                snapshot.minValue = value;
            }
            if (value > snapshot.maxValue)
            {
                snapshot.maxValue = value;
            }

            sum += value;
            delayMicroseconds(200);
        }

        sortAscending(samples, ADC_BURST_SAMPLES);

        snapshot.medianValue = samples[ADC_BURST_SAMPLES / 2];
        snapshot.averageValue = static_cast<int>(sum / ADC_BURST_SAMPLES);
        return snapshot;
    }
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(300);

    analogReadResolution(12);
    analogSetPinAttenuation(ADC_PIN, ADC_11db);
    analogSetClockDiv(255);
    adcAttachPin(ADC_PIN);
    pinMode(ADC_PIN, INPUT);

    Serial.println();
    Serial.println("=== Raw ADC Monitor ===");
    Serial.printf("ADC pin: %d\n", ADC_PIN);
    Serial.printf("Burst samples: %d\n", ADC_BURST_SAMPLES);
    Serial.println("Press and hold a button. Only raw ADC statistics are shown.");
    Serial.println("Columns: first median avg last min max spread");
}

void loop()
{
    static unsigned long lastPrintMs = 0;

    const unsigned long now = millis();
    if (now - lastPrintMs < PRINT_INTERVAL_MS)
    {
        delay(2);
        return;
    }
    lastPrintMs = now;

    const AdcSnapshot adc = sampleAdc();
    Serial.printf("ADC: first=%4d median=%4d avg=%4d last=%4d min=%4d max=%4d spread=%3d\n",
                  adc.firstValue,
                  adc.medianValue,
                  adc.averageValue,
                  adc.lastValue,
                  adc.minValue,
                  adc.maxValue,
                  adc.maxValue - adc.minValue);
}