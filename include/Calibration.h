#pragma once

namespace calibration {
// Frozen after independent validation; never refit from live measurements.
inline constexpr double ultrasonicOffsetCm = 2.274955;
// Flat-wall references only: not an operational validity/clamping range.
inline constexpr float testedReferenceMinCm = 30.0f;
inline constexpr float testedReferenceMaxCm = 100.0f;
}
