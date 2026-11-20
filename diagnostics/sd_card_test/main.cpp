#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <cstring>

namespace {
constexpr char path[] = "/test.txt";
constexpr char expected[] = "Khazzan SD Test OK\n";
constexpr size_t expectedBytes = sizeof(expected) - 1;
bool passed = false;

const char* typeName(uint8_t type) {
    switch (type) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC";
    case CARD_NONE: return "NONE";
    default: return "UNKNOWN";
    }
}

bool runTest() {
    SPI.begin(14, 19, 23, 27);
    // Never format on mount failure. Use explicit pins and a low SPI frequency.
    if (!SD.begin(27, SPI, 1000000, "/sd", 5, false)) {
        Serial.println("SD_INIT_FAIL: SD.begin returned false; no formatting attempted");
        return false;
    }
    const uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
        Serial.println("SD_CARD_FAIL: no card detected after mount");
        return false;
    }
    const uint64_t capacity = SD.cardSize();
    Serial.printf("SD_INIT_OK type=%s capacity_bytes=%llu capacity_MiB=%llu spi_hz=1000000\n",
                  typeName(type), static_cast<unsigned long long>(capacity),
                  static_cast<unsigned long long>(capacity / (1024ULL * 1024ULL)));
    if (capacity == 0) {
        Serial.println("SD_CAPACITY_FAIL: zero capacity");
        return false;
    }
    // Only this explicitly requested test file may be created/overwritten.
    File out = SD.open(path, FILE_WRITE);
    if (!out || out.isDirectory()) {
        Serial.println("WRITE_OPEN_FAIL: /test.txt");
        out.close();
        return false;
    }
    const size_t written = out.write(reinterpret_cast<const uint8_t*>(expected), expectedBytes);
    out.flush();
    const int writeError = out.getWriteError();
    out.close();
    if (written != expectedBytes || writeError != 0) {
        Serial.printf("WRITE_FAIL bytes=%u expected=%u stream_error=%d\n",
                      static_cast<unsigned>(written), static_cast<unsigned>(expectedBytes), writeError);
        return false;
    }
    Serial.printf("WRITE_OK bytes=%u file=/test.txt\n", static_cast<unsigned>(written));

    File in = SD.open(path, FILE_READ);
    if (!in || in.isDirectory()) {
        Serial.println("READ_OPEN_FAIL: /test.txt");
        in.close();
        return false;
    }
    const size_t size = in.size();
    uint8_t actual[expectedBytes] = {};
    const size_t read = in.read(actual, sizeof(actual));
    const bool hasExtra = in.available() > 0;
    in.close();
    if (read != expectedBytes) {
        Serial.printf("READ_FAIL bytes=%u expected=%u file_size=%u\n",
                      static_cast<unsigned>(read), static_cast<unsigned>(expectedBytes),
                      static_cast<unsigned>(size));
        return false;
    }
    Serial.printf("READ_OK bytes=%u content_begin\n", static_cast<unsigned>(read));
    Serial.write(actual, read);
    Serial.println("content_end");
    if (size != expectedBytes || hasExtra || std::memcmp(actual, expected, expectedBytes) != 0) {
        Serial.printf("VERIFY_FAIL: exact content/length mismatch file_size=%u\n", static_cast<unsigned>(size));
        return false;
    }
    Serial.println("VERIFY_OK: exact byte-for-byte match including newline");
    return true;
}
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("Khazzan sd_card_test | SCK=14 MISO=19 MOSI=23 CS=27 | no format");
    passed = runTest();
    SD.end();
    SPI.end();
    Serial.println(passed ? "SD_TEST_PASS" : "SD_TEST_FAIL");
}

void loop() {
    // Report the result without repeating writes.
    delay(2000);
    Serial.println(passed ? "SD_TEST_PASS" : "SD_TEST_FAIL");
}
