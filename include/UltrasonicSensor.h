#pragma once
#include <cstdint>
#include <limits>

enum class MeasurementStatus { Ok, Timeout, EchoHighBeforeTrigger, OutOfRange };

struct Measurement {
    MeasurementStatus status;
    uint32_t durationUs;
    // Invalid readings always carry NaN, never a plausible zero distance.
    float distanceCm;
    bool valid() const { return status == MeasurementStatus::Ok; }
};

// Pure conversion is independent of Arduino for host testing.
inline Measurement convertEcho(uint32_t durationUs, float speed,
                               float minCm, float maxCm) {
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    if (durationUs == 0) return {MeasurementStatus::Timeout, 0, invalid};
    const float distance = static_cast<float>(durationUs) * speed / 2.0f;
    if (!(distance >= minCm && distance <= maxCm)) {
        return {MeasurementStatus::OutOfRange, durationUs, invalid};
    }
    return {MeasurementStatus::Ok, durationUs, distance};
}

class UltrasonicSensor {
public:
    void begin();
    Measurement read();
};
