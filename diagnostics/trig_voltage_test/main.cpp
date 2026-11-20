#include <Arduino.h>

namespace {
constexpr uint8_t trigPin = 5;
constexpr uint8_t echoPin = 18;
constexpr uint32_t holdMs = 10000;
bool trigHigh = false;
uint32_t lastChangeMs = 0;
}

void setup() {
    // Preload LOW before enabling the output. ECHO is never driven.
    digitalWrite(trigPin, LOW);
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    Serial.begin(115200);
    Serial.println("TRIG_LOW");
    // Hold the initial LOW for 10 seconds before the first HIGH.
    lastChangeMs = millis();
}

void loop() {
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastChangeMs) >= holdMs) {
        trigHigh = !trigHigh;
        digitalWrite(trigPin, trigHigh ? HIGH : LOW);
        lastChangeMs = millis();
        Serial.println(trigHigh ? "TRIG_HIGH" : "TRIG_LOW");
    }
    delay(1);
}
