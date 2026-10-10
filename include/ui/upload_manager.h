#ifndef UPLOAD_MANAGER_H
#define UPLOAD_MANAGER_H

#include <Arduino.h>

// State of the HTTP upload, written by the upload task and read by the LVGL
// loop (UITabFiles::checkUploadProgress). Progress fields are volatile so
// the compiler never caches them.
struct HttpUploadState {
    volatile bool active;            // Upload task running
    volatile bool completed;         // Finished (success, failure or cancelled)
    volatile bool success;           // Result when completed == true
    volatile bool cancelRequested;   // Set by the UI; the task stops sending
    volatile size_t bytesSent;       // File bytes sent so far
    volatile size_t totalBytes;      // File size
    char error[96];                  // Error message (written before completed=true)
};

class UploadManager {
public:
    // Error text when the upload was cancelled
    static constexpr const char* CANCELLED = "Upload cancelled";

    static bool init();
    // Start an HTTP upload to FluidNC's SD card (toFlash false, /upload) or
    // flash (toFlash true, /files) in a background task. destDir is the folder
    // on that volume, e.g. "/fluidtouch/uploads/". Only the default upload
    // folder (FLUIDNC_UPLOAD_PATH) is created if missing; other folders must
    // already exist. Returns false if an upload is already running or the
    // task couldn't start.
    static bool startUpload(const char* localPath,
                            const char* filename,
                            bool toFlash,
                            const char* destDir);
    static const HttpUploadState& state();
    static void cancel();
    static bool isUploading();

private:
    static HttpUploadState _state;
    static void uploadTask(void* params);
    static bool uploadFile(const char* localPath, const char* filename,
                           bool toFlash, const char* destDir);
    static bool ensureDirectoryExists(const String& machineIP, const char* endpoint, const String& dirPath);
};

#endif // UPLOAD_MANAGER_H
