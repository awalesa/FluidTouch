#ifndef XMODEM_SENDER_H
#define XMODEM_SENDER_H

#include <Arduino.h>
#include <FS.h>
#include <functional>

// XModem sender for FluidNC's $Xmodem/Receive=<path> command.
// Sends 1 KB blocks (XModem-1K), falling back to 128-byte blocks if FluidNC
// doesn't accept the first 1 KB block, and uses CRC-16 or checksum packets,
// whichever FluidNC asks for. Must be called from a dedicated FreeRTOS task
// so it can block waiting for each ACK without stalling the LVGL loop.
class XModemSender {
public:
    using ProgressCallback = std::function<void(size_t bytesSent, size_t totalBytes)>;

    // Sends the $Xmodem/Receive=<remotePath> command then transfers the open
    // file. The file must be opened for reading and positioned at byte 0.
    // fileSize must match the actual file size. Returns true on success; on
    // failure a description is written to error (if given).
    static bool sendFile(Stream& serial,
                         const char* remotePath,
                         fs::File& file,
                         size_t fileSize,
                         ProgressCallback onProgress = nullptr,
                         char* error = nullptr,
                         size_t errorLen = 0);

private:
    static uint16_t calcCrc16(const uint8_t* data, size_t len);
    // Reads one byte with a timeout.  Returns -1 on timeout.
    static int readByte(Stream& serial, uint32_t timeoutMs);
    // Drains incoming bytes until none arrive for quietMs. Returns true if
    // FluidNC sent CAN (it does so at once if it can't open the file).
    static bool drainUntilQuiet(Stream& serial, uint32_t quietMs);
    // Sends one packet and waits for the reply: ACK, NAK, CAN, or -1 on timeout
    static int sendPacket(Stream& serial, uint8_t seq, const uint8_t* data,
                          size_t blockSize, bool crcMode);
};

#endif // XMODEM_SENDER_H
