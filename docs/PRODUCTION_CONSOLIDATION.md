# Khazzan V1.0 production consolidation and calibration

## Production behavior

The default esp32dev environment now integrates JSN-SR20-Y3.0 ultrasonic acquisition, DS3231 RTC and MicroSD logging. It replaces the original 10 us trigger/pulseIn path with the validated 25 us trigger and 70 ms interrupt observation window. Measurements repeat every two seconds continuously; historical finite diagnostics remain separate and unchanged.

Pins: TRIG5, ECHO18 through the existing 10k/15.1k divider; RTC SDA21/SCL22; SD SCK14/MISO19/MOSI23/CS27 at 1 MHz. Serial speed remains 115200. RTC time is never adjusted. GPIO mappings and wiring were not changed.

The ISR and trigger function bodies match the validated post-solder source. The extracted decoder retains status precedence for overflow/repeated levels, initial HIGH, no edges, incomplete HIGH, invalid sequence/zero width and raw range failure. No printing occurs inside the ISR. Only a clean, valid pulse reaches convertEcho. That conversion preserves rawDistanceCm and adds the frozen 2.274955 cm offset exactly once to distanceCm. No filtering, smoothing, clamping, tank dimensions or water-level calculation was introduced.

RTC register/BCD/calendar/OSF validation is reused. A progression failure explicitly blanks the timestamp. SD initialization is independent; each attempt can be logged with explicit invalid statuses. A new uniquely named schema-v2 CSV is created without touching historical records. Every row is appended, flushed, closed, reopened and byte-verified. Partial writes or failed row verification disable further writes to that file. No format/delete operation is used. Production does not retain a growing array of expected rows.

See [CSV schema v2](CSV_SCHEMA_V2.md). Local serial port overrides are intentionally absent from repository configuration; supply them locally when hardware access is explicitly authorized.

## Software verification actually completed

- All nine environments compiled successfully, with no warnings in the final build: esp32dev, serial_test, ultrasonic_diag, trig_voltage_test, ultrasonic_diag_25us, sd_card_test, rtc_test, khazzan_integration_test and khazzan_post_solder_test.
- Xtensa C++17 compile-time calibration/invalid-edge checks passed. The original runtime assert-based conversion test was compile-checked only; no runtime execution is claimed.
- All 210 preserved historical edge traces were replayed through the production decoder; pulse duration, raw distance and exactly-once corrected distance agreed within 0.00005 cm float tolerance.
- A compiler-evaluated differential comparison against the original validated decoder passed 508 synthetic level-pattern/initial-level/overflow combinations.
- The centralized coefficient and all historical diagnostic source/evidence files stayed unchanged. All eight historical environments retain equivalent effective build settings after moving shared settings to khazzan_base.
- A 194-file SHA-256-verified backup was created before edits. Five intended existing files changed and 189 remained unchanged. The local guarded rollback script checks current/backup hashes before restoring those five files. Backups, build artifacts, raw device logs and local review scripts remain local.

Build command (from a configured PlatformIO shell):

```sh
pio run -e esp32dev -e serial_test -e ultrasonic_diag -e trig_voltage_test -e ultrasonic_diag_25us -e sd_card_test -e rtc_test -e khazzan_integration_test -e khazzan_post_solder_test
```

Platform espressif32 6.9.0; Arduino 3.20017.241212; Xtensa 8.4.0+2021r2-patch5; RTClib2.1.4, BusIO1.17.4. Existing verified library caches were reused. An initial dependency-inheritance attempt caused SPI.h errors in unrelated environments; the final base-section separation fixed them. Failed and final build logs remain locally preserved.

| Resource | Calibrated legacy production | Consolidated production |
|---|---:|---:|
| Static RAM | 21,472 bytes | 25,048 / 327,680 bytes (7.6%) |
| Flash | 270,489 bytes | 376,529 / 1,310,720 bytes (28.7%) |

These are linker sizes, not runtime heap/stack guarantees. The consolidated binary has NOT been uploaded or tested on hardware; all verification of this change was software-only.

## Historical controlled calibration and validation

These results preceded consolidation; they are not new tests of the consolidated binary. Four calibration references and three independent validation references supplied 30 measurements each. The frozen correction was applied offline during validation and was not refitted from validation data. No measurement was discarded. The aligned 71 cm dataset is used; an earlier failed obstructed 71 cm run remains separately preserved.

| Dataset | Reference cm | Raw mean cm | Corrected mean cm | Corrected bias cm | Population SD cm |
|---|---:|---:|---:|---:|---:|
| calibration | 30 | 27.925917 | 30.200872 | +0.200872 | 0.016765 |
| calibration | 50 | 47.879370 | 50.154325 | +0.154325 | 0.012046 |
| calibration | 71 | 68.149527 | 70.424482 | -0.575518 | 0.017676 |
| calibration | 100 | 97.945365 | 100.220320 | +0.220320 | 0.012796 |
| validation | 40 | 38.037557 | 40.312512 | +0.312512 | 0.010786 |
| validation | 60 | 58.068185 | 60.343140 | +0.343140 | 0.109577 |
| validation | 90 | 87.382680 | 89.657635 | -0.342365 | 0.012046 |

Independent validation at 40/60/90 cm passed each predeclared criterion: 100% valid, absolute corrected mean error <=1 cm, maximum corrected error <=2 cm, population SD <=0.5 cm, zero unexpected resets and zero logging failures. All 210 SD read-back rows matched live serial data; RTC progression/OSF checks passed in the historical runs.

| Pooled independent validation, n=90 | Raw cm | Corrected cm |
|---|---:|---:|
| Bias | -2.170526 | 0.104429 |
| MAE | 2.170526 | 0.332672 |
| RMSE | 2.194366 | 0.339063 |
| Maximum absolute error | 2.655050 | 0.687855 |

MAE improvement: 84.67%; RMSE improvement: 84.55%. Calibration and independent validation remain distinct. Full raw evidence and hashes are preserved locally, excluded from commits per CONTRIBUTING.md.

## Limits and rollback

Accuracy evidence covers the tested stationary flat-wall positions within 30â€“100 cm, not every distance, a manufacturer-rated range, or actual water surfaces. Water/foam/ripples, tank reflections, condensation, environmental variation, remounting reproducibility, blind-zone boundaries and long-duration behavior require separate physical tests. Supply/divider voltages, soldering safety, brownouts, SD power-loss durability and RTC battery retention cannot be established by compilation.

For repository-level rollback, revert this update commit on a new branch and rebuild; do not reset or delete historical evidence. The local verified backup and guarded rollback script also restore the pre-consolidation calibrated production files. No upload is part of rollback. Hardware deployment requires separate authorization and validation.
