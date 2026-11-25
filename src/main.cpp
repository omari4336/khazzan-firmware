#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>
#include <cstring>
#include <cmath>
#include <esp_system.h>
#include "UltrasonicSensor.h"
#include "config.h"

namespace {
UltrasonicSensor sensor;
bool pending = false;
uint32_t startedMs = 0, sequence = 0;
RTC_DS3231 rtc;
bool rtcReady = false;
bool readRtcBytes(uint8_t reg, uint8_t* buffer, uint8_t length) {
    Wire.beginTransmission(0x68); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<uint8_t>(0x68), length) != length) {
        while (Wire.available()) Wire.read();
        return false;
    }
    for (uint8_t i = 0; i < length; ++i) buffer[i] = Wire.read();
    return true;
}
uint8_t fromBcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
bool validBcd(uint8_t b) { return (b & 15) <= 9 && (b >> 4) <= 9; }
bool timestampNow(char* result, size_t size) {
    if (!rtcReady) { Serial.println("RTC_TIME_FAIL: not initialized"); return false; }
    uint8_t status, data[7];
    if (!readRtcBytes(0x0F, &status, 1) || !readRtcBytes(0, data, 7)) {
        Serial.println("RTC_TIME_FAIL: I2C read failed"); return false;
    }
    if ((status & 0x80) || rtc.lostPower()) {
        Serial.println("RTC_TIME_FAIL: OSF set; NO time adjustment"); return false;
    }
    const uint8_t hourField = data[2] & ((data[2] & 0x40) ? 0x1F : 0x3F);
    if (!validBcd(data[0] & 0x7F) || !validBcd(data[1] & 0x7F) || !validBcd(hourField) ||
        !validBcd(data[4] & 0x3F) || !validBcd(data[5] & 0x1F) || !validBcd(data[6]) || (data[5] & 0x80)) {
        Serial.println("RTC_TIME_FAIL: calendar encoding unsupported/invalid"); return false;
    }
    uint8_t hour = fromBcd(hourField);
    if (data[2] & 0x40) {
        if (hour < 1 || hour > 12) return false;
        hour = hour % 12 + ((data[2] & 0x20) ? 12 : 0);
    }
    const DateTime now(2000 + fromBcd(data[6]), fromBcd(data[5] & 0x1F),
                       fromBcd(data[4] & 0x3F), hour, fromBcd(data[1] & 0x7F), fromBcd(data[0] & 0x7F));
    if (!now.isValid()) { Serial.println("RTC_TIME_FAIL: invalid date/time"); return false; }
    const int written = snprintf(result, size, "%04u-%02u-%02u %02u:%02u:%02u", now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return written == 19 && static_cast<size_t>(written) < size;
}



// Schema v2 gets its own unique file; never append new fields to historical CSVs.
constexpr char header[] = "schema_version,sample,timestamp,echo_us,raw_distance_cm,distance_cm,ultrasonic_status,rtc_status,heap_bytes\n";
char csvPath[48] = {};
bool sdReady = false, csvReady = false;
uint32_t rowWritten = 0, rowVerified = 0, logErrors = 0;
uint32_t previousRtc = 0, previousRtcMs = 0, rtcValidCount = 0;
bool verifyBytes(File& file, const String& text) {
    for (size_t i = 0; i < text.length(); ++i) {
        if (file.read() != static_cast<uint8_t>(text[i])) return false;
    }
    return true;
}
const char* cardName(uint8_t type) {
    switch (type) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC";
    case CARD_NONE: return "NONE";
    default: return "UNKNOWN";
    }
}
bool createUniqueCsv() {
    const uint32_t nonce = esp_random();
    for (uint32_t i = 0; i < 1000; ++i) {
        snprintf(csvPath, sizeof(csvPath), "/khz_v2_%08lx_%03lu.csv", static_cast<unsigned long>(nonce), static_cast<unsigned long>(i));
        if (SD.exists(csvPath)) continue;
        File f = SD.open(csvPath, FILE_APPEND);
        if (!f || f.isDirectory() || f.size() != 0) {
            f.close(); Serial.println("CSV_CREATE_FAIL: unavailable/nonempty path; no overwrite"); return false;
        }
        const size_t written = f.print(header); f.flush(); const int error = f.getWriteError(); f.close();
        if (written != strlen(header) || error) { Serial.println("CSV_HEADER_WRITE_FAIL"); return false; }
        File check = SD.open(csvPath, FILE_READ);
        const bool ok = check && check.size() == strlen(header) && verifyBytes(check, String(header));
        check.close();
        Serial.printf("CSV_FILE=%s HEADER_VERIFY=%s\n", csvPath, ok ? "PASS" : "FAIL");
        return ok;
    }
    Serial.println("CSV_CREATE_FAIL: unique filename attempts exhausted");
    return false;
}
bool logRow(const String& row) {
    if (!csvReady) { ++logErrors; Serial.println("CSV_LOG_FAIL: file unavailable"); return false; }
    File f = SD.open(csvPath, FILE_APPEND);
    if (!f || f.isDirectory()) { f.close(); ++logErrors; Serial.println("CSV_APPEND_OPEN_FAIL"); return false; }
    const size_t offset = f.size();
    const size_t written = f.print(row); f.flush(); const int error = f.getWriteError(); f.close();
    if (written != row.length() || error) {
        csvReady = false; ++logErrors; Serial.println("CSV_WRITE_FAIL: partial tail may remain; no subsequent writes"); return false;
    }
    ++rowWritten;
    File readback = SD.open(csvPath, FILE_READ);
    const bool ok = readback && readback.size() == offset + row.length() && readback.seek(offset) && verifyBytes(readback, row);
    readback.close();
    if (ok) ++rowVerified;
    else { csvReady = false; ++logErrors; }
    Serial.printf("CSV_WRITE_OK written_records=%lu ROW_READBACK=%s verified_records=%lu\n",
                  static_cast<unsigned long>(rowWritten), ok ? "PASS" : "FAIL", static_cast<unsigned long>(rowVerified));
    return ok;
}

void reportMeasurement(const Measurement& reading) {
    char rawText[24] = {}, correctedText[24] = {};
    if (reading.valid()) {
        snprintf(rawText, sizeof(rawText), "%.6f", reading.rawDistanceCm);
        snprintf(correctedText, sizeof(correctedText), "%.6f", reading.distanceCm);
    }
    char stamp[32] = {};
    bool timeOk = timestampNow(stamp, sizeof(stamp));
    const uint32_t nowMs = millis();
    if (timeOk) {
        int y,m,d,h,mi,s;
        sscanf(stamp, "%d-%d-%d %d:%d:%d", &y,&m,&d,&h,&mi,&s);
        const DateTime rtcNow(y,m,d,h,mi,s);
        const uint32_t current = rtcNow.unixtime();
        if (rtcValidCount) {
            const int64_t rtcStep = static_cast<int64_t>(current) - previousRtc;
            const float elapsed = static_cast<uint32_t>(nowMs - previousRtcMs) / 1000.0f;
            if (rtcStep < 1 || std::fabs(static_cast<float>(rtcStep) - elapsed) > 1.1f) {
                timeOk = false;
                Serial.printf("RTC_TICK_FAIL step=%lld host_interval_s=%.3f\n", static_cast<long long>(rtcStep), elapsed);
            }
        }
        previousRtc = current; previousRtcMs = nowMs; ++rtcValidCount;
    }
    if (timeOk) Serial.printf("RTC_READ_OK %s OSF=0\n", stamp);
    else { stamp[0] = '\0'; Serial.println("RTC_READ_FAIL: timestamp blank in CSV"); }
    const char* status = measurementStatusName(reading.status);
    const uint32_t heap = ESP.getFreeHeap();
    Serial.printf("READING sample=%lu echo_us=%lu raw_distance_cm=%s distance_cm=%s status=%s rtc=%s heap=%lu\n",
                  static_cast<unsigned long>(sequence), static_cast<unsigned long>(reading.durationUs),
                  rawText, correctedText, status, timeOk ? stamp : "INVALID", static_cast<unsigned long>(heap));
    const String row = String("2,") + String(sequence) + "," + stamp + "," + String(reading.durationUs) + "," +
        rawText + "," + correctedText + "," + status + "," + (timeOk ? "OK" : "RTC_INVALID") + "," + String(heap) + "\n";
    logRow(row);
}
}
void setup() {
    Serial.begin(config::serialBaud);
    sensor.begin();
    delay(config::startupDelayMs);
    Serial.printf("KHAZZAN_BOOT reset_reason=%d firmware=V1.0 csv_schema=2\n", static_cast<int>(esp_reset_reason()));
    Serial.println("TRIG=5 ECHO=18 trigger_us=25 window_us=70000 interval_ms=2000");
    Serial.printf("CALIBRATION_OFFSET_CM=%.6f tested_flat_wall_reference_cm=30-100 water_surface=NOT_VALIDATED\n", calibration::ultrasonicOffsetCm);
    const bool wireOk = Wire.begin(21,22); Wire.setClock(100000); Wire.setTimeOut(100);
    bool found = false;
    if (wireOk) {
        Wire.beginTransmission(0x68); found = Wire.endTransmission() == 0;
    }
    rtcReady = wireOk && found && rtc.begin(&Wire);
    Serial.println(rtcReady ? "RTC_INIT_OK address=0x68" : "RTC_INIT_FAIL");
    uint8_t status;
    if (rtcReady && readRtcBytes(0x0F,&status,1)) Serial.printf("RTC_OSF=%u NO_TIME_ADJUSTMENT\n",(status>>7)&1);
    SPI.begin(14,19,23,27);
    sdReady = SD.begin(27,SPI,1000000,"/sd",5,false);
    if (sdReady) sdReady = SD.cardType()!=CARD_NONE && SD.cardSize()>0;
    if (sdReady) {
        Serial.printf("SD_INIT_OK type=%s capacity_bytes=%llu spi_hz=1000000\n",cardName(SD.cardType()),static_cast<unsigned long long>(SD.cardSize()));
        csvReady = createUniqueCsv();
    } else Serial.println("SD_INIT_FAIL: no formatting performed");
    startedMs = millis() - config::measurementIntervalMs;
}
void loop() {
    if (pending) {
        if (sensor.ready()) { pending = false; reportMeasurement(sensor.finish()); }
    } else if (static_cast<uint32_t>(millis()-startedMs) >= config::measurementIntervalMs) {
        const uint32_t now = millis();
        if (sequence && static_cast<uint32_t>(now-startedMs) > config::measurementIntervalMs+100)
            Serial.println("SCHEDULE_WARNING: measurement delayed over 100ms");
        startedMs = now; ++sequence; sensor.start(); pending = true;
    }
    delay(1);
}
