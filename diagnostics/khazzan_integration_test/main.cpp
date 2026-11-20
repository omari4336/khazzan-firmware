#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>
#include <cstring>
#include <driver/gpio.h>

namespace {
constexpr uint8_t trigPin = 5;
constexpr uint8_t echoPin = 18;
constexpr uint32_t windowUs = 70000;
constexpr uint32_t intervalMs = 2000;
constexpr uint16_t capacity = 128;

struct Edge { uint32_t offsetUs; uint8_t level; };
volatile Edge edges[capacity];
Edge snapshot[capacity];
portMUX_TYPE captureMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool capturing = false;
volatile uint32_t originUs = 0;
volatile uint32_t transitionCount = 0;
volatile uint16_t storedCount = 0;
bool windowPending = false;
uint8_t initialLevel = LOW;
uint32_t startedMs = 0;
uint32_t triggerLowOffsetUs = 0;
uint32_t sequence = 0;

void IRAM_ATTR onEchoChange() {
    portENTER_CRITICAL_ISR(&captureMux);
    if (capturing) {
        const uint32_t offset = static_cast<uint32_t>(micros() - originUs);
        if (offset <= windowUs) {
            const uint8_t level = static_cast<uint8_t>(gpio_get_level(GPIO_NUM_18));
            ++transitionCount;
            if (storedCount < capacity) {
                edges[storedCount].offsetUs = offset;
                edges[storedCount].level = level;
                ++storedCount;
            }
        }
    }
    portEXIT_CRITICAL_ISR(&captureMux);
}

void startWindow() {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(5);
    portENTER_CRITICAL(&captureMux);
    initialLevel = static_cast<uint8_t>(gpio_get_level(GPIO_NUM_18));
    storedCount = 0;
    transitionCount = 0;
    originUs = micros();
    capturing = true;
    portEXIT_CRITICAL(&captureMux);
    // Origin is just before the requested trigger, not a measured pin edge.
    digitalWrite(trigPin, HIGH);
    // Y1-derived timing hypothesis; exact Y3.0 compatibility is unverified.
    delayMicroseconds(25);
    digitalWrite(trigPin, LOW);
    triggerLowOffsetUs = static_cast<uint32_t>(micros() - originUs);
    windowPending = true;
    ++sequence;
}


RTC_DS3231 rtc;
bool rtcReady = false, sdReady = false, csvReady = false;
bool allGood = true, finished = false;
uint32_t runStartMs = 0;
uint32_t sessionOffset = 0;
uint32_t rowsLogged = 0;
uint32_t validMeasurements = 0;
String sessionRows[16];
constexpr char csvPath[] = "/khazzan_test.csv";
constexpr char header[] = "timestamp,distance_cm,status\n";

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

bool verifyBytes(File& f, const String& expected) {
    for (size_t i = 0; i < expected.length(); ++i) {
        if (f.read() != static_cast<uint8_t>(expected[i])) return false;
    }
    return true;
}

bool prepareCsv() {
    File input = SD.open(csvPath, FILE_READ);
    const bool exists = SD.exists(csvPath);
    size_t size = 0;
    if (exists) {
        if (!input || input.isDirectory()) { Serial.println("CSV_INIT_FAIL: existing file unreadable"); return false; }
        size = input.size();
        if (size > 0) {
            if (!verifyBytes(input, String(header)) || !input.seek(size - 1) || input.read() != '\n') {
                input.close();
                Serial.println("CSV_INIT_FAIL: existing header or final newline invalid; file unchanged"); return false;
            }
        }
    }
    input.close();
    if (size == 0) {
        File output = SD.open(csvPath, FILE_APPEND);
        if (!output || output.isDirectory()) { Serial.println("CSV_HEADER_FAIL: append open"); return false; }
        const size_t n = output.print(header); output.flush();
        const int error = output.getWriteError(); output.close();
        if (n != strlen(header) || error) { Serial.println("CSV_HEADER_FAIL: short write/error"); return false; }
        Serial.println("CSV_HEADER_WRITTEN");
    } else Serial.println("CSV_HEADER_EXISTS: preserving previous rows");
    File verify = SD.open(csvPath, FILE_READ);
    if (!verify || !verifyBytes(verify, String(header))) { verify.close(); Serial.println("CSV_HEADER_VERIFY_FAIL"); return false; }
    sessionOffset = verify.size(); verify.close();
    Serial.printf("CSV_READY append_offset=%lu\n", static_cast<unsigned long>(sessionOffset));
    return true;
}

bool appendAndVerify(const String& row) {
    if (!sdReady || !csvReady) { Serial.println("CSV_LOG_FAIL: SD/CSV unavailable"); return false; }
    if (rowsLogged >= 16) { Serial.println("CSV_LOG_FAIL: session verification buffer full"); return false; }
    File output = SD.open(csvPath, FILE_APPEND);
    if (!output || output.isDirectory()) { Serial.println("CSV_LOG_FAIL: append open"); return false; }
    const size_t offset = output.size();
    const size_t n = output.print(row); output.flush();
    const int error = output.getWriteError(); output.close();
    if (n != row.length() || error) { Serial.println("CSV_LOG_FAIL: short write/error; partial tail may remain"); csvReady = false; return false; }
    File input = SD.open(csvPath, FILE_READ);
    const bool ok = input && input.size() == offset + row.length() && input.seek(offset) && verifyBytes(input, row);
    input.close();
    if (!ok) { Serial.println("CSV_ROW_VERIFY_FAIL"); csvReady = false; return false; }
    sessionRows[rowsLogged++] = row;
    Serial.printf("CSV_LOG_OK row=%lu CSV_ROW_VERIFY_OK\n", static_cast<unsigned long>(rowsLogged));
    return true;
}

void finishRun() {
    finished = true;
    File input;
    bool verified = sdReady && csvReady && rowsLogged > 0;
    if (verified) {
        input = SD.open(csvPath, FILE_READ);
        verified = input && verifyBytes(input, String(header)) && input.seek(sessionOffset);
    }
    Serial.println("CSV_READBACK_BEGIN");
    if (verified) {
        Serial.print(header);
        for (uint32_t i = 0; i < rowsLogged; ++i) {
            if (!verifyBytes(input, sessionRows[i])) { verified = false; break; }
            Serial.print(sessionRows[i]);
        }
        if (input.available()) verified = false;
    }
    input.close();
    Serial.println("CSV_READBACK_END");
    Serial.printf("CSV_READBACK_%s verified_session_rows=%lu\n", verified ? "OK" : "FAIL", static_cast<unsigned long>(rowsLogged));
    Serial.printf("RUN_SUMMARY measurements=%lu valid_ultrasonic=%lu logged_rows=%lu\n",
                  static_cast<unsigned long>(sequence), static_cast<unsigned long>(validMeasurements), static_cast<unsigned long>(rowsLogged));
    const bool pass = allGood && rtcReady && sdReady && verified && sequence > 0 &&
                      validMeasurements == sequence && rowsLogged == sequence;
    Serial.println(pass ? "INTEGRATION_PASS: provisional distance only; calibration PENDING" : "INTEGRATION_NOT_PASSED: inspect errors/statuses above");
    Serial.println("RUN_COMPLETE: no further triggers or CSV writes until reset");
}

void reportWindow() {
    portENTER_CRITICAL(&captureMux);
    capturing = false;
    const uint16_t count = storedCount;
    const uint32_t total = transitionCount;
    for (uint16_t i = 0; i < count; ++i) snapshot[i] = {edges[i].offsetUs, edges[i].level};
    portEXIT_CRITICAL(&captureMux);
    windowPending = false;
    bool repeated = false, pending = initialLevel == HIGH, hasRise = false;
    uint8_t previous = initialLevel;
    uint32_t rise = 0, pulse = 0, complete = 0;
    Serial.printf("sample=%lu initial_level=%u transitions=%lu stored=%u overflow=%s trigger_low_command_offset_us=%lu\n",
                  static_cast<unsigned long>(sequence), initialLevel, static_cast<unsigned long>(total),
                  static_cast<unsigned>(count), total > count ? "YES" : "NO", static_cast<unsigned long>(triggerLowOffsetUs));
    for (uint16_t i = 0; i < count; ++i) {
        const Edge edge = snapshot[i];
        Serial.printf("  edge=%u t_us=%lu level=%u\n", static_cast<unsigned>(i + 1), static_cast<unsigned long>(edge.offsetUs), edge.level);
        if (edge.level == previous) repeated = true;
        previous = edge.level;
        if (edge.level == HIGH) { pending = true; hasRise = true; rise = edge.offsetUs; }
        else if (pending) {
            if (hasRise) { pulse = edge.offsetUs - rise; ++complete; Serial.printf("  high_pulse_us=%lu\n", static_cast<unsigned long>(pulse)); }
            pending = false; hasRise = false;
        }
    }
    const char* status = "OK_PROVISIONAL";
    if (total > count || repeated) status = "CAPTURE_INCOMPLETE";
    else if (initialLevel == HIGH) status = "INITIAL_HIGH";
    else if (count == 0) status = "TIMEOUT_NO_HIGH_OBSERVED";
    else if (pending) status = "INCOMPLETE_HIGH_PULSE";
    else if (complete != 1 || count != 2 || pulse == 0) status = "INVALID_PULSE_SEQUENCE";
    const float distance = static_cast<float>(pulse) * 0.0343f / 2.0f;
    if (strcmp(status, "OK_PROVISIONAL") == 0 && !(distance >= 2.0f && distance <= 600.0f)) status = "OUT_OF_RANGE";
    const bool valid = strcmp(status, "OK_PROVISIONAL") == 0;
    char distanceText[24] = {};
    if (valid) { snprintf(distanceText, sizeof(distanceText), "%.3f", distance); ++validMeasurements; }
    else allGood = false;
    char timestamp[32] = {};
    if (!timestampNow(timestamp, sizeof(timestamp))) {
        Serial.printf("MEASUREMENT timestamp=INVALID distance_cm=%s status=%s CSV_LOG_SKIPPED_RTC_INVALID\n", distanceText, status);
        allGood = false; return;
    }
    const String row = String(timestamp) + "," + distanceText + "," + status + "\n";
    Serial.print("MEASUREMENT "); Serial.print(row);
    if (!appendAndVerify(row)) allGood = false;
}
}

void setup() {
    Serial.begin(115200);
    digitalWrite(trigPin, LOW); pinMode(trigPin, OUTPUT); pinMode(echoPin, INPUT);
    attachInterrupt(digitalPinToInterrupt(echoPin), onEchoChange, CHANGE);
    delay(1500);
    Serial.println("Khazzan khazzan_integration_test | 25us TRIG / 70ms window / 2s | CALIBRATION_PENDING | RTC Asia/Amman fields, never adjusted");
    const bool wireOk = Wire.begin(21, 22);
    Wire.setClock(100000); Wire.setTimeOut(100);
    if (wireOk) {
        Wire.beginTransmission(0x68);
        rtcReady = Wire.endTransmission() == 0 && rtc.begin(&Wire);
    }
    Serial.println(rtcReady ? "RTC_INIT_OK address=0x68" : "RTC_INIT_FAIL");
    char timestamp[32];
    const bool timeValid = timestampNow(timestamp, sizeof(timestamp));
    if (timeValid) Serial.printf("RTC_TIME_OK %s OSF=0\n", timestamp);
    else allGood = false;
    SPI.begin(14, 19, 23, 27);
    sdReady = SD.begin(27, SPI, 1000000, "/sd", 5, false);
    if (sdReady) sdReady = SD.cardType() != CARD_NONE && SD.cardSize() > 0;
    Serial.println(sdReady ? "SD_INIT_OK spi_hz=1000000 SCK=14 MISO=19 MOSI=23 CS=27" : "SD_INIT_FAIL: no formatting attempted");
    if (sdReady) {
        Serial.printf("SD_CAPACITY_BYTES=%llu\n", static_cast<unsigned long long>(SD.cardSize()));
        csvReady = prepareCsv();
    }
    if (!sdReady || !csvReady || !rtcReady) allGood = false;
    runStartMs = millis(); startedMs = runStartMs - intervalMs;
}

void loop() {
    if (finished) { delay(100); return; }
    if (windowPending) {
        if (static_cast<uint32_t>(micros() - originUs) >= windowUs) reportWindow();
    } else if (static_cast<uint32_t>(millis() - runStartMs) >= 30000) finishRun();
    else if (static_cast<uint32_t>(millis() - startedMs) >= intervalMs) {
        startedMs = millis(); startWindow();
    }
    delay(1);
}
