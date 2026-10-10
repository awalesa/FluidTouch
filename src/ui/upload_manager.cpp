#include "ui/upload_manager.h"
#include "config.h"
#include "network/fluidnc_client.h"
#include <SD.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

HttpUploadState UploadManager::_state = {};

bool UploadManager::ensureDirectoryExists(const String& machineIP, const char* endpoint, const String& dirPath) {
    Serial.printf("[UploadManager] Ensuring directory exists: %s%s\n", endpoint, dirPath.c_str());

    // FluidNC's /upload (SD) and /files (flash) endpoints both handle directory creation
    // Parse path to create each parent directory level
    String parentPath = "";
    String workPath = dirPath;
    
    // Remove leading slash and trailing slash
    if (workPath.startsWith("/")) workPath = workPath.substring(1);
    if (workPath.endsWith("/")) workPath = workPath.substring(0, workPath.length() - 1);
    
    // Split path by '/' and create each level
    int slashPos = 0;
    while ((slashPos = workPath.indexOf('/')) > 0 || workPath.length() > 0) {
        String dirName;
        if (slashPos > 0) {
            dirName = workPath.substring(0, slashPos);
            workPath = workPath.substring(slashPos + 1);
        } else {
            dirName = workPath;
            workPath = "";
        }
        
        if (dirName.length() == 0) break;
        
        Serial.printf("[UploadManager] Creating directory: %s in path: %s\n", dirName.c_str(), parentPath.c_str());
        
        // Build URL: /upload?action=createdir&filename=dirname&path=/parent/path
        String url = "http://" + machineIP + endpoint + "?action=createdir&filename=";
        url += dirName;
        url += "&path=";
        url += parentPath.length() > 0 ? parentPath : "/";
        
        HTTPClient http;
        http.begin(url);
        http.setTimeout(3000);
        
        int httpCode = http.GET();
        if (httpCode > 0) {
            Serial.printf("[UploadManager] HTTP response: %d\n", httpCode);
            // 200 = success, even if directory already exists
        } else {
            Serial.printf("[UploadManager] HTTP request failed: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
        
        // Update parent path for next level
        parentPath += "/" + dirName;
        
        if (workPath.length() == 0) break;
    }
    
    return true;
}

bool UploadManager::init() {
    Serial.println("[UploadManager] Initializing SD card...");
    
#ifdef HARDWARE_ADVANCE
    Serial.println("[UploadManager] Advance: SPI mode (MOSI=6, MISO=4, CLK=5, CS=0/GND)");
    // CS is GPIO 0 per Elecrow example, though physically tied to GND
    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 4000000, "/sd", 5, false)) {
#else
    Serial.println("[UploadManager] Basic: SPI mode (MOSI=11, MISO=13, CLK=12, CS=10)");
    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 4000000, "/sd", 5, false)) {  // Use configured CS pin
#endif
        Serial.println("[UploadManager] SD Card mount failed");
        return false;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[UploadManager] No SD card attached");
        return false;
    }
    
    Serial.printf("[UploadManager] SD Card Type: %s\n",
                 cardType == CARD_MMC ? "MMC" :
                 cardType == CARD_SD ? "SDSC" :
                 cardType == CARD_SDHC ? "SDHC" : "UNKNOWN");
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[UploadManager] SD Card Size: %lluMB\n", cardSize);
    
    return true;
}

struct UploadTaskParams {
    char localPath[256];
    char filename[128];
    char destDir[160];
    bool toFlash;
};

bool UploadManager::startUpload(const char* localPath,
                                const char* filename,
                                bool toFlash,
                                const char* destDir) {
    if (_state.active) {
        Serial.println("[UploadManager] Upload already in progress");
        return false;
    }
    UploadTaskParams* params = (UploadTaskParams*)malloc(sizeof(UploadTaskParams));
    if (!params) return false;
    strncpy(params->localPath, localPath, sizeof(params->localPath) - 1);
    params->localPath[sizeof(params->localPath) - 1] = '\0';
    strncpy(params->filename, filename, sizeof(params->filename) - 1);
    params->filename[sizeof(params->filename) - 1] = '\0';
    strncpy(params->destDir, destDir ? destDir : "/", sizeof(params->destDir) - 1);
    params->destDir[sizeof(params->destDir) - 1] = '\0';
    params->toFlash = toFlash;

    memset((void*)&_state, 0, sizeof(_state));
    _state.active = true;

    // Runs on core 0 like the XModem upload, so the UI and the FluidNC
    // connection keep running and the Cancel button works
    BaseType_t rc = xTaskCreatePinnedToCore(uploadTask, "http_upload", 8192, params, 5, nullptr, 0);
    if (rc != pdPASS) {
        Serial.println("[UploadManager] xTaskCreate failed");
        free(params);
        _state.active = false;
        return false;
    }
    return true;
}

void UploadManager::uploadTask(void* pvParams) {
    UploadTaskParams* params = (UploadTaskParams*)pvParams;
    bool ok = uploadFile(params->localPath, params->filename, params->toFlash, params->destDir);
    free(params);

    if (ok) _state.error[0] = '\0';
    _state.success = ok;
    _state.completed = true;
    _state.active = false;
    Serial.printf("[UploadManager] Upload %s\n", ok ? "completed" : "failed");
    vTaskDelete(nullptr);
}

const HttpUploadState& UploadManager::state() {
    return _state;
}

void UploadManager::cancel() {
    if (_state.active) {
        _state.cancelRequested = true;
    }
}

bool UploadManager::isUploading() {
    return _state.active;
}

// Runs in the upload task. Reports progress through _state; on failure the
// reason is in _state.error.
bool UploadManager::uploadFile(const char* localPath,
                               const char* filename,
                               bool toFlash,
                               const char* destDirArg) {
    auto fail = [](const char* msg) {
        Serial.printf("[UploadManager] %s\n", msg);
        strncpy(_state.error, msg, sizeof(_state.error) - 1);
        _state.error[sizeof(_state.error) - 1] = '\0';
        return false;
    };

    Serial.printf("[UploadManager] Starting upload of %s\n", localPath);

    // Open local file
    File file = SD.open(localPath);
    if (!file) {
        return fail("Failed to open local file");
    }

    size_t fileSize = file.size();
    _state.totalBytes = fileSize;
    Serial.printf("[UploadManager] File size: %d bytes\n", fileSize);

    // Support files up to 10MB
    if (fileSize > 10485760) {  // 10MB limit
        file.close();
        return fail("File too large (max 10MB)");
    }

    // Allocate streaming buffer in PSRAM (16KB chunks)
    const size_t CHUNK_SIZE = 16384;
    uint8_t* buffer = (uint8_t*)heap_caps_malloc(CHUNK_SIZE, MALLOC_CAP_SPIRAM);
    if (!buffer) {
        file.close();
        return fail("Out of memory");
    }

    // Get FluidNC IP (already resolved from hostname/mDNS during connect())
    String machineIP = FluidNCClient::getMachineIP();
    if (machineIP.isEmpty()) {
        heap_caps_free(buffer);
        file.close();
        return fail("FluidNC not connected");
    }

    // Build remote path - remove leading slash from filename if present
    String filenameStr = String(filename);
    if (filenameStr.startsWith("/")) {
        filenameStr = filenameStr.substring(1);
    }

    // FluidNC expects path as directory only, filename goes in form field name
    const char* endpoint = toFlash ? "/files" : "/upload";
    String destDir = String(destDirArg && *destDirArg ? destDirArg : "/");
    if (!destDir.startsWith("/")) destDir = "/" + destDir;
    if (!destDir.endsWith("/")) destDir += "/";
    String fullPath = destDir + filenameStr;

    // Only the default upload folder is created; any other folder must already
    // exist (the Files tab checks before uploading)
    if (destDir == FLUIDNC_UPLOAD_PATH && !ensureDirectoryExists(machineIP, endpoint, destDir)) {
        Serial.println("[UploadManager] Warning: Could not verify directory creation");
        // Continue anyway - the upload might still work if directory exists
    }

    // Get current timestamp
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &timeinfo);

    String url = "http://" + machineIP + endpoint;
    Serial.printf("[UploadManager] Uploading to %s (path: %s)\n", url.c_str(), fullPath.c_str());

    // Build multipart boundary
    String boundary = "----WebKitFormBoundary" + String(random(0xffff), HEX);
    String contentType = "multipart/form-data; boundary=" + boundary;

    // Build multipart body parts
    // Part 1: path field (destination directory)
    String part1 = "--" + boundary + "\r\n";
    part1 += "Content-Disposition: form-data; name=\"path\"\r\n\r\n";
    part1 += destDir + "\r\n";

    // Part 2: file size field
    String sizeFieldName = fullPath + "S";
    String part2 = "--" + boundary + "\r\n";
    part2 += "Content-Disposition: form-data; name=\"" + sizeFieldName + "\"\r\n\r\n";
    part2 += String(fileSize) + "\r\n";

    // Part 3: timestamp field
    String timeFieldName = fullPath + "T";
    String part3 = "--" + boundary + "\r\n";
    part3 += "Content-Disposition: form-data; name=\"" + timeFieldName + "\"\r\n\r\n";
    part3 += String(timestamp) + "\r\n";

    // Part 4: file data header
    String part4 = "--" + boundary + "\r\n";
    part4 += "Content-Disposition: form-data; name=\"myfiles\"; filename=\"" + fullPath + "\"\r\n";
    part4 += "Content-Type: application/octet-stream\r\n\r\n";

    String footer = "\r\n--" + boundary + "--\r\n";

    size_t totalSize = part1.length() + part2.length() + part3.length() + part4.length() + fileSize + footer.length();
    Serial.printf("[UploadManager] Total multipart size: %d bytes\n", totalSize);

    if (_state.cancelRequested) {
        heap_caps_free(buffer);
        file.close();
        return fail(CANCELLED);
    }

    // Create WiFi client and connect directly
    WiFiClient client;
    if (!client.connect(machineIP.c_str(), 80)) {
        heap_caps_free(buffer);
        file.close();
        return fail("Connection failed");
    }

    Serial.println("[UploadManager] Connected, sending request...");

    // Send HTTP headers
    client.print(String("POST ") + endpoint + " HTTP/1.1\r\n");
    client.print("Host: " + machineIP + "\r\n");
    client.print("Content-Type: " + contentType + "\r\n");
    client.print("Content-Length: " + String(totalSize) + "\r\n");
    client.print("Connection: close\r\n\r\n");

    // Send multipart parts
    client.print(part1);
    client.print(part2);
    client.print(part3);
    client.print(part4);

    Serial.println("[UploadManager] Headers sent, streaming file...");

    // Stream file in chunks
    size_t bytesUploaded = 0;
    size_t lastReportedPercent = 0;
    bool success = true;
    bool cancelled = false;

    while (file.available()) {
        if (_state.cancelRequested) {
            // Dropping the connection aborts the upload; FluidNC discards the partial file
            cancelled = true;
            success = false;
            break;
        }
        size_t bytesRead = file.read(buffer, CHUNK_SIZE);
        size_t bytesWritten = client.write(buffer, bytesRead);

        if (bytesWritten != bytesRead) {
            Serial.println("[UploadManager] Write error");
            success = false;
            break;
        }

        bytesUploaded += bytesWritten;
        _state.bytesSent = bytesUploaded;

        size_t currentPercent = (bytesUploaded * 100) / fileSize;
        if (currentPercent >= lastReportedPercent + 10 || bytesUploaded == fileSize) {
            lastReportedPercent = currentPercent;
            Serial.printf("[UploadManager] Progress: %d%% (%d/%d bytes)\n", currentPercent, bytesUploaded, fileSize);
        }

        // Small delay for network and watchdog
        delay(1);
    }

    file.close();

    // Send footer
    if (success) {
        client.print(footer);
        Serial.println("[UploadManager] All data sent, waiting for response...");
    }

    // Get response
    int httpCode = -1;
    if (success) {
        unsigned long timeout = millis();
        while (client.available() == 0) {
            if (millis() - timeout > 10000) {
                Serial.println("[UploadManager] Response timeout");
                success = false;
                break;
            }
            delay(10);
        }

        if (success) {
            String response = client.readStringUntil('\n');
            Serial.printf("[UploadManager] Response: %s\n", response.c_str());

            if (response.indexOf("200") >= 0) httpCode = 200;
            else if (response.indexOf("201") >= 0) httpCode = 201;
            else {
                Serial.println("[UploadManager] Upload failed - bad response");
                success = false;
                httpCode = 500;
            }
        }
    }

    client.stop();
    heap_caps_free(buffer);

    Serial.printf("[UploadManager] HTTP response code: %d\n", httpCode);

    if (cancelled) {
        return fail(CANCELLED);
    }
    if (success && (httpCode == 200 || httpCode == 201)) {
        return true;
    }
    return fail(httpCode < 0 ? "Connection error" : "Server error");
}
