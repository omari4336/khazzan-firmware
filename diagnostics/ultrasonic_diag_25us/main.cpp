#include <Arduino.h>
#include <driver/gpio.h>

namespace {
constexpr uint8_t trigPin = 5;
constexpr uint8_t echoPin = 18;
constexpr uint32_t windowUs = 70000;
constexpr uint32_t intervalMs = 2000;
constexpr uint16_t capacity = 128;

struct Edge { uint32_t offsetUs; uint8_t level; };
volatile Edge edges[capacity];
Edge snapshot[capacity];
portMUX_TYPE captureMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool capturing = false;
volatile uint32_t originUs = 0;
volatile uint32_t transitionCount = 0;
volatile uint16_t storedCount = 0;
bool windowPending = false;
uint8_t initialLevel = LOW;
uint32_t startedMs = 0;
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

void reportWindow() {
    portENTER_CRITICAL(&captureMux);
    capturing = false;
    const uint16_t count = storedCount;
    const uint32_t total = transitionCount;
    for (uint16_t i = 0; i < count; ++i) {
        snapshot[i] = {edges[i].offsetUs, edges[i].level};
    }
    portEXIT_CRITICAL(&captureMux);
    windowPending = false;

    const bool overflow = total > count;
    bool highObserved = initialLevel == HIGH;
    bool highPending = initialLevel == HIGH;
    bool hasCapturedRise = false;
    uint32_t riseUs = 0;
    uint32_t completePulses = 0;
    uint32_t risingEdges = 0;
    uint8_t previousLevel = initialLevel;
    bool repeatedLevel = false;

    Serial.printf("sample=%lu initial_level=%u window_us=%lu transitions=%lu stored=%u overflow=%s trigger_low_command_offset_us=%lu\n",
                  static_cast<unsigned long>(sequence), initialLevel,
                  static_cast<unsigned long>(windowUs), static_cast<unsigned long>(total),
                  static_cast<unsigned>(count), overflow ? "YES" : "NO",
                  static_cast<unsigned long>(triggerLowOffsetUs));
    for (uint16_t i = 0; i < count; ++i) {
        const Edge edge = snapshot[i];
        Serial.printf("  edge=%u t_us=%lu level=%u\n", static_cast<unsigned>(i + 1),
                      static_cast<unsigned long>(edge.offsetUs), edge.level);
        if (edge.level == previousLevel) repeatedLevel = true;
        previousLevel = edge.level;
        if (edge.level == HIGH) {
            highObserved = true;
            highPending = true;
            hasCapturedRise = true;
            riseUs = edge.offsetUs;
            ++risingEdges;
        } else if (highPending) {
            if (hasCapturedRise) {
                ++completePulses;
                Serial.printf("  high_pulse_us=%lu\n",
                              static_cast<unsigned long>(edge.offsetUs - riseUs));
            } else {
                Serial.printf("  initial_HIGH_partial_us=%lu (rise_before_capture_or_boundary_race)\n",
                              static_cast<unsigned long>(edge.offsetUs));
            }
            highPending = false;
            hasCapturedRise = false;
        }
    }
    if (highPending) {
        Serial.printf("  HIGH_open_at_window_end observed_partial_us=%lu\n",
                      static_cast<unsigned long>(windowUs - riseUs));
    }
    Serial.printf("  high_observed=%s rising_edges=%lu complete_high_pulses=%lu repeated_level=%s status=%s\n",
                  highObserved ? "YES" : "NO", static_cast<unsigned long>(risingEdges),
                  static_cast<unsigned long>(completePulses), repeatedLevel ? "YES" : "NO",
                  (overflow || repeatedLevel) ? "CAPTURE_INCOMPLETE" :
                  (highObserved ? "HIGH_OBSERVED" : "NO_HIGH_OBSERVED"));
}
}

void setup() {
    Serial.begin(115200);
    digitalWrite(trigPin, LOW);
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    attachInterrupt(digitalPinToInterrupt(echoPin), onEchoChange, CHANGE);
    delay(1000);
    Serial.println("Khazzan ultrasonic_diag_25us | TRIG=5 ECHO=18 trigger_high_us=25 window_us=70000 | Y1 compatibility hypothesis UNVERIFIED | software edge capture only");
    startedMs = millis() - intervalMs;
}

void loop() {
    if (windowPending) {
        if (static_cast<uint32_t>(micros() - originUs) >= windowUs) reportWindow();
    } else if (static_cast<uint32_t>(millis() - startedMs) >= intervalMs) {
        startedMs = millis();
        startWindow();
    }
    delay(1);
}
