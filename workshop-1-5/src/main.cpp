#include <Arduino.h>

#define BUTTON_RIGHT 40

int16_t counter_right = 0;

void IRAM_ATTR reaction_right()
{
    counter_right++;
    Serial.println("\nRIGHT Button Pressed! Count: " + String(counter_right));
}

void setup()
{
    pinMode(BUTTON_RIGHT, INPUT);
    Serial.begin(115200);
    attachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT), reaction_right, FALLING);
    Serial.print("HELLO");
}

void loop()
{

    delay(250);
}