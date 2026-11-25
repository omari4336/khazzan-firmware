# Khazzan CSV schema v2

Production `esp32dev` creates a new `/khz_v2_<8-hex-random>_<3-digit-suffix>.csv` per boot. Existing paths are skipped, never overwritten. No format or delete operation is used. Historical `/post_*.csv` and `/khazzan_test.csv` retain their original raw-distance schemas and records.

Header, in exact order:

```csv
schema_version,sample,timestamp,echo_us,raw_distance_cm,distance_cm,ultrasonic_status,rtc_status,heap_bytes
```

| Field | Meaning |
|---|---|
| schema_version | Literal 2 |
| sample | One-based trigger-window sequence per boot; includes failures |
| timestamp | DS3231 local calendar fields, YYYY-MM-DD HH:MM:SS, Asia/Amman convention; blank on RTC read/OSF/calendar/progression failure |
| echo_us | Captured complete HIGH pulse duration; may be zero or a partial-sequence duration on invalid capture; status determines validity |
| raw_distance_cm | Uncorrected echo_us * 0.0343f / 2.0f, six decimals; blank for invalid echo |
| distance_cm | rawDistanceCm + frozen 2.274955 cm, rounded to float once; six decimals; blank for invalid echo |
| ultrasonic_status | OK, CAPTURE_INCOMPLETE, INITIAL_HIGH, TIMEOUT_NO_HIGH_OBSERVED, INCOMPLETE_HIGH_PULSE, INVALID_PULSE_SEQUENCE, OUT_OF_RANGE |
| rtc_status | OK or RTC_INVALID; independent of ultrasonic validity |
| heap_bytes | Free ESP32 heap at reporting time |

Six decimal places are serialization precision, not demonstrated physical accuracy. The correction is applied exactly once in convertEcho, only after raw echo/sequence/range validity checks. It is not added by logging or downstream consumers. No filtering, smoothing, clamping, tank dimensions or water-level conversion is implemented. The tested 30Ã¢â‚¬â€œ100 cm stationary flat-wall references are accuracy evidence, not new operational cutoffs. Actual water-surface and out-of-range accuracy remain unverified.

RTC is never adjusted by this firmware. Initialization/read failures do not suppress ultrasonic acquisition or SD initialization. RTC failures log an empty timestamp and RTC_INVALID, not invented time. Invalid ultrasonic attempts log empty distance fields and the original echo/capture status. No record is silently discarded by software; SD failures are explicitly reported and may prevent persistence of that record.

The header is written once into the newly created empty file and read back. Each row is opened with FILE_APPEND, written, flushed, closed, reopened and verified byte-for-byte at the recorded offset with an exact file-size check. A partial write or failed row verification disables further writes to that session file to avoid masking a corrupt tail. No ever-growing expected-row buffer is retained in production. Per-row verification does not prove crash-atomic writes or power-loss durability; those require separate testing.
