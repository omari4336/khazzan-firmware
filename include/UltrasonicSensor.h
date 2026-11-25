#pragma once
#include <cstdint>
#include <limits>
#include "Calibration.h"

enum class MeasurementStatus {
    Ok, Timeout, EchoHighBeforeTrigger, OutOfRange,
    CaptureIncomplete, IncompleteHighPulse, InvalidPulseSequence
};
inline constexpr const char* measurementStatusName(MeasurementStatus status) {
    switch (status) {
    case MeasurementStatus::Ok: return "OK";
    case MeasurementStatus::Timeout: return "TIMEOUT_NO_HIGH_OBSERVED";
    case MeasurementStatus::EchoHighBeforeTrigger: return "INITIAL_HIGH";
    case MeasurementStatus::OutOfRange: return "OUT_OF_RANGE";
    case MeasurementStatus::CaptureIncomplete: return "CAPTURE_INCOMPLETE";
    case MeasurementStatus::IncompleteHighPulse: return "INCOMPLETE_HIGH_PULSE";
    case MeasurementStatus::InvalidPulseSequence: return "INVALID_PULSE_SEQUENCE";
    }
    return "UNKNOWN";
}

struct Measurement {
    MeasurementStatus status;
    uint32_t durationUs;
    // Invalid readings always carry NaN, never a plausible zero distance.
    // Corrected distance for downstream consumers; raw value remains separate.
    float distanceCm;
    float rawDistanceCm = std::numeric_limits<float>::quiet_NaN();
    constexpr bool valid() const { return status == MeasurementStatus::Ok; }
};

// Pure conversion is independent of Arduino for host testing.
inline constexpr Measurement convertEcho(uint32_t durationUs, float speed,
                               float minCm, float maxCm) {
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    if (durationUs == 0) return {MeasurementStatus::Timeout, 0, invalid};
    const float distance = static_cast<float>(durationUs) * speed / 2.0f;
    if (!(distance >= minCm && distance <= maxCm)) {
        return {MeasurementStatus::OutOfRange, durationUs, invalid};
    }
    // The only correction site: derive from the immutable raw conversion once.
    // Raw range validation above remains unchanged; do not clamp corrected data.
    const float corrected = static_cast<float>(
        static_cast<double>(distance) + calibration::ultrasonicOffsetCm);
    return {MeasurementStatus::Ok, durationUs, corrected, distance};
}

class UltrasonicSensor {
public:
    void begin();
    void start();
    bool ready() const;
    Measurement finish();
};
