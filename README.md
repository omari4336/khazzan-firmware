# Khazzan ESP32 Firmware

Khazzan is a team capstone prototype for monitoring household water tanks in Jordan. The ESP32 captures ultrasonic echo timing. The integration firmware timestamps readings with a DS3231 RTC and writes them to a CSV file on microSD.

## Hardware and firmware

- ESP32-WROOM-32, JSN-SR20-Y3.0 ultrasonic sensor, DS3231 RTC and microSD module
- Arduino framework with C++17 and PlatformIO
- Separate diagnostic environments for the ultrasonic sensor, RTC, microSD and integration

## Build

Open this directory in VS Code with PlatformIO or run the commands below from a PlatformIO shell.

```sh
pio run -e esp32dev
pio run -e khazzan_integration_test
```

The default environment builds the ultrasonic firmware. The integration environment builds the timestamped CSV logging test. See [wiring](docs/wiring.md) before connecting hardware. For boards requiring a manual serial port, set your local port through PlatformIO.

## Validation

A team hardware integration test recorded and read back 15 of 15 timestamped ultrasonic measurements. This verified the logging path. Distance calibration and validation against known water levels are pending. Wi-Fi, backend services, and ML are outside this firmware release.