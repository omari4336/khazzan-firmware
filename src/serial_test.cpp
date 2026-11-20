#include <Arduino.h>

// Serial-only board test. No sensor initialization or GPIO operations.
void setup() {
    Serial.begin(115200);
}

void loop() {
    Serial.println("Khazzan ESP32 ready");
    delay(2000);
}
