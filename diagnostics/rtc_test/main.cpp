#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <cstdio>
#include <cstring>

namespace {
RTC_DS3231 rtc;
constexpr uint8_t address = 0x68;
bool ready = false;
bool havePrevious = false;
uint32_t previousUnix = 0;
uint32_t lastSampleMs = 0;
uint8_t goodTicks = 0;
char command[64] = {};
size_t commandLength = 0;
bool commandOverflow = false;

bool readRegisters(uint8_t reg, uint8_t* data, size_t length) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    const uint8_t error = Wire.endTransmission(false);
    if (error != 0) {
        Serial.printf("RTC_I2C_READ_FAIL register=0x%02X error=%u\n", reg, error);
        return false;
    }
    const size_t received = Wire.requestFrom(address, static_cast<uint8_t>(length));
    if (received != length) {
        Serial.printf("RTC_I2C_READ_FAIL register=0x%02X received=%u expected=%u\n",
                      reg, static_cast<unsigned>(received), static_cast<unsigned>(length));
        while (Wire.available()) Wire.read();
        return false;
    }
    for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
    return true;
}

bool bcdValid(uint8_t value) {
    return (value & 0x0F) <= 9 && ((value >> 4) & 0x0F) <= 9;
}

bool clockFieldsReadable() {
    uint8_t data[7];
    if (!readRegisters(0x00, data, sizeof(data))) return false;
    const uint8_t hour = data[2] & ((data[2] & 0x40) ? 0x1F : 0x3F);
    if (!bcdValid(data[0] & 0x7F) || !bcdValid(data[1] & 0x7F) ||
        !bcdValid(hour) || !bcdValid(data[4] & 0x3F) ||
        !bcdValid(data[5] & 0x1F) || !bcdValid(data[6]) ||
        (data[5] & 0x80) || (data[3] & 7) == 0) {
        Serial.println("RTC_READ_FAIL: invalid/unsupported calendar register encoding (supported years 2000-2099)");
        return false;
    }
    return true;
}

bool oscillatorFlag(bool& osf) {
    uint8_t status;
    if (!readRegisters(0x0F, &status, 1)) return false;
    osf = rtc.lostPower();
    Serial.printf("RTC_LOST_POWER=%s OSF=%u\n", osf ? "YES" : "NO", osf ? 1 : 0);
    return true;
}

void instructions() {
    Serial.println("RTC_SET_HELP: ONLY on explicit approval send SET_AMMAN YYYY-MM-DD HH:MM:SS followed by newline");
    Serial.println("RTC_SET_HELP: supply current Asia/Amman local fields (UTC+3); no build-time or automatic setting");
}

void handleCommand() {
    // No call to adjust() is reachable without this exact explicit serial command.
    if (std::strcmp(command, "HELP") == 0) { instructions(); return; }
    if (!ready) { Serial.println("RTC_SET_REJECTED: RTC not initialized"); return; }
    if (std::strlen(command) != 29 || std::strncmp(command, "SET_AMMAN ", 10) != 0 ||
        command[14] != '-' || command[17] != '-' || command[20] != ' ' ||
        command[23] != ':' || command[26] != ':') {
        Serial.println("RTC_COMMAND_REJECTED: expected SET_AMMAN YYYY-MM-DD HH:MM:SS");
        return;
    }
    for (size_t i = 10; i < 29; ++i) {
        if (i == 14 || i == 17 || i == 20 || i == 23 || i == 26) continue;
        if (command[i] < '0' || command[i] > '9') {
            Serial.println("RTC_SET_REJECTED: nonnumeric date/time"); return;
        }
    }
    int y, m, d, h, minute, second;
    if (std::sscanf(command + 10, "%4d-%2d-%2d %2d:%2d:%2d", &y, &m, &d, &h, &minute, &second) != 6 ||
        y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31 ||
        h < 0 || h > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        Serial.println("RTC_SET_REJECTED: fields outside supported range"); return;
    }
    const DateTime requested(y, m, d, h, minute, second);
    if (!requested.isValid()) { Serial.println("RTC_SET_REJECTED: invalid calendar date"); return; }
    bool osf;
    if (!oscillatorFlag(osf)) { Serial.println("RTC_SET_REJECTED: status unreadable"); return; }
    rtc.adjust(requested);
    if (!clockFieldsReadable()) { Serial.println("RTC_SET_FAIL: readback unavailable"); return; }
    const DateTime actual = rtc.now();
    if (!actual.isValid() || actual.unixtime() < requested.unixtime() ||
        actual.unixtime() - requested.unixtime() > 1 || !oscillatorFlag(osf) || osf) {
        Serial.println("RTC_SET_FAIL: time/status verification failed"); return;
    }
    havePrevious = false;
    goodTicks = 0;
    Serial.println("RTC_SET_OK: explicit Asia/Amman fields written and read back; retention still PENDING");
}

void serviceCommands() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            if (commandOverflow) Serial.println("RTC_COMMAND_REJECTED: line too long");
            else if (commandLength) { command[commandLength] = '\0'; handleCommand(); }
            commandLength = 0;
            commandOverflow = false;
        } else if (commandLength < sizeof(command) - 1 && !commandOverflow) {
            command[commandLength++] = c;
        } else commandOverflow = true;
    }
}

void sampleClock() {
    if (!clockFieldsReadable()) { havePrevious = false; goodTicks = 0; return; }
    const DateTime now = rtc.now();
    if (!now.isValid()) {
        Serial.println("RTC_READ_FAIL: invalid calendar date/time");
        Serial.println("RTC_TIME_NOT_SET: explicit setting required");
        havePrevious = false; goodTicks = 0; return;
    }
    bool osf;
    if (!oscillatorFlag(osf)) { havePrevious = false; goodTicks = 0; return; }
    Serial.printf("%04u-%02u-%02u %02u:%02u:%02u\n", now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
    Serial.println("RTC_READ_OK");
    if (osf) Serial.println("RTC_TIME_NOT_SET: OSF set; displayed fields are not trusted; no automatic adjustment");
    if (havePrevious) {
        const uint32_t delta = now.unixtime() - previousUnix;
        if (delta >= 1 && delta <= 2) {
            if (goodTicks < 3) ++goodTicks;
            if (goodTicks == 3) Serial.println("RTC_TICK_OK: consecutive advancing readings");
        } else {
            Serial.printf("RTC_TICK_WARNING delta_seconds=%lu\n", static_cast<unsigned long>(delta));
            goodTicks = 0;
        }
    }
    previousUnix = now.unixtime(); havePrevious = true;
    Serial.println("RTC_RETENTION_PENDING | TIME_REFERENCE_UNVERIFIED | BATTERY_HEALTH_UNVERIFIED");
}
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("Khazzan rtc_test | SDA=21 SCL=22 I2C=0x68 | no automatic time setting");
    if (!Wire.begin(21, 22)) { Serial.println("RTC_I2C_FAIL: Wire.begin failed"); return; }
    Wire.setClock(100000);
    Wire.setTimeOut(100);
    Wire.beginTransmission(address);
    const uint8_t result = Wire.endTransmission();
    if (result != 0) { Serial.printf("RTC_I2C_FAIL address=0x68 error=%u\n", result); return; }
    Serial.println("RTC_I2C_OK address=0x68");
    if (!rtc.begin(&Wire)) { Serial.println("RTC_INIT_FAIL: RTClib begin returned false"); return; }
    Serial.println("RTC_INIT_OK");
    ready = true;
    bool osf;
    if (!oscillatorFlag(osf)) { ready = false; return; }
    if (osf) Serial.println("RTC_TIME_NOT_SET: oscillator-stop flag set; explicit setting required");
    instructions();
    sampleClock();
    lastSampleMs = millis();
}

void loop() {
    serviceCommands();
    if (ready && static_cast<uint32_t>(millis() - lastSampleMs) >= 1000) {
        lastSampleMs = millis(); sampleClock();
    }
    delay(1);
}
