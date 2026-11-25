# Khazzan ESP32 Firmware

Khazzan is a graduation-project prototype for monitoring household water tanks in Jordan, using ESP32-WROOM-32, JSN-SR20-Y3.0 ultrasonic sensing, DS3231 RTC and MicroSD.

## Production firmware

The default `esp32dev` environment uses the experimentally validated **25 us trigger and 70 ms interrupt-based ECHO capture**, every two seconds. It preserves raw distance and applies the frozen **+2.274955 cm** correction once to valid readings. RTC timestamps and raw/corrected distances are appended to a uniquely named CSV with per-row read-back verification. RTC time is never set automatically; SD is never formatted.

```sh
pio run -e esp32dev
```

Use local serial-port overrides rather than committing machine-specific settings. See [wiring](docs/wiring.md), [CSV schema v2](docs/CSV_SCHEMA_V2.md), and the [production consolidation and validation report](docs/PRODUCTION_CONSOLIDATION.md).

## Validation status

Historical calibration used 30/50/71/100 cm and independent validation used 40/60/90 cm: 210 controlled flat-wall measurements in total. All independent validation criteria passed using the frozen offset offline. The consolidated production code passed builds for all nine environments, 210 historical edge replays and invalid-capture regression checks. **The consolidated binary has not been uploaded or physically tested.** No accuracy beyond the tested 30â€“100 cm positions or on water surfaces is claimed.

Historical diagnostic environments remain separate, including `khazzan_post_solder_test`. No Wi-Fi, deep sleep, filtering, invented tank geometry or water-level calculation is included.

## Contributing

Follow the [team contribution guide](CONTRIBUTING.md): use focused branches and pull requests, document software versus hardware verification, and keep raw device logs and local configuration out of commits.
