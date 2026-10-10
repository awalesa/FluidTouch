// XModemSender test against an in-process receiver that behaves like
// FluidNC's $Xmodem/Receive: handshake a second after the command, repeated
// every 2 s; 1 KB (STX) and 128-byte (SOH) blocks; CRC or checksum mode;
// trailing SUB padding stripped from the file.
//
//   sim\build\xmodem_sender_test.exe
//
// Exits 0 if every case passes.

#include <Arduino.h>
#include <SD.h>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "network/xmodem_sender.h"

bool g_serialMuted = true;  // Keep the sender's LOG_* output quiet

static std::string g_data_dir;
namespace sim {
int g_argc = 0;
char** g_argv = nullptr;
void shutdown() { exit(0); }
const char *dataDir() { return g_data_dir.c_str(); }
}  // namespace sim

struct ReceiverOptions {
    bool accept1K = true;       // false: NAK every 1 KB block, like a receiver without XModem-1K
    bool checksumMode = false;  // true: ask for checksum packets (NAK handshake)
    bool cancelOnOpen = false;  // true: send CAN at once, like a destination that can't be opened
};

class FakeReceiver : public Stream {
public:
    explicit FakeReceiver(ReceiverOptions opt) : opt_(opt) {}

    std::vector<uint8_t> file;
    int blocks1K = 0, blocks128 = 0, rejected = 0;
    bool gotEot = false;

    size_t write(uint8_t c) override { process(c); return 1; }
    size_t write(const uint8_t *buf, size_t n) override {
        for (size_t i = 0; i < n; i++) process(buf[i]);
        return n;
    }
    using Print::write;
    int available() override { handshake(); return (int)rx_.size(); }
    int read() override {
        handshake();
        if (rx_.empty()) return -1;
        int b = rx_.front();
        rx_.pop_front();
        return b;
    }
    int peek() override { handshake(); return rx_.empty() ? -1 : rx_.front(); }
    void flush() override {}

private:
    ReceiverOptions opt_;
    std::deque<uint8_t> rx_;
    std::string line_;
    bool receiving_ = false;      // Command received, in XModem mode
    bool started_ = false;        // First packet seen: stop sending the handshake
    uint32_t nextHandshake_ = 0;
    std::vector<uint8_t> packet_;
    size_t want_ = 0;
    uint8_t expectSeq_ = 1;

    void handshake() {
        if (receiving_ && !started_ && millis() >= nextHandshake_) {
            rx_.push_back(opt_.checksumMode ? 0x15 : 'C');
            nextHandshake_ += 2000;
        }
    }

    void process(uint8_t c) {
        if (!receiving_) {
            if (c == '\n') {
                if (line_.rfind("$Xmodem/Receive=", 0) == 0) {
                    if (opt_.cancelOnOpen) {
                        rx_.push_back(0x18);
                    } else {
                        receiving_ = true;
                        nextHandshake_ = millis() + 1000;
                    }
                }
                line_.clear();
            } else {
                line_ += (char)c;
            }
            return;
        }
        if (want_ == 0) {
            if (c == 0x01 || c == 0x02) {
                started_ = true;
                size_t size = (c == 0x02) ? 1024 : 128;
                want_ = 1 + 2 + size + (opt_.checksumMode ? 1 : 2);
                packet_.assign(1, c);
            } else if (c == 0x04) {
                gotEot = true;
                while (!file.empty() && file.back() == 0x1A) file.pop_back();
                rx_.push_back(0x06);
            }
            return;
        }
        packet_.push_back(c);
        if (packet_.size() == want_) {
            checkPacket();
            want_ = 0;
        }
    }

    void checkPacket() {
        size_t size = (packet_[0] == 0x02) ? 1024 : 128;
        if (size == 1024 && !opt_.accept1K) {
            rejected++;
            rx_.push_back(0x15);
            return;
        }
        const uint8_t *data = &packet_[3];
        bool ok = packet_[1] == expectSeq_ && packet_[2] == (uint8_t)(255 - packet_[1]);
        if (opt_.checksumMode) {
            uint8_t sum = 0;
            for (size_t i = 0; i < size; i++) sum += data[i];
            ok = ok && sum == packet_[3 + size];
        } else {
            uint16_t crc = 0;
            for (size_t i = 0; i < size; i++) {
                crc ^= (uint16_t)data[i] << 8;
                for (int j = 0; j < 8; j++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
            }
            ok = ok && packet_[3 + size] == (crc >> 8) && packet_[4 + size] == (crc & 0xFF);
        }
        if (!ok) {
            rejected++;
            rx_.push_back(0x15);
            return;
        }
        file.insert(file.end(), data, data + size);
        (size == 1024 ? blocks1K : blocks128)++;
        expectSeq_++;
        rx_.push_back(0x06);
    }
};

static int failures = 0;

static void runCase(const char *name, size_t size, ReceiverOptions opt, bool expectOk,
                    int expect1K = -1, int expect128 = -1) {
    // Test file: varied bytes, never ending in SUB (which XModem can't keep)
    std::vector<uint8_t> original(size);
    for (size_t i = 0; i < size; i++) original[i] = (uint8_t)((i * 31 + 7) % 251);
    {
        std::ofstream out(std::filesystem::path(g_data_dir) / "sd" / "test.bin", std::ios::binary);
        out.write((const char *)original.data(), (std::streamsize)original.size());
    }
    fs::File file = SD.open("/test.bin");

    FakeReceiver rx(opt);
    char error[128] = "";
    uint32_t start = millis();
    bool ok = XModemSender::sendFile(rx, "/sd/test.bin", file, size, nullptr, error, sizeof(error));
    file.close();

    bool pass = ok == expectOk;
    if (pass && ok) {
        pass = rx.gotEot && rx.file == original &&
               (expect1K < 0 || rx.blocks1K == expect1K) &&
               (expect128 < 0 || rx.blocks128 == expect128);
    }
    printf("%-34s %s  ok=%d 1K=%d 128=%d rejected=%d %ums%s%s\n", name, pass ? "PASS" : "FAIL", ok,
           rx.blocks1K, rx.blocks128, rx.rejected, (unsigned)(millis() - start),
           error[0] ? "  error: " : "", error);
    if (!pass) failures++;
}

int main() {
    g_data_dir = (std::filesystem::temp_directory_path() / "fluidtouch_xmodem_test").string();
    std::filesystem::create_directories(std::filesystem::path(g_data_dir) / "sd");
    SD.begin(0, SPI, 4000000, "/sd", 5, false);

    ReceiverOptions std1K;
    runCase("empty file", 0, std1K, true, 0, 1);
    runCase("1 byte", 1, std1K, true, 0, 1);
    runCase("128 bytes", 128, std1K, true, 0, 1);
    runCase("129 bytes (one 1K block)", 129, std1K, true, 1, 0);
    runCase("1024 bytes", 1024, std1K, true, 1, 0);
    runCase("1025 bytes (1K + 128 tail)", 1025, std1K, true, 1, 1);
    runCase("3000 bytes", 3000, std1K, true, 3, 0);
    runCase("300 KB (sequence wraps)", 300 * 1024, std1K, true, 300, 0);

    ReceiverOptions no1K;
    no1K.accept1K = false;
    runCase("no 1K support: falls back", 3000, no1K, true, 0, 24);

    ReceiverOptions checksum;
    checksum.checksumMode = true;
    runCase("checksum mode", 3000, checksum, true, 3, 0);

    ReceiverOptions cancel;
    cancel.cancelOnOpen = true;
    runCase("can't open destination", 3000, cancel, false);

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
