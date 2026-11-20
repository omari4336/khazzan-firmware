#include "UltrasonicSensor.h"
#include <cassert>
#include <cmath>
#include <initializer_list>

int main() {
    const auto timeout = convertEcho(0, 0.0343f, 2, 600);
    assert(timeout.status == MeasurementStatus::Timeout && std::isnan(timeout.distanceCm));
    const auto valid = convertEcho(1000, 0.0343f, 2, 600);
    assert(valid.valid() && std::fabs(valid.distanceCm - 17.15f) < 0.001f);
    for (const auto pulse : {1u, 40000u}) {
        const auto invalid = convertEcho(pulse, 0.0343f, 2, 600);
        assert(invalid.status == MeasurementStatus::OutOfRange && std::isnan(invalid.distanceCm));
    }
    assert(convertEcho(100, 0.04f, 2, 600).valid());
    assert(convertEcho(30000, 0.04f, 2, 600).valid());
}
