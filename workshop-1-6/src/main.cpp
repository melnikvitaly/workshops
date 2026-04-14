#include <Arduino.h>

#define LIGHT_PIN 9
constexpr uint16_t RESOLUTION = 12; // default value
constexpr uint16_t MAX = 1 << RESOLUTION;  // default value
constexpr float V_REF = 3.3f;

class ErrorBuffer
{
    static constexpr size_t BUFFER = 10;
    float errors[BUFFER];
    size_t nextIndex = 0;

public:
    void track(float error)
    {
        if (error < 0)
            error = -error;
        errors[nextIndex] = error;
        if (nextIndex >= BUFFER)
        {
            nextIndex = 0;
        }
    }

    float getMean()
    {
        float sum = 0;
        for (size_t i = 0; i < BUFFER; i++)
            sum += errors[i];
        return sum / BUFFER;
    }
};

void setup()
{
    Serial.begin(115200);
    analogReadResolution(RESOLUTION); 
    analogSetAttenuation(ADC_11db);  
}

uint16_t lvlMin = (uint16_t)5000;
uint16_t lvlMax = (uint16_t)0;
ErrorBuffer errorBuffer;

void trackMinMax(uint16_t value)
{
    if (value < lvlMin)
        lvlMin = value;
    if (value > lvlMax)
        lvlMax = value;
}

void loop()
{    
    
    uint16_t lightLvl = analogRead(LIGHT_PIN);
    float lightVCalculated = lightLvl * V_REF / MAX;
    float lightVRead = analogReadMilliVolts(LIGHT_PIN) / 1000.0;
    trackMinMax(lightLvl);
    errorBuffer.track(lightVRead - lightVCalculated);
    auto meanError = errorBuffer.getMean();
    Serial.printf("lvl=%4d  V=%.3fV  Vcalc=%.3fV  MaxLvl=%4d  MinLvl=%4d  MeanErr=%.2f Err%=%.1f\n",
                  lightLvl, lightVRead, lightVCalculated, lvlMax, lvlMin, meanError, meanError/(lightVRead) *100);

    delay(100);
}