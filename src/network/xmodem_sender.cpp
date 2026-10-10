#include "network/xmodem_sender.h"
#include "debug_log.h"

// In USB CDC mode the `serial` stream handed to this sender IS the Arduino
// `Serial` (UART0) debug console, so any raw Serial.print() here would be
// injected straight into the xmodem byte stream to FluidNC and corrupt the
// transfer. All logging therefore goes through the LOG_* macros, which are
// silenced by g_serialMuted while a USB CDC link is active.

// XModem control characters
#define XMODEM_SOH  0x01   // Start of 128-byte block
#define XMODEM_STX  0x02   // Start of 1024-byte block (XModem-1K)
#define XMODEM_EOT  0x04   // End of Transmission
#define XMODEM_ACK  0x06   // Acknowledge
#define XMODEM_NAK  0x15   // Negative Acknowledge
#define XMODEM_CAN  0x18   // Cancel
#define XMODEM_CRC  'C'    // Receiver ready for CRC mode
#define XMODEM_SUB  0x1A   // Pad byte for last block

#define XMODEM_BLOCK_SMALL  128
#define XMODEM_BLOCK_1K     1024
#define XMODEM_MAX_RETRIES  10
// Failed attempts at the first 1 KB block before falling back to 128 bytes
#define XMODEM_1K_ATTEMPTS  2

// Only one transfer runs at a time; static so they don't use the task's stack
static uint8_t data_buf[XMODEM_BLOCK_1K];
static uint8_t packet_buf[XMODEM_BLOCK_1K + 5];

// CRC-16-CCITT (polynomial 0x1021, init 0x0000)
uint16_t XModemSender::calcCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

int XModemSender::readByte(Stream& serial, uint32_t timeoutMs) {
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (serial.available()) {
            return serial.read();
        }
        vTaskDelay(1);
    }
    return -1; // timeout
}

bool XModemSender::drainUntilQuiet(Stream& serial, uint32_t quietMs) {
    bool cancelled = false;
    uint32_t start = millis();
    while (millis() - start < quietMs) {
        if (serial.available()) {
            if (serial.read() == XMODEM_CAN) {
                cancelled = true;
            }
            start = millis(); // reset timer on each byte so we truly drain
        } else {
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
    }
    return cancelled;
}

int XModemSender::sendPacket(Stream& serial, uint8_t seq, const uint8_t* data,
                             size_t blockSize, bool crcMode) {
    // Layout: SOH/STX, seq, ~seq, data, then CRC-16 (2 bytes, high first)
    // or an 8-bit checksum, whichever the receiver asked for
    packet_buf[0] = (blockSize == XMODEM_BLOCK_1K) ? XMODEM_STX : XMODEM_SOH;
    packet_buf[1] = seq;
    packet_buf[2] = (uint8_t)(255u - seq);
    memcpy(packet_buf + 3, data, blockSize);
    size_t len = blockSize + 3;
    if (crcMode) {
        uint16_t crc = calcCrc16(data, blockSize);
        packet_buf[len++] = (uint8_t)(crc >> 8);
        packet_buf[len++] = (uint8_t)(crc & 0xFF);
    } else {
        uint8_t sum = 0;
        for (size_t i = 0; i < blockSize; i++) sum += data[i];
        packet_buf[len++] = sum;
    }

    serial.write(packet_buf, len);
    serial.flush();

    // Wait for the reply, skipping any stray bytes
    uint32_t start = millis();
    while (millis() - start < 5000) {
        int b = readByte(serial, 5000 - (millis() - start));
        if (b < 0) break;
        if (b == XMODEM_ACK || b == XMODEM_NAK || b == XMODEM_CAN) return b;
        LOG_PRINTF("[XModem] Unexpected response 0x%02X\n", (uint8_t)b);
    }
    return -1;
}

void XModemSender::sendCancel(Stream& serial) {
    // FluidNC stops on two CANs in a row; send three, as receivers do
    serial.write((uint8_t)XMODEM_CAN);
    serial.write((uint8_t)XMODEM_CAN);
    serial.write((uint8_t)XMODEM_CAN);
    serial.flush();
    drainUntilQuiet(serial, 300);  // FluidNC replies with ACK
}

bool XModemSender::sendFile(Stream& serial,
                             const char* remotePath,
                             fs::File& file,
                             size_t fileSize,
                             ProgressCallback onProgress,
                             char* error,
                             size_t errorLen,
                             CancelCheck shouldCancel,
                             Stats* stats) {
    Stats localStats;
    if (!stats) stats = &localStats;
    *stats = Stats();
    auto fail = [&](const char* msg) {
        LOG_PRINTF("[XModem] ERROR: %s\n", msg);
        if (error && errorLen > 0) {
            strncpy(error, msg, errorLen - 1);
            error[errorLen - 1] = '\0';
        }
        return false;
    };
    auto cancelled = [&]() { return shouldCancel && shouldCancel(); };

    // -----------------------------------------------------------------------
    // Step 1: Send FluidNC command to put it into XModem-receive mode
    // -----------------------------------------------------------------------
    char cmd[280];
    snprintf(cmd, sizeof(cmd), "$Xmodem/Receive=%s\n", remotePath);
    serial.print(cmd);
    serial.flush();
    LOG_PRINTF("[XModem] Sent: %s", cmd);

    // -----------------------------------------------------------------------
    // Step 2: Drain FluidNC's reply until it goes quiet; it waits a second
    //         before sending its first 'C'. If it can't open the destination
    //         file it sends CAN straight away.
    // -----------------------------------------------------------------------
    if (drainUntilQuiet(serial, 500)) {
        return fail("FluidNC couldn't open the destination file");
    }

    // -----------------------------------------------------------------------
    // Step 3: Wait for the receiver's handshake: 'C' asks for CRC-16 packets,
    //         NAK for checksum packets. FluidNC repeats it every 2 seconds.
    // -----------------------------------------------------------------------
    LOG_PRINTLN("[XModem] Waiting for handshake...");
    bool receiverReady = false;
    bool crcMode = true;
    uint32_t waitStart = millis();
    while (millis() - waitStart < 30000) { // up to 30 s
        if (cancelled()) {
            sendCancel(serial);
            return fail(CANCELLED);
        }
        int b = readByte(serial, 300);
        if (b == XMODEM_CRC) {
            LOG_PRINTLN("[XModem] CRC handshake received");
            receiverReady = true;
            break;
        } else if (b == XMODEM_NAK) {
            LOG_PRINTLN("[XModem] Checksum handshake received");
            crcMode = false;
            receiverReady = true;
            break;
        } else if (b == XMODEM_CAN) {
            return fail("FluidNC cancelled the transfer");
        } else if (b >= 0) {
            // Ignore stray bytes (leftover echo text, etc.)
            LOG_PRINTF("[XModem] Skipping byte: 0x%02X\n", (uint8_t)b);
        }
    }

    if (!receiverReady) {
        return fail("No response from FluidNC");
    }

    // -----------------------------------------------------------------------
    // Step 4: Send the file in 1 KB blocks; a final part of 128 bytes or less
    //         goes in a 128-byte block. If FluidNC doesn't accept the first
    //         1 KB block, fall back to 128-byte blocks for the whole file.
    // -----------------------------------------------------------------------
    LOG_PRINTF("[XModem] Sending %u bytes\n", (unsigned)fileSize);

    size_t maxBlock = XMODEM_BLOCK_1K;
    size_t bytesSent = 0;
    uint8_t seqNum = 1;
    bool anyAcked = false;

    do {
        if (cancelled()) {
            sendCancel(serial);
            return fail(CANCELLED);
        }
        size_t chunk = fileSize - bytesSent;
        if (chunk > maxBlock) chunk = maxBlock;
        size_t blockSize = (chunk <= XMODEM_BLOCK_SMALL) ? XMODEM_BLOCK_SMALL : maxBlock;

        if (file.read(data_buf, chunk) != chunk) {
            return fail("Failed to read the file on the SD card");
        }
        // Pad the last block with SUB (FluidNC strips trailing SUBs)
        memset(data_buf + chunk, XMODEM_SUB, blockSize - chunk);

        bool blockAcked = false;
        bool fellBack = false;
        for (int retry = 0; retry < XMODEM_MAX_RETRIES && !blockAcked; retry++) {
            if (retry > 0) {
                if (cancelled()) {
                    sendCancel(serial);
                    return fail(CANCELLED);
                }
                LOG_PRINTF("[XModem] Retry %d for block %u\n", retry, (unsigned)seqNum);
                if (drainUntilQuiet(serial, 150)) {
                    return fail("FluidNC cancelled the transfer");
                }
                if (blockSize == XMODEM_BLOCK_1K && !anyAcked && retry >= XMODEM_1K_ATTEMPTS) {
                    // This FluidNC doesn't take 1 KB blocks: resend this part
                    // in 128-byte blocks with the same sequence number
                    LOG_PRINTLN("[XModem] 1 KB blocks not accepted, using 128-byte blocks");
                    maxBlock = XMODEM_BLOCK_SMALL;
                    file.seek(bytesSent);
                    fellBack = true;
                    break;
                }
            }

            int response = sendPacket(serial, seqNum, data_buf, blockSize, crcMode);
            if (response == XMODEM_ACK) {
                blockAcked = true;
            } else if (response == XMODEM_CAN) {
                // FluidNC cancels if it can't write a block, or after receiving
                // a block it already accepted three times (its ACKs were lost)
                return fail("FluidNC cancelled the transfer (too many resent blocks, or the destination is full)");
            } else if (response < 0) {
                LOG_PRINTLN("[XModem] Timeout waiting for ACK");
                stats->timeouts++;
            } else {
                stats->naks++;  // NAK: retry
            }
        }
        if (fellBack) continue;

        if (!blockAcked) {
            sendCancel(serial);
            char msg[64];
            snprintf(msg, sizeof(msg), "FluidNC didn't accept block %u", (unsigned)seqNum);
            return fail(msg);
        }

        anyAcked = true;
        bytesSent += chunk;
        if (onProgress) {
            onProgress(bytesSent, fileSize);
        }
        seqNum++; // uint8_t wraps 255 → 0 automatically
    } while (bytesSent < fileSize);

    // -----------------------------------------------------------------------
    // Step 5: End of Transmission
    // -----------------------------------------------------------------------
    LOG_PRINTLN("[XModem] Sending EOT...");
    for (int eotTry = 0; eotTry < 5; eotTry++) {
        serial.write((uint8_t)XMODEM_EOT);
        serial.flush();
        int response = readByte(serial, 5000);
        if (response == XMODEM_ACK) {
            LOG_PRINTLN("[XModem] Transfer complete");
            return true;
        }
        if (response == XMODEM_CAN) {
            return fail("FluidNC couldn't finish writing the file (is the destination full?)");
        }
        LOG_PRINTF("[XModem] EOT response 0x%02X, retry %d\n",
                      response >= 0 ? (uint8_t)response : 0xFF, eotTry + 1);
    }

    return fail("FluidNC didn't confirm the end of the transfer");
}
