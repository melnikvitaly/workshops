#include <Arduino.h>

#define LIGHT_PIN 9

void setup()
{
    Serial.begin(115200);
    analogReadResolution(12);       // default value
    analogSetAttenuation(ADC_11db); // default value
}

uint16_t lvlMin = (uint16_t)5000;
uint16_t lvlMax = (uint16_t)0;

void trackMinMax(uint16_t value)
{
    if (value < lvlMin)
        lvlMin = value;
    if (value > lvlMax)
        lvlMax = value;
}

void loop()
{

    uint16_t lightNum = analogRead(LIGHT_PIN);
    uint16_t lightV = analogReadMilliVolts(LIGHT_PIN);
    trackMinMax(lightV);

    Serial.println(String("lvl=") + lightNum + String("V=") + lightV + String("mV. ") + String("MaxLvl=") + lvlMax + String(" MinLvl=") + lvlMin);
    delay(100);
}