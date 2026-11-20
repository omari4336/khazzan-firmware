#pragma once
#include <cstdint>

namespace config {
inline constexpr uint32_t serialBaud = 115200;
inline constexpr uint8_t trigPin = 5;
inline constexpr uint8_t echoPin = 18;
inline constexpr uint32_t triggerLowUs = 5;
inline constexpr uint32_t triggerHighUs = 10;
inline constexpr uint32_t echoTimeoutUs = 40000;
inline constexpr uint32_t startupDelayMs = 1000;
inline constexpr uint32_t measurementIntervalMs = 2000;
// Engineering starting values, NOT verified JSN-SR20-Y3.0 specifications.
inline constexpr float soundSpeedCmPerUs = 0.0343f;
inline constexpr float minDistanceCm = 2.0f;
inline constexpr float maxDistanceCm = 600.0f;
static_assert(trigPin != echoPin, "TRIG and ECHO must differ");
static_assert(triggerHighUs > 0 && triggerHighUs < 1000, "Invalid trigger width");
static_assert(echoTimeoutUs > 0 && echoTimeoutUs < 1000000, "Invalid timeout");
static_assert(measurementIntervalMs > 0 && measurementIntervalMs < 0x80000000UL,
              "Invalid measurement interval");
static_assert(soundSpeedCmPerUs > 0 && minDistanceCm > 0 &&
              maxDistanceCm > minDistanceCm, "Invalid conversion limits");
}
