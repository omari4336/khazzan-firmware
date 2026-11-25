#include "EchoCapture.h"
constexpr bool isNaN(float value) { return value != value; }
constexpr EchoEdge valid[] = {{6675,1},{10059,0}};
constexpr auto good = decodeEcho(0, valid, 2, 2);
static_assert(good.valid() && good.durationUs==3384, "Complete echo not decoded");
static_assert(good.rawDistanceCm==convertEcho(3384,0.0343f,2,600).rawDistanceCm &&
              good.distanceCm==convertEcho(3384,0.0343f,2,600).distanceCm, "Calibration applied more than once");
constexpr auto noEdges=decodeEcho(0,nullptr,0,0);
static_assert(noEdges.status==MeasurementStatus::Timeout && isNaN(noEdges.distanceCm) && isNaN(noEdges.rawDistanceCm), "Timeout changed");
static_assert(decodeEcho(1,nullptr,0,0).status==MeasurementStatus::EchoHighBeforeTrigger,"Initial HIGH priority changed");
constexpr EchoEdge rise[]={{100,1}};
static_assert(decodeEcho(0,rise,1,1).status==MeasurementStatus::IncompleteHighPulse,"Missing falling edge accepted");
constexpr EchoEdge repeated[]={{100,1},{200,1},{300,0}};
static_assert(decodeEcho(0,repeated,3,3).status==MeasurementStatus::CaptureIncomplete,"Repeated level accepted");
static_assert(decodeEcho(0,valid,2,129).status==MeasurementStatus::CaptureIncomplete,"Overflow accepted");
static_assert(decodeEcho(1,valid,2,129).status==MeasurementStatus::CaptureIncomplete,"Overflow precedence changed");
constexpr EchoEdge multiple[]={{100,1},{200,0},{300,1},{400,0}};
static_assert(decodeEcho(0,multiple,4,4).status==MeasurementStatus::InvalidPulseSequence,"Multiple pulses accepted");
constexpr EchoEdge zero[]={{100,1},{100,0}};
static_assert(decodeEcho(0,zero,2,2).status==MeasurementStatus::InvalidPulseSequence,"Zero-width pulse accepted");
constexpr EchoEdge tooShort[]={{100,1},{101,0}};
constexpr auto invalidRange=decodeEcho(0,tooShort,2,2);
static_assert(invalidRange.status==MeasurementStatus::OutOfRange && invalidRange.durationUs==1 && isNaN(invalidRange.distanceCm) && isNaN(invalidRange.rawDistanceCm),"Offset rescued invalid raw range");
constexpr EchoEdge tooLong[]={{100,1},{40100,0}};
static_assert(decodeEcho(0,tooLong,2,2).status==MeasurementStatus::OutOfRange,"Out-of-range echo accepted");
constexpr EchoEdge fallingOnly[]={{100,0}};
static_assert(decodeEcho(0,fallingOnly,1,1).status==MeasurementStatus::CaptureIncomplete,"Unexpected falling edge priority changed");
constexpr EchoEdge initialHighFall[]={{100,0}};
static_assert(decodeEcho(1,initialHighFall,1,1).status==MeasurementStatus::EchoHighBeforeTrigger,"Initial high pulse accepted");
