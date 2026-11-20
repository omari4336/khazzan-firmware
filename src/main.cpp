#include <Arduino.h>
#include "UltrasonicSensor.h"
#include "config.h"

namespace {
UltrasonicSensor sensor;
uint32_t previousMeasurementMs = 0;
}

void setup() {
    Serial.begin(config::serialBaud);
    sensor.begin();
    delay(config::startupDelayMs);
    Serial.println("Khazzan Phase 1 | JSN-SR20-Y3.0 assumptions UNVERIFIED");
    Serial.printf("TRIG=%u ECHO=%u timeout_us=%lu interval_ms=%lu\n",
                  static_cast<unsigned>(config::trigPin),
                  static_cast<unsigned>(config::echoPin),
                  static_cast<unsigned long>(config::echoTimeoutUs),
                  static_cast<unsigned long>(config::measurementIntervalMs));
    previousMeasurementMs = millis() - config::measurementIntervalMs;
}

void loop() {
    const uint32_t now = millis();
    // Unsigned subtraction remains correct across millis() wraparound.
    if (static_cast<uint32_t>(now - previousMeasurementMs) <
        config::measurementIntervalMs) {
        delay(1);
        return;
    }
    previousMeasurementMs = now;
    const Measurement reading = sensor.read();
    Serial.printf("uptime_ms=%lu ", static_cast<unsigned long>(now));
    switch (reading.status) {
    case MeasurementStatus::Ok:
        Serial.printf("status=OK echo_us=%lu distance_cm=%.2f\n",
                      static_cast<unsigned long>(reading.durationUs), reading.distanceCm);
        break;
    case MeasurementStatus::Timeout:
        Serial.println("status=TIMEOUT distance_cm=NA");
        break;
    case MeasurementStatus::EchoHighBeforeTrigger:
        Serial.println("status=ECHO_HIGH_BEFORE_TRIGGER distance_cm=NA");
        break;
    case MeasurementStatus::OutOfRange:
        Serial.printf("status=OUT_OF_RANGE echo_us=%lu distance_cm=NA\n",
                      static_cast<unsigned long>(reading.durationUs));
        break;
    }
}
