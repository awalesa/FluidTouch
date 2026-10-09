#ifndef UPLOAD_MANAGER_H
#define UPLOAD_MANAGER_H

#include <Arduino.h>
#include <functional>

class UploadManager {
public:
    using ProgressCallback = std::function<void(size_t current, size_t total)>;
    using CompleteCallback = std::function<void(bool success, const char* error)>;

    static bool init();
    // Upload over HTTP to FluidNC's SD card (toFlash false, /upload) or flash
    // (toFlash true, /files). destDir is the folder on that volume, e.g.
    // "/fluidtouch/uploads/". Only the default upload folder (FLUIDNC_UPLOAD_PATH)
    // is created if missing; other folders must already exist.
    static bool uploadFile(const char* localPath,
                          const char* filename,
                          bool toFlash,
                          const char* destDir,
                          ProgressCallback onProgress,
                          CompleteCallback onComplete);
    static bool isUploading();

private:
    static bool _uploading;
    static bool ensureDirectoryExists(const String& machineIP, const char* endpoint, const String& dirPath);
};

#endif // UPLOAD_MANAGER_H
