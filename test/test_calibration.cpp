// Compile-time checks execute the same constexpr conversion used by firmware.
#include "UltrasonicSensor.h"
constexpr float absDelta(float a, float b) { return a > b ? a-b : b-a; }
constexpr bool nan(float v) { return v != v; }
constexpr auto known = convertEcho(1000, 0.0343f, 2, 600);
static_assert(calibration::ultrasonicOffsetCm == 2.274955, "Frozen offset changed");
static_assert(known.valid() && known.durationUs == 1000, "Valid metadata changed");
static_assert(absDelta(known.rawDistanceCm, 17.15f) < 0.00001f, "Raw conversion changed");
static_assert(absDelta(known.distanceCm, 19.424955f) < 0.00001f, "Correction missing or doubled");
constexpr auto repeated = convertEcho(1000, 0.0343f, 2, 600);
static_assert(known.distanceCm == repeated.distanceCm, "Correction accumulated");
constexpr auto timeout = convertEcho(0, 0.0343f, 2, 600);
static_assert(timeout.status == MeasurementStatus::Timeout && nan(timeout.distanceCm) && nan(timeout.rawDistanceCm), "Timeout must remain invalid");
constexpr auto shortPulse = convertEcho(1, 0.0343f, 2, 600);
constexpr auto longPulse = convertEcho(40000, 0.0343f, 2, 600);
static_assert(shortPulse.status == MeasurementStatus::OutOfRange && nan(shortPulse.distanceCm) && nan(shortPulse.rawDistanceCm), "Offset must not rescue invalid raw range");
static_assert(longPulse.status == MeasurementStatus::OutOfRange && nan(longPulse.distanceCm) && nan(longPulse.rawDistanceCm), "Long pulse must remain invalid");
constexpr Measurement initialHigh{MeasurementStatus::EchoHighBeforeTrigger, 0, std::numeric_limits<float>::quiet_NaN()};
static_assert(!initialHigh.valid() && nan(initialHigh.distanceCm) && nan(initialHigh.rawDistanceCm), "Initial HIGH handling changed");
constexpr auto lower = convertEcho(100, 0.04f, 2, 600);
constexpr auto upper = convertEcho(30000, 0.04f, 2, 600);
static_assert(lower.valid() && upper.valid(), "Raw boundary validation changed");
static_assert(upper.distanceCm > 600 && upper.rawDistanceCm == 600, "Do not clamp corrected boundary");
static_assert(!convertEcho(99, 0.04f, 2, 600).valid(), "Below raw boundary must fail");
static_assert(!convertEcho(30001, 0.04f, 2, 600).valid(), "Above raw boundary must fail");
constexpr auto speedInvalid = convertEcho(1000, -0.0343f, 2, 600);
static_assert(!speedInvalid.valid() && nan(speedInvalid.rawDistanceCm) && nan(speedInvalid.distanceCm), "Invalid conversion must not be calibrated");
