#include <Arduino.h>
#include <driver/gpio.h>
#include "UltrasonicSensor.h"
#include "EchoCapture.h"
#include "config.h"

namespace {
constexpr uint8_t trigPin = 5;
constexpr uint8_t echoPin = 18;
constexpr uint32_t windowUs = 70000;
constexpr uint32_t intervalMs = 2000;
constexpr uint16_t capacity = 128;

using Edge = EchoEdge;
volatile Edge edges[capacity];
Edge snapshot[capacity];
portMUX_TYPE captureMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool capturing = false;
volatile uint32_t originUs = 0;
volatile uint32_t transitionCount = 0;
volatile uint16_t storedCount = 0;
bool windowPending = false;
uint8_t initialLevel = LOW;
uint32_t triggerLowOffsetUs = 0;
uint32_t sequence = 0;

void IRAM_ATTR onEchoChange() {
    portENTER_CRITICAL_ISR(&captureMux);
    if (capturing) {
        const uint32_t offset = static_cast<uint32_t>(micros() - originUs);
        if (offset <= windowUs) {
            const uint8_t level = static_cast<uint8_t>(gpio_get_level(GPIO_NUM_18));
            ++transitionCount;
            if (storedCount < capacity) {
                edges[storedCount].offsetUs = offset;
                edges[storedCount].level = level;
                ++storedCount;
            }
        }
    }
    portEXIT_CRITICAL_ISR(&captureMux);
}

void startWindow() {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(5);
    portENTER_CRITICAL(&captureMux);
    initialLevel = static_cast<uint8_t>(gpio_get_level(GPIO_NUM_18));
    storedCount = 0;
    transitionCount = 0;
    originUs = micros();
    capturing = true;
    portEXIT_CRITICAL(&captureMux);
    // Origin is just before the requested trigger, not a measured pin edge.
    digitalWrite(trigPin, HIGH);
    // Y1-derived timing hypothesis; exact Y3.0 compatibility is unverified.
    delayMicroseconds(25);
    digitalWrite(trigPin, LOW);
    triggerLowOffsetUs = static_cast<uint32_t>(micros() - originUs);
    windowPending = true;
    ++sequence;
}




static_assert(config::triggerLowUs == 5 && config::soundSpeedCmPerUs == 0.0343f &&
              config::minDistanceCm == 2.0f && config::maxDistanceCm == 600.0f,
              "Validated conversion and low timing must match");
static_assert(config::trigPin == trigPin && config::echoPin == echoPin, "Validated pins must match");
static_assert(config::triggerHighUs == 25 && config::echoTimeoutUs == windowUs &&
              config::measurementIntervalMs == intervalMs, "Validated timing must match");
}
void UltrasonicSensor::begin() {
    digitalWrite(trigPin, LOW); pinMode(trigPin, OUTPUT); pinMode(echoPin, INPUT);
    attachInterrupt(digitalPinToInterrupt(echoPin), onEchoChange, CHANGE);
}
void UltrasonicSensor::start() { startWindow(); }
bool UltrasonicSensor::ready() const {
    return windowPending && static_cast<uint32_t>(micros() - originUs) >= windowUs;
}
Measurement UltrasonicSensor::finish() {
    portENTER_CRITICAL(&captureMux);
    capturing = false;
    const uint16_t count = storedCount;
    const uint32_t total = transitionCount;
    for (uint16_t i = 0; i < count; ++i) snapshot[i] = {edges[i].offsetUs, edges[i].level};
    portEXIT_CRITICAL(&captureMux);
    windowPending = false;
    return decodeEcho(initialLevel, snapshot, count, total);
}
