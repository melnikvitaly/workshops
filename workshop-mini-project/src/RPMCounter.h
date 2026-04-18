#pragma once
/// @brief Calculates RPM based on light resistor voltage oscillations
/// by measuring distance between extremum
/// AI-generated
class RPMCounter
{
private:
    ADC &sensor;
    Debug &dbg;

    // Time tracking for RPM calculation
    unsigned long lastExtremumTime = 0;
    unsigned long currentRPM = 0;

    // State variables for peak/valley detection
    uint8_t lastExtremumValue = 0;
    bool isRising = true;

    // Hysteresis threshold to ignore minor signal jitter (adjust based on sensor variance)
    const uint8_t HYSTERESIS = 5;

    // Moving average filter variables
    uint8_t bufferIndex = 0;
    uint16_t bufferSum = 0;
    const uint8_t WINDOW_SIZE = 10; // Use a 10-sample window from your 100-byte buffer

    // Timeout: If no peaks are detected in this time, RPM drops to 0
    const unsigned long TIMEOUT_MS = 20000;

public:
    uint8_t buffer[100] = {0};

    RPMCounter(ADC &sensor, Debug &dbg) : sensor(sensor), dbg(dbg)
    {
    }

    void tick()
    {
        unsigned long now = millis();
        auto newValue = sensor.percent();

        // 1. Moving Average Filter (Smooths the analog noise)
        bufferSum -= buffer[bufferIndex];
        buffer[bufferIndex] = newValue;
        bufferSum += newValue;
        bufferIndex = (bufferIndex + 1) % WINDOW_SIZE;

        uint8_t smoothedValue = bufferSum / WINDOW_SIZE;

        // 2. Extremum Detection Logic
        if (isRising)
        {
            // Looking for a PEAK
            if (smoothedValue < lastExtremumValue - HYSTERESIS)
            {
                // Signal was rising, but just dropped below the threshold. Peak found!
                isRising = false;

                if (lastExtremumTime > 0)
                {
                    unsigned long deltaMs = now - lastExtremumTime;
                    if (deltaMs > 0)
                    {
                        // Calculate RPM: (1 revolution / delta ms) * (60,000 ms / 1 minute)
                        // Note: This assumes 1 peak = 1 full revolution.
                        currentRPM = 60000 / deltaMs;
                        dbg.print(String("RPM=") + currentRPM + " , DeltaMS=" + deltaMs);
                    }
                }

                lastExtremumTime = now;
                lastExtremumValue = smoothedValue; // Reset threshold to track the valley
            }
            else if (smoothedValue > lastExtremumValue)
            {
                // Still rising, push the peak higher
                lastExtremumValue = smoothedValue;
            }
        }
        else
        {
            // Looking for a VALLEY
            if (smoothedValue > lastExtremumValue + HYSTERESIS)
            {
                // Signal was falling, but just rose above the threshold. Valley found!
                isRising = true;
                lastExtremumValue = smoothedValue; // Reset threshold to track the next peak

                // Note: If your target has multiple extremums per revolution (e.g., a fan
                // with multiple blades), you would also track time/deltas on valleys here.
            }
            else if (smoothedValue < lastExtremumValue)
            {
                // Still falling, push the valley lower
                lastExtremumValue = smoothedValue;
            }
        }

        // 3. Stopped Motor Handling
        // If the rotor stops, no new extremum trigger. We must force RPM to 0.
        if (currentRPM > 0 && (now - lastExtremumTime > TIMEOUT_MS))
        {
            currentRPM = 0;
            dbg.print(String("RPM=") + currentRPM);
        }
    }

    // Getter to safely retrieve the calculated RPM from your main loop
    unsigned long getRPM() const
    {
        return currentRPM;
    }
};