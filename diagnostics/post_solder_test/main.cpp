#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <esp_system.h>
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


constexpr uint32_t targetSamples = 30;
constexpr char header[] = "sample,timestamp,echo_us,distance_cm,ultrasonic_status,rtc_status,heap_bytes\n";
char csvPath[48] = {};
bool sdReady = false, csvReady = false, finished = false;
bool rtcAllValid = true, rtcProgression = true, espStable = true;
uint32_t previousRtc = 0, previousRtcMs = 0, rtcValidCount = 0;
uint32_t validCount = 0, completeEchoCount = 0, rowWritten = 0, rowVerified = 0;
uint32_t timeouts = 0, outOfRange = 0, captureErrors = 0;
uint32_t logErrors = 0, rtcErrors = 0, scheduleWarnings = 0;
uint32_t minHeap = UINT32_MAX;
String expectedRows[targetSamples];
float distances[targetSamples] = {};

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
        snprintf(csvPath, sizeof(csvPath), "/post_%08lx_%03lu.csv", static_cast<unsigned long>(nonce), static_cast<unsigned long>(i));
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
    expectedRows[rowWritten++] = row;
    File readback = SD.open(csvPath, FILE_READ);
    const bool ok = readback && readback.size() == offset + row.length() && readback.seek(offset) && verifyBytes(readback, row);
    readback.close();
    if (ok) ++rowVerified;
    else { csvReady = false; ++logErrors; }
    Serial.printf("CSV_WRITE_OK written_records=%lu ROW_READBACK=%s verified_records=%lu\n",
                  static_cast<unsigned long>(rowWritten), ok ? "PASS" : "FAIL", static_cast<unsigned long>(rowVerified));
    return ok;
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
    Serial.printf("SAMPLE=%lu uptime_ms=%lu initial_level=%u transitions=%lu stored=%u overflow=%s trigger_low_command_offset_us=%lu\n",
                  static_cast<unsigned long>(sequence), static_cast<unsigned long>(millis()), initialLevel,
                  static_cast<unsigned long>(total), static_cast<unsigned>(count), total > count ? "YES" : "NO",
                  static_cast<unsigned long>(triggerLowOffsetUs));
    for (uint16_t i = 0; i < count; ++i) {
        const Edge edge = snapshot[i];
        Serial.printf("EDGE=%u t_us=%lu level=%u\n", static_cast<unsigned>(i + 1), static_cast<unsigned long>(edge.offsetUs), edge.level);
        if (edge.level == previous) repeated = true;
        previous = edge.level;
        if (edge.level == HIGH) { pending = true; hasRise = true; rise = edge.offsetUs; }
        else if (pending) {
            if (hasRise) { pulse = edge.offsetUs - rise; ++complete; }
            pending = false; hasRise = false;
        }
    }
    const char* status = "OK_PROVISIONAL";
    if (total > count || repeated) status = "CAPTURE_INCOMPLETE";
    else if (initialLevel == HIGH) status = "INITIAL_HIGH";
    else if (!count) status = "TIMEOUT_NO_HIGH_OBSERVED";
    else if (pending) status = "INCOMPLETE_HIGH_PULSE";
    else if (complete != 1 || count != 2 || !pulse) status = "INVALID_PULSE_SEQUENCE";
    const bool cleanEcho = strcmp(status, "OK_PROVISIONAL") == 0;
    if (cleanEcho) ++completeEchoCount;
    const float distance = pulse * 0.0343f / 2.0f;
    if (cleanEcho && !(distance >= 2.0f && distance <= 600.0f)) status = "OUT_OF_RANGE";
    const bool valid = strcmp(status, "OK_PROVISIONAL") == 0;
    if (valid) distances[validCount++] = distance;
    else if (!count && initialLevel == LOW) ++timeouts;
    else if (strcmp(status, "OUT_OF_RANGE") == 0) ++outOfRange;
    else ++captureErrors;
    char distanceText[24] = {};
    if (valid) snprintf(distanceText, sizeof(distanceText), "%.3f", distance);
    char stamp[32] = {};
    const bool timeOk = timestampNow(stamp, sizeof(stamp));
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
                rtcProgression = false; Serial.printf("RTC_TICK_FAIL step=%lld host_interval_s=%.3f\n", static_cast<long long>(rtcStep), elapsed);
            }
        }
        previousRtc=current; previousRtcMs=nowMs; ++rtcValidCount;
        Serial.printf("RTC_READ_OK %s OSF=0\n",stamp);
    } else { ++rtcErrors; rtcAllValid=false; Serial.println("RTC_READ_FAIL: timestamp blank in CSV"); }
    const uint32_t heap = ESP.getFreeHeap(); minHeap = std::min(minHeap, heap);
    if (heap < 10000) espStable=false;
    Serial.printf("READING sample=%lu echo_us=%lu distance_cm=%s status=%s rtc=%s heap=%lu\n",
                  static_cast<unsigned long>(sequence), static_cast<unsigned long>(pulse), distanceText, status,
                  timeOk ? stamp : "INVALID", static_cast<unsigned long>(heap));
    const String row = String(sequence)+","+stamp+","+String(pulse)+","+distanceText+","+status+","+(timeOk?"OK":"RTC_INVALID")+","+String(heap)+"\n";
    logRow(row);
}
void finishRun() {
    finished=true;
    uint32_t finalRows=0;
    File file;
    bool verified=csvReady && rowWritten>0;
    if (verified) {
        file=SD.open(csvPath,FILE_READ);
        verified=file && verifyBytes(file,String(header));
    }
    Serial.println("CSV_READBACK_BEGIN");
    if (verified) {
        Serial.print(header);
        for (uint32_t i=0;i<rowWritten;++i) {
            if (!verifyBytes(file,expectedRows[i])) { verified=false;break; }
            ++finalRows;Serial.print(expectedRows[i]);
        }
        if (file.available()) verified=false;
    }
    file.close();Serial.println("CSV_READBACK_END");
    float sum=0,mean=0,median=0,min=0,max=0,sd=0;
    if (validCount) {
        std::sort(distances,distances+validCount);
        for(uint32_t i=0;i<validCount;++i)sum+=distances[i];
        mean=sum/validCount;min=distances[0];max=distances[validCount-1];
        median=validCount%2?distances[validCount/2]:(distances[validCount/2-1]+distances[validCount/2])/2;
        for(uint32_t i=0;i<validCount;++i)sd+=(distances[i]-mean)*(distances[i]-mean);
        sd=std::sqrt(sd/validCount);
        Serial.printf("STATS valid=%lu total=%lu valid_pct=%.2f min_cm=%.6f max_cm=%.6f mean_cm=%.6f median_cm=%.6f population_stddev_cm=%.6f\n",
                      static_cast<unsigned long>(validCount),static_cast<unsigned long>(sequence),100.0f*validCount/sequence,min,max,mean,median,sd);
    } else Serial.println("STATS valid=0 total=30 valid_pct=0 min_cm=NA max_cm=NA mean_cm=NA median_cm=NA population_stddev_cm=NA");
    const bool rtcPass=rtcReady&&rtcAllValid&&rtcProgression&&rtcValidCount==targetSamples;
    const bool sdPass=verified&&rowWritten==targetSamples&&rowVerified==targetSamples&&finalRows==targetSamples;
    const bool integrated=rtcPass&&sdPass;
    const bool comm=completeEchoCount==targetSamples;
    const char* stability=validCount<targetSamples/2?"FAIL":
        (validCount<targetSamples || (mean>0 && sd/mean>0.05f) || (median>0&&(max-min)/median>0.20f)?"WARNING":"PASS");
    const bool fail=!espStable||!rtcPass||!sdReady||!sdPass||!integrated||!comm||strcmp(stability,"FAIL")==0;
    const char* overall=fail?"FAIL":(strcmp(stability,"WARNING")==0||scheduleWarnings?"WARNING":"PASS");
    Serial.println("DIAGNOSTIC_SUMMARY_BEGIN");
    Serial.printf("ESP32=%s min_free_heap=%lu historical_reset_reason=%d\n",espStable?"PASS":"FAIL",static_cast<unsigned long>(minHeap),static_cast<int>(esp_reset_reason()));
    Serial.printf("RTC_DETECTION=%s\nRTC_TIME_PROGRESSION=%s valid_timestamps=%lu rtc_errors=%lu\n",rtcReady?"PASS":"FAIL",rtcPass?"PASS":"FAIL",static_cast<unsigned long>(rtcValidCount),static_cast<unsigned long>(rtcErrors));
    Serial.printf("ULTRASONIC_COMMUNICATION=%s clean_echoes=%lu\nULTRASONIC_STABILITY=%s\n",comm?"PASS":"FAIL",static_cast<unsigned long>(completeEchoCount),stability);
    Serial.printf("MICROSD_INIT=%s\nMICROSD_WRITE_READ_VERIFY=%s written=%lu row_verified=%lu final_readback=%lu log_errors=%lu\n",sdReady?"PASS":"FAIL",sdPass?"PASS":"FAIL",static_cast<unsigned long>(rowWritten),static_cast<unsigned long>(rowVerified),static_cast<unsigned long>(finalRows),static_cast<unsigned long>(logErrors));
    Serial.printf("INTEGRATED_DATA_LOGGING=%s\nUNEXPECTED_RESETS=0 scope=current_uninterrupted_run_confirm_with_host_log\n",integrated?"PASS":"FAIL");
    Serial.printf("VALID_DISTANCE_READINGS=%lu/30 percentage=%.2f timeouts=%lu out_of_range=%lu capture_errors=%lu schedule_warnings=%lu\n",static_cast<unsigned long>(validCount),100.0f*validCount/targetSamples,static_cast<unsigned long>(timeouts),static_cast<unsigned long>(outOfRange),static_cast<unsigned long>(captureErrors),static_cast<unsigned long>(scheduleWarnings));
    Serial.printf("OVERALL=%s\n",overall);
    Serial.println("PHYSICAL_ACCURACY=NOT_TESTED SOLDERING_SAFETY=NOT_TESTED SUPPLY_VOLTAGE=NOT_TESTED RTC_BATTERY_RETENTION=NOT_TESTED");
    Serial.println("DIAGNOSTIC_SUMMARY_END");
    Serial.println("RUN_COMPLETE: no further triggers or writes until reset");
}
}
void setup() {
    Serial.begin(115200);
    digitalWrite(trigPin,LOW);pinMode(trigPin,OUTPUT);pinMode(echoPin,INPUT);
    attachInterrupt(digitalPinToInterrupt(echoPin),onEchoChange,CHANGE);
    delay(1500);
    Serial.printf("POST_SOLDER_BOOT reset_reason=%d chip=%s revision=%u cores=%u cpu_MHz=%u flash_bytes=%lu heap=%lu min_heap=%lu\n",static_cast<int>(esp_reset_reason()),ESP.getChipModel(),ESP.getChipRevision(),ESP.getChipCores(),ESP.getCpuFreqMHz(),static_cast<unsigned long>(ESP.getFlashChipSize()),static_cast<unsigned long>(ESP.getFreeHeap()),static_cast<unsigned long>(ESP.getMinFreeHeap()));
    Serial.println("POST_SOLDER_CONFIG TRIG=5 ECHO=18 trigger_us=25 window_us=70000 interval_ms=2000 samples=30 CALIBRATION_PENDING");
    const bool wireOk=Wire.begin(21,22);Wire.setClock(100000);Wire.setTimeOut(100);
    bool found=false;
    if (wireOk) {
        for(uint8_t a=1;a<127;++a) {
            Wire.beginTransmission(a);const uint8_t result=Wire.endTransmission();
            if(result==0){Serial.printf("I2C_FOUND=0x%02X\n",a);if(a==0x68)found=true;}
            else if(result!=2)Serial.printf("I2C_SCAN_ERROR address=0x%02X error=%u\n",a,result);
        }
    }
    rtcReady=wireOk&&found&&rtc.begin(&Wire);
    Serial.println(rtcReady?"RTC_INIT_OK address=0x68":"RTC_INIT_FAIL");
    uint8_t status;
    if(rtcReady&&readRtcBytes(0x0F,&status,1))Serial.printf("RTC_OSF=%u NO_TIME_ADJUSTMENT\n",(status>>7)&1);
    SPI.begin(14,19,23,27);
    sdReady=SD.begin(27,SPI,1000000,"/sd",5,false);
    if(sdReady)sdReady=SD.cardType()!=CARD_NONE&&SD.cardSize()>0;
    if(sdReady){Serial.printf("SD_INIT_OK type=%s capacity_bytes=%llu spi_hz=1000000\n",cardName(SD.cardType()),static_cast<unsigned long long>(SD.cardSize()));csvReady=createUniqueCsv();}
    else Serial.println("SD_INIT_FAIL: no formatting performed");
    startedMs=millis()-intervalMs;
}
void loop() {
    if(finished){delay(100);return;}
    if(windowPending){if(static_cast<uint32_t>(micros()-originUs)>=windowUs)reportWindow();}
    else if(sequence>=targetSamples)finishRun();
    else if(static_cast<uint32_t>(millis()-startedMs)>=intervalMs){
        const uint32_t now=millis();
        if(sequence && static_cast<uint32_t>(now-startedMs)>intervalMs+100){++scheduleWarnings;Serial.println("SCHEDULE_WARNING: measurement delayed over 100ms");}
        startedMs=now;startWindow();
    }
    delay(1);
}
