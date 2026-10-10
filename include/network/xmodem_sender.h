#ifndef XMODEM_SENDER_H
#define XMODEM_SENDER_H

#include <Arduino.h>
#include <FS.h>
#include <functional>

// XModem sender for FluidNC's $Xmodem/Receive=<path> command.
// Sends 1 KB blocks (XModem-1K), switching to 128-byte blocks if FluidNC keeps
// rejecting a 1 KB block (no XModem-1K support, or a link that loses bytes in
// long bursts, such as USB CDC at a high baud), and uses CRC-16 or checksum packets,
// whichever FluidNC asks for. Must be called from a dedicated FreeRTOS task
// so it can block waiting for each ACK without stalling the LVGL loop.
class XModemSender {
public:
    using ProgressCallback = std::function<void(size_t bytesSent, size_t totalBytes)>;
    using CancelCheck = std::function<bool()>;

    // Problems during a transfer, for diagnosing a poor link
    struct Stats {
        uint32_t naks = 0;      // Blocks FluidNC rejected (corrupted or incomplete)
        uint32_t timeouts = 0;  // Blocks with no reply in time (e.g. a lost ACK); resent
        bool smallBlocks = false;  // Switched to 128-byte blocks after 1 KB blocks were rejected
    };

    // Error text when the transfer was cancelled through shouldCancel
    static constexpr const char* CANCELLED = "Cancelled";

    // Sends the $Xmodem/Receive=<remotePath> command then transfers the open
    // file. The file must be opened for reading and positioned at byte 0.
    // fileSize must match the actual file size. Returns true on success; on
    // failure a description is written to error (if given). shouldCancel is
    // checked before each block and while waiting for the receiver; when it
    // returns true the transfer is cancelled and error is CANCELLED.
    static bool sendFile(Stream& serial,
                         const char* remotePath,
                         fs::File& file,
                         size_t fileSize,
                         ProgressCallback onProgress = nullptr,
                         char* error = nullptr,
                         size_t errorLen = 0,
                         CancelCheck shouldCancel = nullptr,
                         Stats* stats = nullptr);

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
    // Tells the receiver to stop (CAN CAN CAN) and waits for it to go quiet
    static void sendCancel(Stream& serial);
};

#endif // XMODEM_SENDER_H
