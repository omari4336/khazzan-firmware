#include <Arduino.h>
#include "UltrasonicSensor.h"
#include "config.h"

void UltrasonicSensor::begin() {
    // Preload LOW before enabling the output.
    digitalWrite(config::trigPin, LOW);
    pinMode(config::trigPin, OUTPUT);
    pinMode(config::echoPin, INPUT);
}

Measurement UltrasonicSensor::read() {
    if (digitalRead(config::echoPin) == HIGH) {
        return {MeasurementStatus::EchoHighBeforeTrigger, 0,
                std::numeric_limits<float>::quiet_NaN()};
    }
    digitalWrite(config::trigPin, LOW);
    delayMicroseconds(config::triggerLowUs);
    digitalWrite(config::trigPin, HIGH);
    delayMicroseconds(config::triggerHighUs);
    digitalWrite(config::trigPin, LOW);
    // Zero means no complete pulse was captured inside the bounded wait.
    const auto duration = pulseIn(config::echoPin, HIGH, config::echoTimeoutUs);
    return convertEcho(duration, config::soundSpeedCmPerUs,
                       config::minDistanceCm, config::maxDistanceCm);
}
