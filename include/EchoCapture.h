#pragma once
#include "UltrasonicSensor.h"

struct EchoEdge { uint32_t offsetUs; uint8_t level; };
// Exact post-solder edge traversal and status precedence, extracted for testing.
// Input is the ISR snapshot in acquisition order; no filtering or reordering.
inline constexpr Measurement decodeEcho(uint8_t initialLevel, const EchoEdge* edges,
                                        uint16_t count, uint32_t total) {
    bool repeated = false, pending = initialLevel == 1, hasRise = false;
    uint8_t previous = initialLevel;
    uint32_t rise = 0, pulse = 0, complete = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const EchoEdge edge = edges[i];
        if (edge.level == previous) repeated = true;
        previous = edge.level;
        if (edge.level == 1) { pending = true; hasRise = true; rise = edge.offsetUs; }
        else if (pending) {
            if (hasRise) { pulse = edge.offsetUs - rise; ++complete; }
            pending = false; hasRise = false;
        }
    }
    MeasurementStatus status = MeasurementStatus::Ok;
    if (total > count || repeated) status = MeasurementStatus::CaptureIncomplete;
    else if (initialLevel == 1) status = MeasurementStatus::EchoHighBeforeTrigger;
    else if (!count) status = MeasurementStatus::Timeout;
    else if (pending) status = MeasurementStatus::IncompleteHighPulse;
    else if (complete != 1 || count != 2 || !pulse) status = MeasurementStatus::InvalidPulseSequence;
    if (status != MeasurementStatus::Ok) {
        return {status, pulse, std::numeric_limits<float>::quiet_NaN()};
    }
    // Same float conversion and inclusive 2â€“600 cm raw plausibility limits.
    // convertEcho owns the one and only valid-measurement calibration addition.
    return convertEcho(pulse, 0.0343f, 2.0f, 600.0f);
}
