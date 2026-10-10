#include "ui/tabs/ui_tab_files.h"
#include "ui/ui_theme.h"
#include "ui/ui_tabs.h"
#include "ui/upload_manager.h"
#include "network/fluidnc_client.h"
#include "config.h"
#include <Arduino.h>
#include <algorithm>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

// Static member initialization
lv_obj_t *UITabFiles::file_list_container = nullptr;
lv_obj_t *UITabFiles::status_label = nullptr;
lv_obj_t *UITabFiles::path_label = nullptr;
lv_obj_t *UITabFiles::storage_dropdown = nullptr;
lv_obj_t *UITabFiles::upload_dialog = nullptr;
lv_obj_t *UITabFiles::upload_progress_dialog = nullptr;
lv_obj_t *UITabFiles::upload_progress_bar = nullptr;
lv_obj_t *UITabFiles::upload_progress_label = nullptr;
lv_obj_t *UITabFiles::xmodem_progress_dialog = nullptr;
lv_obj_t *UITabFiles::xmodem_progress_bar = nullptr;
lv_obj_t *UITabFiles::xmodem_progress_label = nullptr;
std::vector<std::string> UITabFiles::file_names;
std::string UITabFiles::current_path = "/sd/";  // Default to SD card root
bool UITabFiles::initial_load_done = false;     // Track initial load
bool UITabFiles::refresh_pending = false;       // Track pending refresh request
StorageSource UITabFiles::current_storage = StorageSource::FLUIDNC_SD;

// Cache for each storage source
UITabFiles::StorageCache UITabFiles::fluidnc_sd_cache = {"", false, {}};
UITabFiles::StorageCache UITabFiles::fluidnc_flash_cache = {"", false, {}};
UITabFiles::StorageCache UITabFiles::display_sd_cache = {"", false, {}};

// Helper to get current storage cache
static UITabFiles::StorageCache* getCurrentCache() {
    if (UITabFiles::current_storage == StorageSource::FLUIDNC_SD) {
        return &UITabFiles::fluidnc_sd_cache;
    } else if (UITabFiles::current_storage == StorageSource::FLUIDNC_FLASH) {
        return &UITabFiles::fluidnc_flash_cache;
    } else {
        return &UITabFiles::display_sd_cache;
    }
}

// Check if Display SD card is available
bool UITabFiles::isDisplaySDAvailable() {
    // First check card type
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[Files] No SD card detected (cardType = NONE)");
        return false;
    }
    
    // Also verify we can actually access the card by checking card size
    // If card was removed, this should fail
    uint64_t cardSize = SD.cardSize();
    if (cardSize == 0) {
        Serial.println("[Files] SD card not accessible (cardSize = 0)");
        return false;
    }
    
    return true;
}

// Request a refresh (called from callbacks)
void UITabFiles::requestRefresh() {
    refresh_pending = true;
}

// Check and execute pending refresh (called from main loop)
void UITabFiles::checkPendingRefresh() {
    if (!refresh_pending) return;
    
    refresh_pending = false;
    
    // Clear cache for current storage
    if (current_storage == StorageSource::FLUIDNC_SD) {
        fluidnc_sd_cache.is_cached = false;
    } else if (current_storage == StorageSource::FLUIDNC_FLASH) {
        fluidnc_flash_cache.is_cached = false;
    } else if (current_storage == StorageSource::DISPLAY_SD) {
        display_sd_cache.is_cached = false;
    }
    
    // Refresh the file list
    if (current_storage == StorageSource::DISPLAY_SD) {
        listDisplaySDFiles(current_path);
    } else {
        refreshFileList(current_path);
    }
}

void UITabFiles::create(lv_obj_t *tab) {
    lv_obj_set_style_bg_color(tab, UITheme::BG_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 10, 0);

    // Storage selection dropdown
    storage_dropdown = lv_dropdown_create(tab);
    lv_dropdown_set_options(storage_dropdown, "FluidNC SD\nFluidNC Flash\nDisplay SD");
    lv_dropdown_set_selected(storage_dropdown, 0);
    lv_obj_set_size(storage_dropdown, 150, 45);
    lv_obj_set_pos(storage_dropdown, 5, 5);
    lv_obj_set_style_text_font(storage_dropdown, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(storage_dropdown, UITheme::BG_BUTTON, 0);
    lv_obj_set_style_text_color(storage_dropdown, lv_color_white(), 0);
    lv_obj_add_event_cb(storage_dropdown, storage_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    
    // Get the dropdown list and set its height to show all 3 options without scrolling
    lv_obj_t *dropdown_list = lv_dropdown_get_list(storage_dropdown);
    if (dropdown_list) {
        lv_obj_set_style_max_height(dropdown_list, 150, 0);  // 3 items × ~48px each
    }

    // Refresh button
    lv_obj_t *btn_refresh = lv_button_create(tab);
    lv_obj_set_size(btn_refresh, 120, 45);
    lv_obj_set_pos(btn_refresh, 165, 5);
    lv_obj_set_style_bg_color(btn_refresh, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_add_event_cb(btn_refresh, refresh_button_event_cb, LV_EVENT_CLICKED, nullptr);
    
    lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(lbl_refresh, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_set_style_text_font(lbl_refresh, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_refresh);

    // Up button (navigate to parent directory)
    lv_obj_t *btn_up = lv_button_create(tab);
    lv_obj_set_size(btn_up, 100, 45);
    lv_obj_set_pos(btn_up, 295, 5);
    lv_obj_set_style_bg_color(btn_up, UITheme::BG_BUTTON, 0);
    lv_obj_add_event_cb(btn_up, up_button_event_cb, LV_EVENT_CLICKED, nullptr);
    
    lv_obj_t *lbl_up = lv_label_create(btn_up);
    lv_label_set_text(lbl_up, LV_SYMBOL_UP " Up");
    lv_obj_set_style_text_font(lbl_up, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_up);

    // Path label (shows current directory)
    path_label = lv_label_create(tab);
    lv_label_set_text(path_label, "/sd/");
    lv_obj_set_style_text_font(path_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(path_label, UITheme::ACCENT_SECONDARY, 0);
    lv_obj_set_pos(path_label, 415, 9);
    lv_label_set_long_mode(path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(path_label, 250);

    // Status label
    status_label = lv_label_create(tab);
    lv_label_set_text(status_label, "Click Refresh");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_label, UITheme::UI_INFO, 0);
    lv_obj_set_pos(status_label, 415, 32);

    // File list container with scrolling
    file_list_container = lv_obj_create(tab);
    lv_obj_set_size(file_list_container, 770, 270);
    lv_obj_set_pos(file_list_container, 5, 65);
    lv_obj_set_style_bg_color(file_list_container, UITheme::BG_DARKER, LV_PART_MAIN);
    lv_obj_set_style_border_color(file_list_container, UITheme::BORDER_LIGHT, LV_PART_MAIN);
    lv_obj_set_style_border_width(file_list_container, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(file_list_container, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(file_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(file_list_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(file_list_container, 6, 0);  // 6px spacing between file rows
    lv_obj_set_scroll_dir(file_list_container, LV_DIR_VER);
}

void UITabFiles::refreshFileList() {
    // Only auto-load once on first tab selection
    if (!initial_load_done) {
        initial_load_done = true;
        refreshFileList(current_path);
    }
}

void UITabFiles::refreshFileList(const std::string &path) {
    if (!FluidNCClient::isConnected()) {
        if (status_label) {
            lv_label_set_text(status_label, "Not connected");
            lv_obj_set_style_text_color(status_label, UITheme::UI_WARNING, 0);
        }
        Serial.println("[Files] Not connected to FluidNC");
        return;
    }
    
    // Check if machine is in IDLE state - don't fetch files if machine is running
    const FluidNCStatus& status = FluidNCClient::getStatus();
    if (!status.isIdle()) {
        if (status_label) {
            lv_label_set_text(status_label, "Machine must be IDLE to list files");
            lv_obj_set_style_text_color(status_label, UITheme::UI_WARNING, 0);
        }
        Serial.printf("[Files] Machine not in IDLE state (state=%d), skipping file list\n", status.state);
        return;
    }
    
    // Update current path
    current_path = path;
    
    // Update path label
    if (path_label) {
        lv_label_set_text(path_label, path.c_str());
    }
    
    if (status_label) {
        lv_label_set_text(status_label, "Loading...");
        lv_obj_set_style_text_color(status_label, UITheme::UI_INFO, 0);
    }
    
    Serial.printf("[Files] Requesting file list for: %s\n", path.c_str());
    
    // Clear previous file list
    file_names.clear();
    
    // Register callback to receive JSON file list response
    FluidNCClient::setMessageCallback([](const char* message) {
        // Accumulate multi-line JSON responses
        static String jsonBuffer;
        static bool collecting = false;
        static uint32_t lastMessageTime = 0;
        
        String msg(message);
        // Remove only line endings (\r\n), not spaces - preserves trailing spaces in filenames
        while (msg.endsWith("\r") || msg.endsWith("\n")) {
            msg.remove(msg.length() - 1);
        }
        uint32_t now = millis();
        
        // Reset buffer if it's been more than 3000ms since last message
        if (now - lastMessageTime > 3000) {
            if (jsonBuffer.length() > 0 && collecting) {
                Serial.printf("[Files] Timeout - parsing JSON buffer (%d bytes)\n", jsonBuffer.length());
                parseFileList(jsonBuffer.c_str());
                FluidNCClient::clearMessageCallback();
            }
            jsonBuffer = "";
            collecting = false;
        }
        lastMessageTime = now;
        
        // Skip status reports and GCode state messages
        if (msg.startsWith("<") || msg.startsWith("[GC:") || msg.startsWith("[MSG:")) {
            return;
        }
        
        // Skip PING messages during JSON collection ("PING:..." or bare "PING" depending on FluidNC version)
        if (msg == "PING" || msg.startsWith("PING:")) {
            return;
        }

        // Detect end of JSON response (ok line) - check this BEFORE printing/processing.
        // FluidNC v4 (port 80) sends the response in ~100 byte chunks and appends the
        // terminating "ok" to the last chunk ("...}\nok") rather than sending it on its
        // own - split it off so the list is parsed immediately instead of on the timeout.
        bool response_complete = msg.equalsIgnoreCase("ok");
        if (!response_complete) {
            int last_newline = msg.lastIndexOf('\n');
            if (last_newline >= 0) {
                String last_line = msg.substring(last_newline + 1);
                last_line.trim();
                if (last_line.equalsIgnoreCase("ok")) {
                    response_complete = true;
                    msg = msg.substring(0, last_newline);
                    while (msg.endsWith("\r")) {
                        msg.remove(msg.length() - 1);
                    }
                }
            }
        }

        if (response_complete && msg.equalsIgnoreCase("ok")) {
            msg = "";  // Nothing to collect - just the terminator
        }

        if (msg.length() > 0) {
            Serial.printf("[Files] Received line: %s\n", msg.c_str());
        }

        // Start collecting when we see JSON start (either [JSON: wrapper or raw JSON with {"files")
        if (msg.length() == 0) {
            // Terminator only
        } else if (msg.startsWith("[JSON:") || msg.startsWith("{\"files")) {
            if (!collecting) {
                Serial.println("[Files] Starting to collect JSON response");
                collecting = true;
                jsonBuffer = "";
            }
            // Remove [JSON: prefix and ] suffix if present
            String jsonLine = msg;
            if (jsonLine.startsWith("[JSON:")) {
                jsonLine.replace("[JSON:", "");
                if (jsonLine.endsWith("]")) {
                    jsonLine.remove(jsonLine.length() - 1);
                }
            }
            jsonBuffer += jsonLine;
            Serial.printf("[Files] JSON buffer now: %d bytes\n", jsonBuffer.length());
        } else if (collecting) {
            // Continue collecting any other lines while in collection mode
            jsonBuffer += msg;
            Serial.printf("[Files] JSON buffer now: %d bytes\n", jsonBuffer.length());
        }

        if (response_complete && collecting) {
            Serial.printf("[Files] Received 'ok', parsing %d bytes\n", jsonBuffer.length());
            parseFileList(jsonBuffer.c_str());
            jsonBuffer = "";
            collecting = false;
            FluidNCClient::clearMessageCallback();
        }
    });
    
    // Send JSON file list command for specified path
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "$Files/ListGcode=%s\n", path.c_str());
    FluidNCClient::sendCommand(cmd);
}

void UITabFiles::refresh_button_event_cb(lv_event_t *e) {
    Serial.println("[Files] Refresh button clicked");
    
    // Clear cache for current storage
    if (current_storage == StorageSource::FLUIDNC_SD) {
        fluidnc_sd_cache.is_cached = false;
    } else if (current_storage == StorageSource::FLUIDNC_FLASH) {
        fluidnc_flash_cache.is_cached = false;
    } else if (current_storage == StorageSource::DISPLAY_SD) {
        display_sd_cache.is_cached = false;
        
        // For Display SD, if card is not available, try to initialize it
        if (!isDisplaySDAvailable()) {
            Serial.println("[Files] SD card not available, attempting to initialize...");
            if (UploadManager::init()) {
                Serial.println("[Files] SD card initialized successfully");
            } else {
                Serial.println("[Files] SD card initialization failed");
            }
        }
    }
    
    // Use appropriate refresh function based on storage type
    if (current_storage == StorageSource::DISPLAY_SD) {
        listDisplaySDFiles(current_path);
    } else {
        refreshFileList(current_path);  // Always refresh with current path, not just on initial load
    }
}

void UITabFiles::storage_dropdown_event_cb(lv_event_t *e) {
    lv_obj_t *dropdown = (lv_obj_t*)lv_event_get_target(e);
    uint16_t selected = lv_dropdown_get_selected(dropdown);
    Serial.printf("[Files] Storage changed to index: %d\n", selected);
    
    current_storage = static_cast<StorageSource>(selected);
    
    // Switch storage root path
    if (current_storage == StorageSource::FLUIDNC_SD) {
        // FluidNC SD Card
        Serial.println("[Files] Switched to FluidNC SD Card");
        
        // Check if already cached - restore cached path
        if (fluidnc_sd_cache.is_cached) {
            current_path = fluidnc_sd_cache.cached_path;
            Serial.printf("[Files] Using cached FluidNC SD file list (%d items) at %s\n", fluidnc_sd_cache.file_list.size(), current_path.c_str());
            if (path_label) {
                lv_label_set_text(path_label, current_path.c_str());
            }
            updateFileListUI();  // Display the cached data (will update status_label with counts)
            return;
        }
        
        // Not cached - start at root
        current_path = "/sd/";
        refreshFileList(current_path);
    } else if (current_storage == StorageSource::FLUIDNC_FLASH) {
        // FluidNC Flash
        Serial.println("[Files] Switched to FluidNC Flash");
        
        // Check if already cached - restore cached path
        if (fluidnc_flash_cache.is_cached) {
            current_path = fluidnc_flash_cache.cached_path;
            Serial.printf("[Files] Using cached FluidNC Flash file list (%d items) at %s\n", fluidnc_flash_cache.file_list.size(), current_path.c_str());
            if (path_label) {
                lv_label_set_text(path_label, current_path.c_str());
            }
            updateFileListUI();  // Display the cached data (will update status_label with counts)
            return;
        }
        
        // Not cached - start at root
        current_path = "/localfs/";
        refreshFileList(current_path);
    } else if (current_storage == StorageSource::DISPLAY_SD) {
        // Display SD Card
        Serial.println("[Files] Switched to Display SD Card");
        
        // Check if already cached - restore cached path
        if (display_sd_cache.is_cached) {
            // Verify SD card is still available before using cache
            if (!isDisplaySDAvailable()) {
                Serial.println("[Files] SD card no longer available, invalidating cache");
                display_sd_cache.is_cached = false;
                display_sd_cache.file_list.clear();
                if (status_label) {
                    lv_label_set_text(status_label, "SD card not available");
                    lv_obj_set_style_text_color(status_label, UITheme::UI_WARNING, 0);
                }
                if (path_label) {
                    lv_label_set_text(path_label, "/");
                }
                // Clear file list UI
                if (file_list_container) {
                    lv_obj_clean(file_list_container);
                }
                return;
            }
            
            current_path = display_sd_cache.cached_path;
            Serial.printf("[Files] Using cached Display SD file list (%d items) at %s\n", display_sd_cache.file_list.size(), current_path.c_str());
            if (path_label) {
                lv_label_set_text(path_label, current_path.c_str());
            }
            updateFileListUI();  // Display the cached data (will update status_label with counts)
            return;
        }
        
        // Not cached - start at root and initialize SD card if not already done
        current_path = "/";
        Serial.println("[Files] Attempting to initialize SD card...");
        bool sdInitialized = false;
        
        // Wrap SD initialization in try-catch equivalent (check for crashes)
        sdInitialized = UploadManager::init();
        
        if (!sdInitialized) {
            Serial.println("[Files] Display SD card initialization failed");
            if (status_label) {
                lv_label_set_text(status_label, "SD card not available");
                lv_obj_set_style_text_color(status_label, UITheme::UI_WARNING, 0);
            }
            if (path_label) {
                lv_label_set_text(path_label, "/");
            }
            // Clear file list UI
            if (file_list_container) {
                lv_obj_clean(file_list_container);
            }
            return;
        }
        
        Serial.println("[Files] SD card initialized successfully");
        listDisplaySDFiles(current_path);
    }
    
    // Update path label
    if (path_label) {
        lv_label_set_text(path_label, current_path.c_str());
    }
}

void UITabFiles::up_button_event_cb(lv_event_t *e) {
    Serial.printf("[Files] Up button clicked, current path: %s\n", current_path.c_str());
    
    // Don't go above storage root (/sd/ or /localfs/ or /)
    if (current_path == "/sd/" || current_path == "/sd" || 
        current_path == "/localfs/" || current_path == "/localfs" ||
        current_path == "/") {
        Serial.println("[Files] Already at root, cannot go up");
        if (status_label) {
            lv_label_set_text(status_label, "At root");
            lv_obj_set_style_text_color(status_label, UITheme::UI_WARNING, 0);
        }
        return;
    }
    
    std::string parent = getParentPath(current_path);
    Serial.printf("[Files] Navigating to parent: %s\n", parent.c_str());
    
    // Invalidate cache for current storage when navigating
    if (current_storage == StorageSource::FLUIDNC_SD) {
        fluidnc_sd_cache.is_cached = false;
    } else if (current_storage == StorageSource::FLUIDNC_FLASH) {
        fluidnc_flash_cache.is_cached = false;
    } else if (current_storage == StorageSource::DISPLAY_SD) {
        display_sd_cache.is_cached = false;
    }
    
    // Use appropriate function based on storage type
    if (current_storage == StorageSource::DISPLAY_SD) {
        listDisplaySDFiles(parent);
    } else {
        refreshFileList(parent);
    }
}

std::string UITabFiles::getParentPath(const std::string &path) {
    // Remove trailing slash if present
    std::string p = path;
    if (p.length() > 1 && p[p.length() - 1] == '/') {
        p = p.substr(0, p.length() - 1);
    }
    
    // Find last slash
    size_t lastSlash = p.find_last_of('/');
    if (lastSlash == std::string::npos || lastSlash == 0) {
        // Return appropriate root based on current storage
        if (current_storage == StorageSource::FLUIDNC_FLASH) {
            return "/localfs/";
        } else if (current_storage == StorageSource::DISPLAY_SD) {
            return "/";
        } else {
            return "/sd/";
        }
    }
    
    // Return path up to (but not including) the last slash to avoid double slashes
    std::string parent = p.substr(0, lastSlash);
    
    // Remove trailing slash from parent if present (except for root)
    if (parent.length() > 1 && parent[parent.length() - 1] == '/') {
        parent = parent.substr(0, parent.length() - 1);
    }
    
    return parent;
}

static void directory_button_event_cb(lv_event_t *e) {
    const char *dirname = (const char*)lv_event_get_user_data(e);
    if (dirname) {
        Serial.printf("[Files] Opening directory: %s\n", dirname);
        
        // Invalidate cache for current storage when navigating
        if (UITabFiles::current_storage == StorageSource::FLUIDNC_SD) {
            UITabFiles::fluidnc_sd_cache.is_cached = false;
        } else if (UITabFiles::current_storage == StorageSource::FLUIDNC_FLASH) {
            UITabFiles::fluidnc_flash_cache.is_cached = false;
        } else if (UITabFiles::current_storage == StorageSource::DISPLAY_SD) {
            UITabFiles::display_sd_cache.is_cached = false;
        }
        
        // Handle Display SD navigation differently
        if (UITabFiles::current_storage == StorageSource::DISPLAY_SD) {
            UITabFiles::listDisplaySDFiles(dirname);
        } else {
            UITabFiles::refreshFileList(dirname);
        }
    }
}

static void play_button_event_cb(lv_event_t *e) {
    const char *filename = (const char*)lv_event_get_user_data(e);
    if (filename) {
        Serial.printf("[Files] Play file: %s\n", filename);
        
        // Determine command prefix based on file path
        const char *cmd_prefix;
        if (strncmp(filename, "/localfs/", 9) == 0) {
            cmd_prefix = "$LocalFS/Run=";
        } else {
            cmd_prefix = "$SD/Run=";
        }
        
        // Send run command to FluidNC
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s%s\n", cmd_prefix, filename);
        FluidNCClient::sendCommand(cmd);
        
        // Switch to Status tab (index 0) to monitor progress
        lv_obj_t *tabview = UITabs::getTabview();
        if (tabview) {
            lv_tabview_set_active(tabview, 0, LV_ANIM_OFF);
        }
    }
}

static void delete_confirm_event_cb(lv_event_t *e) {
    const char *filename = (const char*)lv_event_get_user_data(e);
    
    if (filename) {
        Serial.printf("[Files] Deleting file: %s\n", filename);
        
        // Determine command prefix based on file path
        const char *cmd_prefix;
        if (strncmp(filename, "/localfs/", 9) == 0) {
            cmd_prefix = "$LocalFS/Delete=";
        } else {
            cmd_prefix = "$SD/Delete=";
        }
        
        // Send delete command to FluidNC
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s%s\n", cmd_prefix, filename);
        FluidNCClient::sendCommand(cmd);
        
        // Request a refresh after a short delay (500ms)
        // The refresh will be handled by checkPendingRefresh() in the main loop
        lv_timer_create([](lv_timer_t *timer) {
            UITabFiles::requestRefresh();
            lv_timer_delete(timer);
        }, 500, nullptr);
    }
    
    // Close the dialog
    lv_obj_t *dialog = (lv_obj_t*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
    if (dialog) {
        lv_obj_del(dialog);
    }
}

static void delete_cancel_event_cb(lv_event_t *e) {
    Serial.println("[Files] Delete cancelled");
    
    // Close the dialog
    lv_obj_t *dialog = (lv_obj_t*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
    if (dialog) {
        lv_obj_del(dialog);
    }
}

static void delete_button_event_cb(lv_event_t *e) {
    const char *filename = (const char*)lv_event_get_user_data(e);
    if (filename) {
        Serial.printf("[Files] Delete requested for: %s\n", filename);
        
        // Create modal background
        lv_obj_t *dialog = lv_obj_create(lv_scr_act());
        lv_obj_set_size(dialog, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(dialog, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_bg_opa(dialog, LV_OPA_70, 0);
        lv_obj_set_style_border_width(dialog, 0, 0);
        lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
        
        // Dialog content box
        lv_obj_t *content = lv_obj_create(dialog);
        lv_obj_set_size(content, 500, 220);
        lv_obj_center(content);
        lv_obj_set_style_bg_color(content, UITheme::BG_MEDIUM, 0);
        lv_obj_set_style_border_color(content, UITheme::STATE_ALARM, 0);
        lv_obj_set_style_border_width(content, 3, 0);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(content, 20, 0);
        lv_obj_set_style_pad_gap(content, 15, 0);
        lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        
        // Warning icon and title
        lv_obj_t *title = lv_label_create(content);
        lv_label_set_text_fmt(title, "%s Delete File?", LV_SYMBOL_WARNING);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(title, UITheme::STATE_ALARM, 0);
        
        // File name
        lv_obj_t *name_label = lv_label_create(content);
        lv_label_set_text_fmt(name_label, "%s", filename);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(name_label, UITheme::TEXT_LIGHT, 0);
        lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name_label, 450);
        
        // Message
        lv_obj_t *msg_label = lv_label_create(content);
        lv_label_set_text(msg_label, "This action cannot be undone.");
        lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(msg_label, UITheme::UI_WARNING, 0);
        
        // Button container
        lv_obj_t *btn_container = lv_obj_create(content);
        lv_obj_set_size(btn_container, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_container, 0, 0);
        lv_obj_set_style_pad_all(btn_container, 0, 0);
        lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);
        
        // Store filename in dialog user data (use static storage)
        static char filename_storage[10][128];  // Support up to 10 concurrent dialogs
        static int storage_index = 0;
        strncpy(filename_storage[storage_index], filename, 127);
        filename_storage[storage_index][127] = '\0';
        
        // Cancel button
        lv_obj_t *cancel_btn = lv_btn_create(btn_container);
        lv_obj_set_size(cancel_btn, 180, 50);
        lv_obj_set_style_bg_color(cancel_btn, UITheme::BG_BUTTON, 0);
        lv_obj_set_user_data(cancel_btn, dialog);  // Store dialog reference
        lv_obj_add_event_cb(cancel_btn, delete_cancel_event_cb, LV_EVENT_CLICKED, nullptr);
        
        lv_obj_t *cancel_label = lv_label_create(cancel_btn);
        lv_label_set_text(cancel_label, "Cancel");
        lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_18, 0);
        lv_obj_center(cancel_label);
        
        // Delete button
        lv_obj_t *delete_btn = lv_btn_create(btn_container);
        lv_obj_set_size(delete_btn, 180, 50);
        lv_obj_set_style_bg_color(delete_btn, UITheme::STATE_ALARM, 0);
        lv_obj_set_user_data(delete_btn, dialog);  // Store dialog reference
        lv_obj_add_event_cb(delete_btn, delete_confirm_event_cb, LV_EVENT_CLICKED, filename_storage[storage_index]);
        
        lv_obj_t *delete_label = lv_label_create(delete_btn);
        lv_label_set_text(delete_label, LV_SYMBOL_TRASH " Delete");
        lv_obj_set_style_text_font(delete_label, &lv_font_montserrat_18, 0);
        lv_obj_center(delete_label);
        
        storage_index = (storage_index + 1) % 10;
    }
}

void UITabFiles::parseFileList(const std::string &response) {
    // Parse FluidNC $Files/ListGcode JSON response
    // Expected format: {"files":[{"name":"file.gcode","size":12345},...],"path":"/sd/"}
    file_names.clear();
    
    // Get current storage cache
    StorageCache* cache = getCurrentCache();
    cache->file_list.clear();
    
    Serial.printf("[Files] Parsing JSON file list, response length: %d\n", response.length());
    Serial.printf("[Files] JSON: %s\n", response.c_str());
    
    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
        Serial.printf("[Files] JSON parsing failed: %s\n", error.c_str());
        if (status_label) {
            lv_label_set_text(status_label, "Error parsing file list");
            lv_obj_set_style_text_color(status_label, UITheme::STATE_ALARM, 0);
        }
        return;
    }
    
    // Check for error field in response
    if (doc["error"].is<const char*>()) {
        const char* error_msg = doc["error"];
        Serial.printf("[Files] FluidNC error: %s\n", error_msg);
        if (status_label) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Error: %s", error_msg);
            lv_label_set_text(status_label, msg);
            lv_obj_set_style_text_color(status_label, UITheme::STATE_ALARM, 0);
        }
        updateFileListUI();
        return;
    }
    
    // Check if response has files array
    if (!doc["files"].is<JsonArray>()) {
        Serial.println("[Files] No 'files' array in JSON response");
        if (status_label) {
            lv_label_set_text(status_label, "No files found");
            lv_obj_set_style_text_color(status_label, UITheme::UI_INFO, 0);
        }
        updateFileListUI();
        return;
    }
    
    JsonArray files = doc["files"];
    
    // Extract file and directory information
    for (JsonObject file : files) {
        if (file["name"].is<const char*>()) {
            const char* filename = file["name"];
            int32_t filesize = 0;
            
            // Size can be either string or integer
            if (file["size"].is<const char*>()) {
                // Parse string to integer
                const char* size_str = file["size"];
                filesize = atoi(size_str);
            } else if (file["size"].is<int>()) {
                filesize = file["size"];
            } else {
                Serial.printf("[Files] Warning: Could not parse size for '%s'\n", filename);
                continue;
            }
            
            bool is_dir = (filesize == -1);
            
            file_names.push_back(filename);
            cache->file_list.push_back({filename, filesize, is_dir});
            Serial.printf("[Files] Added %s: '%s' (%d bytes)\n", 
                is_dir ? "directory" : "file", filename, filesize);
        }
    }
    
    // Load folders_on_top preference
    Preferences prefs;
    prefs.begin(PREFS_SYSTEM_NAMESPACE, true);  // Read-only
    bool folders_on_top = prefs.getBool("folders_on_top", false);  // Default to false (folders at bottom)
    prefs.end();
    
    // Sort based on user preference
    if (folders_on_top) {
        // Folders first (at top), then files, both alphabetically (case-insensitive)
        std::sort(cache->file_list.begin(), cache->file_list.end(), 
            [](const FileInfo &a, const FileInfo &b) {
                // Directories come before files (directories at top)
                if (a.is_directory != b.is_directory) {
                    return a.is_directory;  // true (directory) sorts before false (file)
                }
                // Within same type, sort alphabetically (case-insensitive)
                std::string a_lower = a.name;
                std::string b_lower = b.name;
                std::transform(a_lower.begin(), a_lower.end(), a_lower.begin(), ::tolower);
                std::transform(b_lower.begin(), b_lower.end(), b_lower.begin(), ::tolower);
                return a_lower < b_lower;
            });
    } else {
        // Files first, then directories at bottom, both alphabetically (case-insensitive)
        std::sort(cache->file_list.begin(), cache->file_list.end(), 
            [](const FileInfo &a, const FileInfo &b) {
                // Files come before directories (directories at bottom)
                if (a.is_directory != b.is_directory) {
                    return !a.is_directory;  // false (file) sorts before true (directory)
                }
                // Within same type, sort alphabetically (case-insensitive)
                std::string a_lower = a.name;
                std::string b_lower = b.name;
                std::transform(a_lower.begin(), a_lower.end(), a_lower.begin(), ::tolower);
                std::transform(b_lower.begin(), b_lower.end(), b_lower.begin(), ::tolower);
                return a_lower < b_lower;
            });
    }
    
    Serial.printf("[Files] Parsed %d files from JSON (folders_on_top=%d)\n", cache->file_list.size(), folders_on_top);
    
    // Mark cache as valid after successful parse
    cache->cached_path = current_path;
    cache->is_cached = true;
    
    updateFileListUI();
}

void UITabFiles::updateFileListUI() {
    if (!file_list_container) return;
    
    // Get current storage cache
    StorageCache* cache = getCurrentCache();
    
    // Clear existing file buttons
    lv_obj_clean(file_list_container);
    
    if (cache->file_list.empty()) {
        lv_obj_t *empty_label = lv_label_create(file_list_container);
        lv_label_set_text(empty_label, "No files found");
        lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(empty_label, UITheme::TEXT_MEDIUM, 0);
        
        if (status_label) {
            lv_label_set_text(status_label, "No files on SD card");
            lv_obj_set_style_text_color(status_label, UITheme::UI_WARNING, 0);
        }
        return;
    }
    
    // Static storage for filenames/paths (persistent across callbacks)
    static char filenames_storage[100][256];
    size_t max_files = std::min(cache->file_list.size(), (size_t)100);
    
    // Create file/directory entries
    for (size_t i = 0; i < max_files; i++) {
        const FileInfo &file = cache->file_list[i];
        
        // Build full path for the entry
        std::string full_path = current_path;
        if (full_path[full_path.length() - 1] != '/') {
            full_path += "/";
        }
        full_path += file.name;
        // Note: Don't add trailing slash for directories - FluidNC doesn't like it
        
        // Store full path in persistent storage
        strncpy(filenames_storage[i], full_path.c_str(), 255);
        filenames_storage[i][255] = '\0';
        
        // File/directory row container
        lv_obj_t *file_row = lv_obj_create(file_list_container);
        lv_obj_set_size(file_row, 750, 46);
        lv_obj_set_style_bg_color(file_row, file.is_directory ? UITheme::BG_BUTTON : UITheme::BG_DARKER, 0);
        lv_obj_set_style_border_width(file_row, 1, 0);
        lv_obj_set_style_border_color(file_row, UITheme::BORDER_MEDIUM, 0);
        lv_obj_set_style_pad_all(file_row, 5, 0);
        lv_obj_set_style_radius(file_row, 3, 0);
        lv_obj_clear_flag(file_row, LV_OBJ_FLAG_SCROLLABLE);
        
        if (file.is_directory) {
            // Make directory row clickable
            lv_obj_add_flag(file_row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(file_row, directory_button_event_cb, LV_EVENT_CLICKED, filenames_storage[i]);
        } else {
            lv_obj_clear_flag(file_row, LV_OBJ_FLAG_CLICKABLE);
        }
        
        // Icon + Filename label (left side)
        lv_obj_t *lbl_filename = lv_label_create(file_row);
        if (file.is_directory) {
            char label_text[256];
            snprintf(label_text, sizeof(label_text), LV_SYMBOL_DIRECTORY " %s", file.name.c_str());
            lv_label_set_text(lbl_filename, label_text);
            lv_obj_set_style_text_color(lbl_filename, UITheme::ACCENT_SECONDARY, 0);
        } else {
            lv_label_set_text(lbl_filename, file.name.c_str());
            lv_obj_set_style_text_color(lbl_filename, lv_color_white(), 0);
        }
        lv_obj_set_style_text_font(lbl_filename, &lv_font_montserrat_20, 0);
        lv_obj_align(lbl_filename, LV_ALIGN_LEFT_MID, 5, 0);
        lv_label_set_long_mode(lbl_filename, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_filename, file.is_directory ? 720 : 400);
        
        // Only show size and buttons for files, not directories
        if (!file.is_directory) {
            // File size label (center)
            lv_obj_t *lbl_size = lv_label_create(file_row);
            char size_str[32];
            if (file.size >= 1024 * 1024) {
                snprintf(size_str, sizeof(size_str), "%.2f MB", file.size / (1024.0f * 1024.0f));
            } else if (file.size >= 1024) {
                snprintf(size_str, sizeof(size_str), "%.1f KB", file.size / 1024.0f);
            } else {
                snprintf(size_str, sizeof(size_str), "%d B", file.size);
            }
            lv_label_set_text(lbl_size, size_str);
            lv_obj_set_style_text_font(lbl_size, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(lbl_size, UITheme::TEXT_MEDIUM, 0);
            lv_obj_align(lbl_size, LV_ALIGN_LEFT_MID, 420, 0);
            
            // Show upload button for Display SD, or play/delete for FluidNC storage.
            // No upload over ESP-NOW: FluidNC can't receive XModem on that
            // channel, and there's no WiFi for an HTTP upload.
            if (current_storage == StorageSource::DISPLAY_SD) {
                if (!FluidNCClient::isEspNowMode()) {
                    // Upload button (for Display SD files)
                    lv_obj_t *btn_upload = lv_button_create(file_row);
                    lv_obj_set_size(btn_upload, 120, 38);
                    lv_obj_align(btn_upload, LV_ALIGN_RIGHT_MID, -5, 0);
                    lv_obj_set_style_bg_color(btn_upload, UITheme::ACCENT_PRIMARY, 0);
                    lv_obj_set_style_radius(btn_upload, 3, 0);
                    lv_obj_add_event_cb(btn_upload, upload_button_event_cb, LV_EVENT_CLICKED, filenames_storage[i]);
                
                    lv_obj_t *lbl_upload = lv_label_create(btn_upload);
                    lv_label_set_text(lbl_upload, LV_SYMBOL_UPLOAD " Upload");
                    lv_obj_set_style_text_font(lbl_upload, &lv_font_montserrat_18, 0);
                    lv_obj_center(lbl_upload);
                }
            } else {
                // Delete button (for FluidNC files)
                lv_obj_t *btn_delete = lv_button_create(file_row);
                lv_obj_set_size(btn_delete, 70, 38);
                lv_obj_align(btn_delete, LV_ALIGN_RIGHT_MID, -80, 0);
                lv_obj_set_style_bg_color(btn_delete, UITheme::BTN_ESTOP, 0);
                lv_obj_set_style_radius(btn_delete, 3, 0);
                lv_obj_add_event_cb(btn_delete, delete_button_event_cb, LV_EVENT_CLICKED, filenames_storage[i]);
                
                lv_obj_t *lbl_delete = lv_label_create(btn_delete);
                lv_label_set_text(lbl_delete, LV_SYMBOL_TRASH);
                lv_obj_set_style_text_font(lbl_delete, &lv_font_montserrat_18, 0);
                lv_obj_center(lbl_delete);
                
                // Play button (for FluidNC files)
                lv_obj_t *btn_play = lv_button_create(file_row);
                lv_obj_set_size(btn_play, 70, 38);
                lv_obj_align(btn_play, LV_ALIGN_RIGHT_MID, -5, 0);
                lv_obj_set_style_bg_color(btn_play, UITheme::BTN_PLAY, 0);
                lv_obj_set_style_radius(btn_play, 3, 0);
                lv_obj_add_event_cb(btn_play, play_button_event_cb, LV_EVENT_CLICKED, filenames_storage[i]);
                
                lv_obj_t *lbl_play = lv_label_create(btn_play);
                lv_label_set_text(lbl_play, LV_SYMBOL_PLAY);
                lv_obj_set_style_text_font(lbl_play, &lv_font_montserrat_18, 0);
                lv_obj_center(lbl_play);
            }
        }  // End of if (!file.is_directory)
    }  // End of for loop
    
    if (status_label) {
        char buf[64];
        int file_count = 0;
        int dir_count = 0;
        for (const auto &f : cache->file_list) {
            if (f.is_directory) dir_count++;
            else file_count++;
        }
        if (dir_count > 0 && file_count > 0) {
            snprintf(buf, sizeof(buf), "%d folder(s), %d file(s)", dir_count, file_count);
        } else if (dir_count > 0) {
            snprintf(buf, sizeof(buf), "%d folder(s)", dir_count);
        } else {
            snprintf(buf, sizeof(buf), "%d file(s)", file_count);
        }
        lv_label_set_text(status_label, buf);
        lv_obj_set_style_text_color(status_label, UITheme::UI_SUCCESS, 0);
    }
}

// List files from Display SD card
void UITabFiles::listDisplaySDFiles(const std::string &path) {
    Serial.printf("[Files] Listing Display SD: %s\n", path.c_str());
    
    // Check if SD card is available
    if (!isDisplaySDAvailable()) {
        Serial.println("[Files] SD card not available");
        display_sd_cache.is_cached = false;
        display_sd_cache.file_list.clear();
        if (status_label) {
            lv_label_set_text(status_label, "SD card not available");
            lv_obj_set_style_text_color(status_label, UITheme::UI_WARNING, 0);
        }
        if (path_label) {
            lv_label_set_text(path_label, "/");
        }
        // Clear file list UI
        if (file_list_container) {
            lv_obj_clean(file_list_container);
        }
        return;
    }
    
    // Update current path
    current_path = path;
    
    // Get Display SD cache
    display_sd_cache.file_list.clear();
    
    File root = SD.open(path.c_str());
    if (!root || !root.isDirectory()) {
        Serial.println("[Files] Failed to open Display SD directory (card may have been removed)");
        display_sd_cache.is_cached = false;
        display_sd_cache.file_list.clear();
        if (status_label) {
            lv_label_set_text(status_label, "SD card not available");
            lv_obj_set_style_text_color(status_label, UITheme::UI_WARNING, 0);
        }
        if (path_label) {
            lv_label_set_text(path_label, "/");
        }
        // Clear file list UI
        if (file_list_container) {
            lv_obj_clean(file_list_container);
        }
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        FileInfo info;
        info.name = file.name();
        
        // Remove leading path from name
        size_t lastSlash = info.name.find_last_of('/');
        if (lastSlash != std::string::npos) {
            info.name = info.name.substr(lastSlash + 1);
        }
        
        // Skip "System Volume Information" folder
        if (info.name == "System Volume Information") {
            file = root.openNextFile();
            continue;
        }
        
        info.is_directory = file.isDirectory();
        info.size = info.is_directory ? -1 : file.size();
        
        display_sd_cache.file_list.push_back(info);
        file = root.openNextFile();
    }
    
    root.close();
    
    // Sort files (folders on top if preference is set)
    Preferences prefs;
    prefs.begin(PREFS_SYSTEM_NAMESPACE, true);
    bool folders_on_top = prefs.getBool("folders_on_top", false);
    prefs.end();
    
    if (folders_on_top) {
        std::sort(display_sd_cache.file_list.begin(), display_sd_cache.file_list.end(),
                 [](const FileInfo &a, const FileInfo &b) {
                     if (a.is_directory != b.is_directory) {
                         return a.is_directory;  // Folders first
                     }
                     return a.name < b.name;
                 });
    } else {
        std::sort(display_sd_cache.file_list.begin(), display_sd_cache.file_list.end(),
                 [](const FileInfo &a, const FileInfo &b) {
                     return a.name < b.name;
                 });
    }
    
    Serial.printf("[Files] Found %d items on Display SD\n", display_sd_cache.file_list.size());
    
    // Mark cache as valid after successful list
    display_sd_cache.cached_path = current_path;
    display_sd_cache.is_cached = true;
    
    // Update path label
    if (path_label) {
        lv_label_set_text(path_label, current_path.c_str());
    }
    
    // Update UI
    updateFileListUI();
}

// Upload button event callback
void UITabFiles::upload_button_event_cb(lv_event_t *e) {
    const char* fullPath = (const char*)lv_event_get_user_data(e);

    // Check if SD card is still available
    if (!isDisplaySDAvailable()) {
        Serial.println("[Files] SD card not available for upload");
        // Create error dialog
        lv_obj_t *dialog = lv_obj_create(lv_scr_act());
        lv_obj_set_size(dialog, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(dialog, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_bg_opa(dialog, LV_OPA_70, 0);
        lv_obj_set_style_border_width(dialog, 0, 0);
        lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *content = lv_obj_create(dialog);
        lv_obj_set_size(content, 500, 200);
        lv_obj_center(content);
        lv_obj_set_style_bg_color(content, UITheme::BG_MEDIUM, 0);
        lv_obj_set_style_border_color(content, UITheme::UI_WARNING, 0);
        lv_obj_set_style_border_width(content, 3, 0);

        lv_obj_t *label = lv_label_create(content);
        lv_label_set_text(label, "SD card not available.\nPlease insert SD card.");
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);

        lv_obj_t *btn = lv_btn_create(content);
        lv_obj_set_size(btn, 120, 45);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_delete((lv_obj_t*)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, dialog);

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, "OK");
        lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_18, 0);
        lv_obj_center(btn_label);

        return;
    }

    // Extract just the filename from the full path for display
    std::string pathStr(fullPath);
    size_t lastSlash = pathStr.find_last_of('/');
    const char* filename = (lastSlash != std::string::npos) ? fullPath + lastSlash + 1 : fullPath;

    // Get file size
    File file = SD.open(fullPath);
    if (!file) {
        Serial.printf("[Files] Failed to open file: %s\n", fullPath);
        return;
    }

    size_t fileSize = file.size();
    file.close();

    Serial.printf("[Files] File size: %zu bytes (%.2f MB)\n", fileSize, fileSize / (1024.0f * 1024.0f));

    showUploadDialog(filename, fullPath, fileSize);
}

// ============================================================================
// Upload destination - shared by HTTP (WiFi) and XModem (UART / USB CDC)
// ============================================================================

// Remembered until restart so repeated uploads go to the same place
static bool upload_to_flash = false;
static String upload_folder = FLUIDNC_UPLOAD_PATH;

// Parameters of the upload being confirmed or in progress (must outlive dialogs)
static char upload_local_path[256] = {};
static char upload_display_name[128] = {};
static char xmodem_remote_path[256] = {};

static lv_obj_t *upload_dest_dd = nullptr;
static lv_obj_t *upload_folder_label = nullptr;
static lv_obj_t *upload_path_label = nullptr;

// "120 KB / 480 KB (25%)  |  14.2 KB/s" (the rate once there's enough to measure)
static void formatTransferProgress(char *buf, size_t len, size_t sent, size_t total,
                                   size_t rateBytes, uint32_t rateMs) {
    int pct = total > 0 ? (int)((sent * 100) / total) : 0;
    int n;
    if (total >= 1048576) {
        n = snprintf(buf, len, "%.2f MB / %.2f MB (%d%%)", sent / 1048576.0f, total / 1048576.0f, pct);
    } else if (total >= 1024) {
        n = snprintf(buf, len, "%u KB / %u KB (%d%%)", (unsigned)(sent / 1024), (unsigned)(total / 1024), pct);
    } else {
        n = snprintf(buf, len, "%u B / %u B (%d%%)", (unsigned)sent, (unsigned)total, pct);
    }
    if (rateMs >= 500 && n > 0 && (size_t)n < len) {
        snprintf(buf + n, len - n, "  |  %.1f KB/s", rateBytes / 1024.0f / (rateMs / 1000.0f));
    }
}

// Rate measurement: from the first progress report, so connecting and the
// XModem handshake don't count
static uint32_t transfer_start_ms = 0;
static size_t transfer_start_bytes = 0;

// "/a//b" -> "/a/b/", "" -> "/"
static String normalizeFolder(String folder) {
    folder.trim();
    while (folder.indexOf("//") >= 0) {
        folder.replace("//", "/");
    }
    if (!folder.startsWith("/")) folder = "/" + folder;
    if (!folder.endsWith("/")) folder += "/";
    return folder;
}

// FluidNC path of a folder on the chosen volume, without a trailing slash
// ("/sd/fluidtouch/uploads", or "/sd" for the root)
static String remoteDir(bool toFlash, const String& folder) {
    String dir = String(toFlash ? "/localfs" : "/sd") + folder;
    while (dir.endsWith("/")) dir.remove(dir.length() - 1);
    return dir;
}

// "/a/b/" -> "/a/", "/" -> "/"
static String parentFolder(const String& folder) {
    if (folder == "/") return folder;
    String f = folder.substring(0, folder.length() - 1);
    return f.substring(0, f.lastIndexOf('/') + 1);
}

static void updateUploadDestLabels() {
    if (upload_folder_label) {
        lv_label_set_text(upload_folder_label, upload_folder.c_str());
    }
    if (upload_path_label) {
        String path = remoteDir(upload_to_flash, upload_folder) + "/" + upload_display_name;
        lv_label_set_text_fmt(upload_path_label, "Path: %s", path.c_str());
    }
}

void UITabFiles::closeUploadDialog() {
    if (upload_dialog) {
        lv_obj_delete(upload_dialog);
        upload_dialog = nullptr;
    }
    upload_dest_dd = upload_folder_label = upload_path_label = nullptr;
}

// ----------------------------------------------------------------------------
// $Files/ListGcode request outside the Files tab's own list, for the folder
// browser and the pre-upload folder check. Works over every connection type.
// ----------------------------------------------------------------------------

static void (*remote_list_cb)(bool responded, const String &json) = nullptr;
static bool remote_list_pending = false;
static bool remote_list_collecting = false;
static String remote_list_json;
static lv_timer_t *remote_list_timer = nullptr;

// responded is false for a timeout or an "error:" reply (e.g. an older FluidNC)
void UITabFiles::finishRemoteList(bool responded) {
    if (!remote_list_pending) return;
    remote_list_pending = false;
    FluidNCClient::clearMessageCallback();
    if (remote_list_timer) {
        lv_timer_delete(remote_list_timer);
        remote_list_timer = nullptr;
    }
    auto cb = remote_list_cb;
    remote_list_cb = nullptr;
    if (cb) cb(responded, remote_list_json);
}

void UITabFiles::requestRemoteList(const String &dir, void (*cb)(bool responded, const String &json)) {
    if (remote_list_pending) finishRemoteList(false);
    remote_list_cb = cb;
    remote_list_json = "";
    remote_list_collecting = false;
    remote_list_pending = true;

    // Same reassembly as the Files tab: FluidNC v4 sends the JSON in chunks
    // wrapped in [JSON:...], with "ok" sometimes appended to the last chunk
    FluidNCClient::setMessageCallback([](const char* message) {
        String msg(message);
        while (msg.endsWith("\r") || msg.endsWith("\n")) msg.remove(msg.length() - 1);
        if (msg.startsWith("<") || msg.startsWith("[GC:") || msg.startsWith("[MSG:") ||
            msg == "PING" || msg.startsWith("PING:")) {
            return;
        }
        if (msg.startsWith("error:")) {
            finishRemoteList(false);
            return;
        }
        bool done = false;
        String last = msg.substring(msg.lastIndexOf('\n') + 1);
        last.trim();
        if (last.equalsIgnoreCase("ok")) {
            done = true;
            int nl = msg.lastIndexOf('\n');
            msg = nl >= 0 ? msg.substring(0, nl) : "";
        }
        if (msg.length() > 0) {
            if (msg.startsWith("[JSON:") || msg.startsWith("{")) remote_list_collecting = true;
            if (remote_list_collecting) {
                if (msg.startsWith("[JSON:")) {
                    msg.remove(0, 6);
                    if (msg.endsWith("]")) msg.remove(msg.length() - 1);
                }
                remote_list_json += msg;
            }
        }
        if (done && remote_list_collecting) finishRemoteList(true);
    });
    remote_list_timer = lv_timer_create([](lv_timer_t *t) {
        remote_list_timer = nullptr;  // Deleted by LVGL after this single run
        Serial.println("[Files] Folder listing timed out");
        finishRemoteList(false);
    }, 5000, nullptr);
    lv_timer_set_repeat_count(remote_list_timer, 1);

    Serial.printf("[Files] Listing %s\n", dir.c_str());
    String cmd = "$Files/ListGcode=" + dir + "\n";
    FluidNCClient::sendCommand(cmd.c_str());
}

// ----------------------------------------------------------------------------
// Folder browser: pick an existing folder, laid out like the Files tab
// ----------------------------------------------------------------------------

static lv_obj_t *browser_dialog = nullptr;
static lv_obj_t *browser_path_label = nullptr;
static lv_obj_t *browser_status_label = nullptr;
static lv_obj_t *browser_list = nullptr;
static String browser_folder = "/";
static bool browser_fallback_to_root = false;
static char browser_dir_names[100][128];

void UITabFiles::browserNavigate(const String &folder) {
    browser_folder = normalizeFolder(folder);
    if (browser_path_label) {
        lv_label_set_text(browser_path_label, (remoteDir(upload_to_flash, browser_folder) + "/").c_str());
    }
    if (browser_status_label) {
        lv_label_set_text(browser_status_label, "Loading...");
        lv_obj_set_style_text_color(browser_status_label, UITheme::UI_INFO, 0);
    }
    if (browser_list) lv_obj_clean(browser_list);

    requestRemoteList(remoteDir(upload_to_flash, browser_folder), [](bool responded, const String &json) {
        if (!browser_dialog || !browser_list) return;  // Closed while loading

        JsonDocument doc;
        bool parsed = responded && !deserializeJson(doc, json);
        if (!parsed || doc["error"].is<const char*>()) {
            // The starting folder (e.g. the default upload folder) may not
            // exist yet: show the root instead
            if (browser_fallback_to_root && browser_folder != "/") {
                browser_fallback_to_root = false;
                browserNavigate("/");
                return;
            }
            lv_label_set_text(browser_status_label, parsed ? doc["error"].as<const char*>() : "No response from FluidNC");
            lv_obj_set_style_text_color(browser_status_label, UITheme::STATE_ALARM, 0);
            return;
        }
        browser_fallback_to_root = false;

        int folders = 0, files = 0;
        JsonArray entries = doc["files"].as<JsonArray>();
        // Folders first, tappable; then files, dimmed, so you can see what's there
        for (int pass = 0; pass < 2; pass++) {
            for (JsonObject entry : entries) {
                String name = entry["name"].as<String>();
                bool is_dir = entry["size"].as<String>() == "-1";
                if (is_dir != (pass == 0)) continue;

                lv_obj_t *row = lv_obj_create(browser_list);
                lv_obj_set_size(row, 750, 46);
                lv_obj_set_style_bg_color(row, is_dir ? UITheme::BG_BUTTON : UITheme::BG_DARKER, 0);
                lv_obj_set_style_border_width(row, 1, 0);
                lv_obj_set_style_border_color(row, UITheme::BORDER_MEDIUM, 0);
                lv_obj_set_style_pad_all(row, 5, 0);
                lv_obj_set_style_radius(row, 3, 0);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                lv_obj_t *lbl = lv_label_create(row);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
                lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 5, 0);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
                lv_obj_set_width(lbl, 720);

                if (is_dir && folders < 100) {
                    strncpy(browser_dir_names[folders], name.c_str(), sizeof(browser_dir_names[0]) - 1);
                    browser_dir_names[folders][sizeof(browser_dir_names[0]) - 1] = '\0';
                    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_add_event_cb(row, [](lv_event_t *e) {
                        const char *dir_name = (const char*)lv_event_get_user_data(e);
                        browserNavigate(browser_folder + dir_name + "/");
                    }, LV_EVENT_CLICKED, browser_dir_names[folders]);
                    lv_label_set_text_fmt(lbl, LV_SYMBOL_DIRECTORY " %s", name.c_str());
                    lv_obj_set_style_text_color(lbl, UITheme::ACCENT_SECONDARY, 0);
                    folders++;
                } else {
                    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
                    lv_label_set_text(lbl, name.c_str());
                    lv_obj_set_style_text_color(lbl, UITheme::TEXT_DISABLED, 0);
                    files++;
                }
            }
        }
        lv_label_set_text_fmt(browser_status_label, "%d folder(s), %d file(s)", folders, files);
        lv_obj_set_style_text_color(browser_status_label, UITheme::UI_SUCCESS, 0);
    });
}

void UITabFiles::showFolderBrowser() {
    // Full-screen, laid out like the Files tab
    browser_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(browser_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(browser_dialog, UITheme::BG_MEDIUM, 0);
    lv_obj_set_style_bg_opa(browser_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(browser_dialog, 0, 0);
    lv_obj_set_style_radius(browser_dialog, 0, 0);
    lv_obj_set_style_pad_all(browser_dialog, 10, 0);
    lv_obj_clear_flag(browser_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(browser_dialog);
    lv_label_set_text_fmt(title, LV_SYMBOL_DIRECTORY " Choose a folder on %s",
                          upload_to_flash ? "FluidNC Flash" : "FluidNC SD Card");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_set_pos(title, 5, 0);

    // Up button (navigate to parent folder)
    lv_obj_t *btn_up = lv_button_create(browser_dialog);
    lv_obj_set_size(btn_up, 100, 45);
    lv_obj_set_pos(btn_up, 5, 35);
    lv_obj_set_style_bg_color(btn_up, UITheme::BG_BUTTON, 0);
    lv_obj_add_event_cb(btn_up, [](lv_event_t *e) {
        browserNavigate(parentFolder(browser_folder));
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_up = lv_label_create(btn_up);
    lv_label_set_text(lbl_up, LV_SYMBOL_UP " Up");
    lv_obj_set_style_text_font(lbl_up, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_up);

    // Current folder and status
    browser_path_label = lv_label_create(browser_dialog);
    lv_obj_set_style_text_font(browser_path_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(browser_path_label, UITheme::ACCENT_SECONDARY, 0);
    lv_obj_set_pos(browser_path_label, 125, 37);
    lv_label_set_long_mode(browser_path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(browser_path_label, 640);

    browser_status_label = lv_label_create(browser_dialog);
    lv_obj_set_style_text_font(browser_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(browser_status_label, 125, 60);

    // Folder list with scrolling
    browser_list = lv_obj_create(browser_dialog);
    lv_obj_set_size(browser_list, 770, 300);
    lv_obj_set_pos(browser_list, 5, 90);
    lv_obj_set_style_bg_color(browser_list, UITheme::BG_DARKER, LV_PART_MAIN);
    lv_obj_set_style_border_color(browser_list, UITheme::BORDER_LIGHT, LV_PART_MAIN);
    lv_obj_set_style_border_width(browser_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(browser_list, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(browser_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(browser_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(browser_list, 6, 0);
    lv_obj_set_scroll_dir(browser_list, LV_DIR_VER);

    // Select / Cancel
    lv_obj_t *btn_select = lv_button_create(browser_dialog);
    lv_obj_set_size(btn_select, 260, 50);
    lv_obj_set_pos(btn_select, 140, 400);
    lv_obj_set_style_bg_color(btn_select, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_add_event_cb(btn_select, [](lv_event_t *e) {
        upload_folder = browser_folder;
        updateUploadDestLabels();
        closeFolderBrowser();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_select = lv_label_create(btn_select);
    lv_label_set_text(lbl_select, LV_SYMBOL_OK " Select This Folder");
    lv_obj_set_style_text_font(lbl_select, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_select);

    lv_obj_t *btn_cancel = lv_button_create(browser_dialog);
    lv_obj_set_size(btn_cancel, 180, 50);
    lv_obj_set_pos(btn_cancel, 440, 400);
    lv_obj_set_style_bg_color(btn_cancel, UITheme::BG_BUTTON, 0);
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t *e) {
        closeFolderBrowser();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_cancel);

    // Start in the current upload folder (the root if it doesn't exist yet)
    browser_fallback_to_root = true;
    browserNavigate(upload_folder);
}

void UITabFiles::closeFolderBrowser() {
    if (remote_list_pending) finishRemoteList(false);
    if (browser_dialog) {
        lv_obj_delete(browser_dialog);
        browser_dialog = nullptr;
    }
    browser_path_label = browser_status_label = browser_list = nullptr;
}

// ----------------------------------------------------------------------------
// Upload: only to an existing folder, except the default upload folder, which
// an HTTP upload may create. FluidNC can't create folders over a serial link,
// and $Xmodem/Receive cancels the transfer if the folder is missing.
// ----------------------------------------------------------------------------

enum class FolderCheck { Exists, Missing, Unknown };

void UITabFiles::startUpload() {
    if (FluidNCClient::isSerialMode()) {
        if (!FluidNCClient::startXModemUpload(upload_local_path, xmodem_remote_path, upload_display_name)) {
            closeXModemProgress(false, "Failed to start transfer task");
        }
    } else {
        UploadManager::uploadFile(upload_local_path, upload_display_name, upload_to_flash,
                                  upload_folder.c_str(), updateUploadProgress, closeUploadProgress);
    }
}

void UITabFiles::finishFolderCheck(int check) {
    FolderCheck result = (FolderCheck)check;
    bool serial = FluidNCClient::isSerialMode();
    bool is_default = upload_folder == normalizeFolder(FLUIDNC_UPLOAD_PATH);
    if (result == FolderCheck::Missing && (serial || !is_default)) {
        String dir = remoteDir(upload_to_flash, upload_folder);
        char msg[256];
        snprintf(msg, sizeof(msg), "Folder %s doesn't exist on FluidNC.\n%s", dir.c_str(),
                 serial ? "Create it from FluidNC's WebUI, or choose another folder."
                        : "Choose another folder.");
        if (serial) closeXModemProgress(false, msg);
        else closeUploadProgress(false, msg);
        return;
    }
    // Exists, the default folder (created by the HTTP upload), or the check
    // didn't answer (e.g. an older FluidNC): upload and let FluidNC report a failure
    startUpload();
}

void UITabFiles::checkFolderThenUpload() {
    requestRemoteList(remoteDir(upload_to_flash, upload_folder), [](bool responded, const String &json) {
        FolderCheck result = FolderCheck::Unknown;
        if (responded) {
            if (json.indexOf("\"error\"") >= 0) result = FolderCheck::Missing;
            else if (json.indexOf("\"files\"") >= 0) result = FolderCheck::Exists;
        }
        finishFolderCheck((int)result);
    });
}

// Show upload confirmation dialog: destination (SD / flash) and folder
void UITabFiles::showUploadDialog(const char* filename, const char* fullPath, size_t fileSize) {
    closeUploadDialog();

    strncpy(upload_local_path, fullPath, sizeof(upload_local_path) - 1);
    upload_local_path[sizeof(upload_local_path) - 1] = '\0';
    strncpy(upload_display_name, filename, sizeof(upload_display_name) - 1);
    upload_display_name[sizeof(upload_display_name) - 1] = '\0';

    // Create modal background
    upload_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(upload_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(upload_dialog, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(upload_dialog, LV_OPA_70, 0);
    lv_obj_set_style_border_width(upload_dialog, 0, 0);
    lv_obj_clear_flag(upload_dialog, LV_OBJ_FLAG_SCROLLABLE);

    // Dialog content box
    lv_obj_t *content = lv_obj_create(upload_dialog);
    lv_obj_set_size(content, 640, 340);
    lv_obj_center(content);
    lv_obj_set_style_bg_color(content, UITheme::BG_MEDIUM, 0);
    lv_obj_set_style_border_color(content, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_set_style_border_width(content, 3, 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, LV_SYMBOL_UPLOAD " Upload to FluidNC");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Filename and size
    char sizeText[48];
    if (fileSize >= 1048576) snprintf(sizeText, sizeof(sizeText), "%.2f MB", fileSize / 1048576.0f);
    else if (fileSize >= 1024) snprintf(sizeText, sizeof(sizeText), "%.1f KB", fileSize / 1024.0f);
    else snprintf(sizeText, sizeof(sizeText), "%u B", (unsigned)fileSize);
    lv_obj_t *lbl_filename = lv_label_create(content);
    lv_label_set_text_fmt(lbl_filename, "File: %s  (%s)", filename, sizeText);
    lv_obj_set_style_text_font(lbl_filename, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_filename, UITheme::TEXT_LIGHT, 0);
    lv_label_set_long_mode(lbl_filename, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_filename, 590);
    lv_obj_align(lbl_filename, LV_ALIGN_TOP_LEFT, 0, 42);

    // Destination: SD card or flash
    lv_obj_t *lbl_dest = lv_label_create(content);
    lv_label_set_text(lbl_dest, "Destination:");
    lv_obj_set_style_text_font(lbl_dest, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_dest, UITheme::TEXT_LIGHT, 0);
    lv_obj_align(lbl_dest, LV_ALIGN_TOP_LEFT, 0, 88);

    upload_dest_dd = lv_dropdown_create(content);
    lv_dropdown_set_options(upload_dest_dd, "FluidNC SD Card\nFluidNC Flash");
    lv_dropdown_set_selected(upload_dest_dd, upload_to_flash ? 1 : 0);
    lv_obj_set_size(upload_dest_dd, 300, 44);
    lv_obj_align(upload_dest_dd, LV_ALIGN_TOP_LEFT, 130, 80);
    lv_obj_set_style_text_font(upload_dest_dd, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(upload_dest_dd, UITheme::BG_BUTTON, 0);
    lv_obj_set_style_text_color(upload_dest_dd, lv_color_white(), 0);
    lv_obj_add_event_cb(upload_dest_dd, [](lv_event_t *e) {
        bool to_flash = lv_dropdown_get_selected(upload_dest_dd) == 1;
        if (to_flash != upload_to_flash) {
            // A folder on one volume needn't exist on the other: start from the default
            upload_to_flash = to_flash;
            upload_folder = FLUIDNC_UPLOAD_PATH;
        }
        updateUploadDestLabels();
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Folder on that volume, chosen with the folder browser
    lv_obj_t *lbl_folder = lv_label_create(content);
    lv_label_set_text(lbl_folder, "Folder:");
    lv_obj_set_style_text_font(lbl_folder, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_folder, UITheme::TEXT_LIGHT, 0);
    lv_obj_align(lbl_folder, LV_ALIGN_TOP_LEFT, 0, 146);

    upload_folder_label = lv_label_create(content);
    lv_obj_set_style_text_font(upload_folder_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(upload_folder_label, UITheme::ACCENT_SECONDARY, 0);
    lv_label_set_long_mode(upload_folder_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(upload_folder_label, 300);
    lv_obj_align(upload_folder_label, LV_ALIGN_TOP_LEFT, 130, 146);

    lv_obj_t *btn_browse = lv_btn_create(content);
    lv_obj_set_size(btn_browse, 150, 44);
    lv_obj_align(btn_browse, LV_ALIGN_TOP_LEFT, 440, 136);
    lv_obj_set_style_bg_color(btn_browse, UITheme::BG_BUTTON, 0);
    lv_obj_add_event_cb(btn_browse, [](lv_event_t *e) {
        showFolderBrowser();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_browse = lv_label_create(btn_browse);
    lv_label_set_text(lbl_browse, LV_SYMBOL_DIRECTORY " Change");
    lv_obj_set_style_text_font(lbl_browse, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_browse);

    // Full destination path
    upload_path_label = lv_label_create(content);
    lv_obj_set_style_text_font(upload_path_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(upload_path_label, UITheme::TEXT_DISABLED, 0);
    lv_label_set_long_mode(upload_path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(upload_path_label, 590);
    lv_obj_align(upload_path_label, LV_ALIGN_TOP_LEFT, 0, 196);
    updateUploadDestLabels();

    // Button container (positioned at bottom)
    lv_obj_t *btn_container = lv_obj_create(content);
    lv_obj_set_size(btn_container, 590, 60);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    lv_obj_set_style_pad_all(btn_container, 0, 0);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);

    // Upload button (left side)
    lv_obj_t *btn_upload = lv_btn_create(btn_container);
    lv_obj_set_size(btn_upload, 180, 50);
    lv_obj_set_style_bg_color(btn_upload, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_add_event_cb(btn_upload, [](lv_event_t *e) {
        Serial.printf("[UITabFiles] Upload %s to %s\n", upload_display_name,
                      (remoteDir(upload_to_flash, upload_folder) + "/").c_str());

        // Close confirmation dialog first
        closeUploadDialog();

        // Give LVGL time to process the deletion before creating new dialog
        lv_timer_create([](lv_timer_t *timer) {
            lv_timer_delete(timer);
            if (FluidNCClient::isSerialMode()) {
                // UART / USB CDC: XModem
                snprintf(xmodem_remote_path, sizeof(xmodem_remote_path), "%s/%s",
                         remoteDir(upload_to_flash, upload_folder).c_str(), upload_display_name);
                showXModemProgress(upload_display_name);
            } else {
                // WiFi: HTTP upload
                showUploadProgress(upload_display_name);
            }
            if (upload_folder == "/") {
                startUpload();  // The root always exists
            } else {
                lv_obj_t *label = FluidNCClient::isSerialMode() ? xmodem_progress_label : upload_progress_label;
                if (label) lv_label_set_text(label, "Checking folder...");
                checkFolderThenUpload();
            }
        }, 50, nullptr);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl_upload = lv_label_create(btn_upload);
    lv_label_set_text(lbl_upload, LV_SYMBOL_UPLOAD " Upload");
    lv_obj_set_style_text_font(lbl_upload, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_upload);

    // Cancel button (right side)
    lv_obj_t *btn_cancel = lv_btn_create(btn_container);
    lv_obj_set_size(btn_cancel, 180, 50);
    lv_obj_set_style_bg_color(btn_cancel, UITheme::BG_BUTTON, 0);
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t *e) {
        closeUploadDialog();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_cancel);
}

// Show upload progress dialog
void UITabFiles::showUploadProgress(const char* filename) {
    transfer_start_ms = 0;
    Serial.printf("[UITabFiles] showUploadProgress called for: %s\n", filename);
    
    if (upload_progress_dialog) {
        Serial.println("[UITabFiles] Deleting existing progress dialog");
        lv_obj_delete(upload_progress_dialog);
        upload_progress_dialog = nullptr;
        upload_progress_bar = nullptr;
        upload_progress_label = nullptr;
    }
    
    // Create modal background
    upload_progress_dialog = lv_obj_create(lv_scr_act());
    if (!upload_progress_dialog) {
        Serial.println("[UITabFiles] ERROR: Failed to create upload_progress_dialog!");
        return;
    }
    
    lv_obj_set_size(upload_progress_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(upload_progress_dialog, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(upload_progress_dialog, LV_OPA_70, 0);
    lv_obj_set_style_border_width(upload_progress_dialog, 0, 0);
    lv_obj_clear_flag(upload_progress_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(upload_progress_dialog);  // Ensure it's on top
    
    // Dialog content box
    lv_obj_t *content = lv_obj_create(upload_progress_dialog);
    lv_obj_set_size(content, 600, 320);
    lv_obj_center(content);
    lv_obj_set_style_bg_color(content, UITheme::BG_MEDIUM, 0);
    lv_obj_set_style_border_color(content, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_set_style_border_width(content, 3, 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    
    Serial.printf("[UITabFiles] Upload progress dialog created at %p\n", upload_progress_dialog);
    
    // Title
    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, LV_SYMBOL_UPLOAD " Uploading...");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    
    // Filename
    lv_obj_t *lbl_filename = lv_label_create(content);
    lv_label_set_text_fmt(lbl_filename, "File: %s", filename);
    lv_obj_set_style_text_font(lbl_filename, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_filename, UITheme::TEXT_LIGHT, 0);
    lv_label_set_long_mode(lbl_filename, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_filename, 550);
    lv_obj_align(lbl_filename, LV_ALIGN_TOP_LEFT, 0, 45);
    
    // Progress bar
    upload_progress_bar = lv_bar_create(content);
    lv_obj_set_size(upload_progress_bar, 560, 30);
    lv_obj_align(upload_progress_bar, LV_ALIGN_TOP_LEFT, 0, 85);
    lv_bar_set_value(upload_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(upload_progress_bar, UITheme::BG_DARKER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(upload_progress_bar, UITheme::ACCENT_PRIMARY, LV_PART_INDICATOR);
    
    Serial.printf("[UITabFiles] Progress bar created at %p\n", upload_progress_bar);
    
    // Progress label
    upload_progress_label = lv_label_create(content);
    lv_label_set_text(upload_progress_label, "0 KB / 0 KB (0%)");
    lv_obj_set_style_text_font(upload_progress_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(upload_progress_label, UITheme::TEXT_LIGHT, 0);
    lv_obj_align(upload_progress_label, LV_ALIGN_TOP_LEFT, 0, 130);
    
    Serial.printf("[UITabFiles] Progress label created at %p\n", upload_progress_label);
    
    // Info text at bottom (will be replaced with Close button on success)
    lv_obj_t *lbl_info = lv_label_create(content);
    lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_info, UITheme::TEXT_DISABLED, 0);
    lv_obj_align(lbl_info, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(lbl_info, "Reset device to cancel upload");
    
    // Force immediate screen refresh to show dialog before upload blocks
    Serial.println("[UITabFiles] Forcing screen refresh...");
    lv_refr_now(NULL);
    Serial.println("[UITabFiles] Screen refresh complete");
}

// Update upload progress callback
void UITabFiles::updateUploadProgress(size_t current, size_t total) {
    Serial.printf("[UITabFiles] updateUploadProgress called: %d / %d\n", current, total);
    
    if (!upload_progress_bar || !upload_progress_label) {
        Serial.println("[UITabFiles] WARNING: upload_progress_bar or upload_progress_label is null!");
        return;
    }
    
    int percent = total > 0 ? (int)((current * 100) / total) : 0;
    lv_bar_set_value(upload_progress_bar, percent, LV_ANIM_OFF);
    
    if (current == 0 || transfer_start_ms == 0) {
        transfer_start_ms = millis();
        transfer_start_bytes = current;
    }
    char prog_text[96];
    formatTransferProgress(prog_text, sizeof(prog_text), current, total,
                           current - transfer_start_bytes, millis() - transfer_start_ms);
    lv_label_set_text(upload_progress_label, prog_text);
    
    // Force screen refresh to show updated progress
    lv_refr_now(NULL);
}

// Close upload progress dialog
void UITabFiles::closeUploadProgress(bool success, const char* error) {
    Serial.printf("[UITabFiles] closeUploadProgress called: success=%d, error=%s\n", success, error ? error : "null");
    
    if (!upload_progress_dialog) {
        Serial.println("[UITabFiles] upload_progress_dialog is null, already closed");
        return;
    }
    
    if (success) {
        // Update title and show success
        lv_obj_t *content = lv_obj_get_child(upload_progress_dialog, 0);
        if (content) {
            lv_obj_t *title = lv_obj_get_child(content, 0);
            if (title) {
                lv_label_set_text(title, LV_SYMBOL_OK " UPLOAD COMPLETE");
                lv_obj_set_style_text_color(title, UITheme::UI_SUCCESS, 0);
            }
            
            // Remove the info text (it's the last child before we add the button)
            uint32_t child_count = lv_obj_get_child_count(content);
            if (child_count > 0) {
                lv_obj_t *last_child = lv_obj_get_child(content, child_count - 1);
                if (last_child) {
                    lv_obj_delete(last_child);
                }
            }
            
            // Add button container for two buttons side by side
            lv_obj_t *btn_container = lv_obj_create(content);
            lv_obj_set_size(btn_container, LV_PCT(100), 60);
            lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_border_width(btn_container, 0, 0);
            lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_all(btn_container, 0, 0);
            lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            
            // Go to Upload Folder button (LEFT)
            lv_obj_t *btn_goto = lv_btn_create(btn_container);
            lv_obj_set_size(btn_goto, 250, 50);
            lv_obj_set_style_bg_color(btn_goto, UITheme::ACCENT_PRIMARY, 0);
            lv_obj_add_event_cb(btn_goto, [](lv_event_t *e) {
                if (upload_progress_dialog) {
                    lv_obj_delete(upload_progress_dialog);
                    upload_progress_dialog = nullptr;
                    upload_progress_bar = nullptr;
                    upload_progress_label = nullptr;
                }
                navigateToUploadDirectory();
            }, LV_EVENT_CLICKED, nullptr);
            
            lv_obj_t *lbl_goto = lv_label_create(btn_goto);
            lv_label_set_text(lbl_goto, LV_SYMBOL_DIRECTORY " Open Folder");
            lv_obj_set_style_text_font(lbl_goto, &lv_font_montserrat_18, 0);
            lv_obj_center(lbl_goto);
            
            // Close button (RIGHT)
            lv_obj_t *btn_close = lv_btn_create(btn_container);
            lv_obj_set_size(btn_close, 180, 50);
            lv_obj_set_style_bg_color(btn_close, UITheme::BG_BUTTON, 0);
            lv_obj_set_style_border_color(btn_close, UITheme::BORDER_MEDIUM, 0);
            lv_obj_set_style_border_width(btn_close, 2, 0);
            lv_obj_add_event_cb(btn_close, [](lv_event_t *e) {
                if (upload_progress_dialog) {
                    lv_obj_delete(upload_progress_dialog);
                    upload_progress_dialog = nullptr;
                    upload_progress_bar = nullptr;
                    upload_progress_label = nullptr;
                }
            }, LV_EVENT_CLICKED, nullptr);
            
            lv_obj_t *lbl_close = lv_label_create(btn_close);
            lv_label_set_text(lbl_close, "Close");
            lv_obj_set_style_text_font(lbl_close, &lv_font_montserrat_18, 0);
            lv_obj_center(lbl_close);
        }
        
        // Force screen refresh to show the title change and button
        lv_refr_now(NULL);
    } else {
        // Show error or cancellation message
        lv_obj_t *content = lv_obj_get_child(upload_progress_dialog, 0);
        if (content) {
            lv_obj_t *title = lv_obj_get_child(content, 0);
            if (title) {
                if (error && strstr(error, "cancelled")) {
                    lv_label_set_text(title, LV_SYMBOL_CLOSE " UPLOAD CANCELLED");
                    lv_obj_set_style_text_color(title, UITheme::UI_WARNING, 0);
                } else {
                    lv_label_set_text_fmt(title, LV_SYMBOL_WARNING " UPLOAD FAILED");
                    lv_obj_set_style_text_color(title, UITheme::STATE_ALARM, 0);
                }
            }
            
            // Show the error in place of the progress text
            if (upload_progress_label && error && !strstr(error, "cancelled")) {
                lv_label_set_long_mode(upload_progress_label, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(upload_progress_label, 550);
                lv_label_set_text(upload_progress_label, error);
                lv_obj_set_style_text_color(upload_progress_label, UITheme::STATE_ALARM, 0);
            }
            
            // Replace the "reset to cancel" note (the last child) with a Close button
            uint32_t child_count = lv_obj_get_child_count(content);
            if (child_count > 0) {
                lv_obj_delete(lv_obj_get_child(content, child_count - 1));
            }
            lv_obj_t *btn_close = lv_btn_create(content);
            lv_obj_set_size(btn_close, 160, 50);
            lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_bg_color(btn_close, UITheme::BG_BUTTON, 0);
            lv_obj_add_event_cb(btn_close, [](lv_event_t *e) {
                if (upload_progress_dialog) {
                    lv_obj_delete(upload_progress_dialog);
                    upload_progress_dialog = nullptr;
                    upload_progress_bar = nullptr;
                    upload_progress_label = nullptr;
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *lbl_close = lv_label_create(btn_close);
            lv_label_set_text(lbl_close, "Close");
            lv_obj_set_style_text_font(lbl_close, &lv_font_montserrat_18, 0);
            lv_obj_center(lbl_close);
        }
        lv_refr_now(NULL);
    }
}

void UITabFiles::navigateToUploadDirectory() {
    Serial.println("[Files] Navigating to upload directory");

    // Switch to the storage the last upload went to
    current_storage = upload_to_flash ? StorageSource::FLUIDNC_FLASH : StorageSource::FLUIDNC_SD;

    // Update the dropdown to reflect the storage change
    if (storage_dropdown) {
        lv_dropdown_set_selected(storage_dropdown, static_cast<uint16_t>(current_storage));
    }

    // Navigate to the upload's folder ("/sd/fluidtouch/uploads", or "/sd" for the root)
    current_path = remoteDir(upload_to_flash, upload_folder).c_str();

    // Invalidate cache to force a fresh directory listing
    if (upload_to_flash) {
        fluidnc_flash_cache.is_cached = false;
    } else {
        fluidnc_sd_cache.is_cached = false;
    }
    
    // Refresh the file list
    refreshFileList(current_path);
}

// ============================================================================
// XModem (wired) upload dialog and progress
// ============================================================================

void UITabFiles::showXModemProgress(const char* filename) {
    transfer_start_ms = 0;
    if (xmodem_progress_dialog) {
        lv_obj_delete(xmodem_progress_dialog);
        xmodem_progress_dialog = nullptr;
        xmodem_progress_bar    = nullptr;
        xmodem_progress_label  = nullptr;
    }

    // Modal backdrop
    xmodem_progress_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(xmodem_progress_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(xmodem_progress_dialog, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(xmodem_progress_dialog, LV_OPA_70, 0);
    lv_obj_set_style_border_width(xmodem_progress_dialog, 0, 0);
    lv_obj_clear_flag(xmodem_progress_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(xmodem_progress_dialog);

    // Dialog box
    lv_obj_t *content = lv_obj_create(xmodem_progress_dialog);
    lv_obj_set_size(content, 600, 320);
    lv_obj_center(content);
    lv_obj_set_style_bg_color(content, UITheme::BG_MEDIUM, 0);
    lv_obj_set_style_border_color(content, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_set_style_border_width(content, 3, 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, LV_SYMBOL_UPLOAD " XModem Transfer...");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, UITheme::ACCENT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *lbl_file = lv_label_create(content);
    lv_label_set_text_fmt(lbl_file, "File: %s", filename);
    lv_obj_set_style_text_font(lbl_file, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_file, UITheme::TEXT_LIGHT, 0);
    lv_label_set_long_mode(lbl_file, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_file, 550);
    lv_obj_align(lbl_file, LV_ALIGN_TOP_LEFT, 0, 45);

    lv_obj_t *lbl_dest = lv_label_create(content);
    lv_label_set_text_fmt(lbl_dest, "Destination: %s", xmodem_remote_path);
    lv_obj_set_style_text_font(lbl_dest, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_dest, UITheme::TEXT_DISABLED, 0);
    lv_label_set_long_mode(lbl_dest, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_dest, 550);
    lv_obj_align(lbl_dest, LV_ALIGN_TOP_LEFT, 0, 75);

    xmodem_progress_bar = lv_bar_create(content);
    lv_obj_set_size(xmodem_progress_bar, 560, 30);
    lv_obj_align(xmodem_progress_bar, LV_ALIGN_TOP_LEFT, 0, 110);
    lv_bar_set_value(xmodem_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(xmodem_progress_bar, UITheme::BG_DARKER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(xmodem_progress_bar, UITheme::ACCENT_PRIMARY, LV_PART_INDICATOR);

    xmodem_progress_label = lv_label_create(content);
    lv_label_set_text(xmodem_progress_label, "Waiting for FluidNC...");
    lv_label_set_long_mode(xmodem_progress_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(xmodem_progress_label, 550);
    lv_obj_set_style_text_font(xmodem_progress_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(xmodem_progress_label, UITheme::TEXT_LIGHT, 0);
    lv_obj_align(xmodem_progress_label, LV_ALIGN_TOP_LEFT, 0, 155);

    lv_obj_t *lbl_note = lv_label_create(content);
    lv_label_set_text(lbl_note, "Restart device to cancel transfer");
    lv_obj_set_style_text_font(lbl_note, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_note, UITheme::TEXT_DISABLED, 0);
    lv_obj_align(lbl_note, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_refr_now(NULL);
}

void UITabFiles::closeXModemProgress(bool success, const char* error) {
    if (!xmodem_progress_dialog) return;

    // Null these out immediately so checkXModemProgress doesn't call us again next frame
    xmodem_progress_bar   = nullptr;
    xmodem_progress_label = nullptr;

    lv_obj_t *content = lv_obj_get_child(xmodem_progress_dialog, 0);
    if (!content) return;

    lv_obj_t *title = lv_obj_get_child(content, 0);
    if (title) {
        if (success) {
            lv_label_set_text(title, LV_SYMBOL_OK " Transfer Complete");
            lv_obj_set_style_text_color(title, UITheme::UI_SUCCESS, 0);
        } else {
            lv_label_set_text(title, LV_SYMBOL_WARNING " Transfer Failed");
            lv_obj_set_style_text_color(title, UITheme::STATE_ALARM, 0);
        }
    }

    // status label is child index 4 in content (title, lbl_file, lbl_dest, bar, status_label, note)
    lv_obj_t *status_lbl = lv_obj_get_child(content, 4);
    if (status_lbl) {
        if (success) {
            const XModemTransferState& st = FluidNCClient::getXModemState();
            uint32_t ms = transfer_start_ms ? millis() - transfer_start_ms : 0;
            if (ms >= 500 && st.totalBytes > transfer_start_bytes) {
                char done_text[96];
                snprintf(done_text, sizeof(done_text), "Transferred %u KB in %.1f s (%.1f KB/s)",
                         (unsigned)(st.totalBytes / 1024), ms / 1000.0f,
                         (st.totalBytes - transfer_start_bytes) / 1024.0f / (ms / 1000.0f));
                lv_label_set_text(status_lbl, done_text);
            } else {
                lv_label_set_text(status_lbl, "File transferred successfully");
            }
            lv_obj_set_style_text_color(status_lbl, UITheme::UI_SUCCESS, 0);
        } else {
            lv_label_set_text_fmt(status_lbl, "Error: %s", error ? error : "unknown");
            lv_obj_set_style_text_color(status_lbl, UITheme::STATE_ALARM, 0);
        }
    }

    // Replace the "restart to cancel" note with a Close button
    uint32_t n = lv_obj_get_child_count(content);
    if (n > 0) {
        lv_obj_delete(lv_obj_get_child(content, n - 1)); // remove note label
    }
    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_set_size(btn_row, LV_PCT(100), 60);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    if (success) {
        // Show the destination folder in the file list
        lv_obj_t *btn_goto = lv_btn_create(btn_row);
        lv_obj_set_size(btn_goto, 220, 50);
        lv_obj_set_style_bg_color(btn_goto, UITheme::ACCENT_PRIMARY, 0);
        lv_obj_add_event_cb(btn_goto, [](lv_event_t *e) {
            if (xmodem_progress_dialog) {
                lv_obj_delete(xmodem_progress_dialog);
                xmodem_progress_dialog = nullptr;
                xmodem_progress_bar    = nullptr;
                xmodem_progress_label  = nullptr;
            }
            navigateToUploadDirectory();
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *gl = lv_label_create(btn_goto);
        lv_label_set_text(gl, LV_SYMBOL_DIRECTORY " Open Folder");
        lv_obj_set_style_text_font(gl, &lv_font_montserrat_18, 0);
        lv_obj_center(gl);
    }

    lv_obj_t *btn_close = lv_btn_create(btn_row);
    lv_obj_set_size(btn_close, 160, 50);
    lv_obj_set_style_bg_color(btn_close, UITheme::BG_BUTTON, 0);
    lv_obj_add_event_cb(btn_close, [](lv_event_t *e) {
        if (xmodem_progress_dialog) {
            lv_obj_delete(xmodem_progress_dialog);
            xmodem_progress_dialog = nullptr;
            xmodem_progress_bar    = nullptr;
            xmodem_progress_label  = nullptr;
        }
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *bl = lv_label_create(btn_close);
    lv_label_set_text(bl, "Close");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_18, 0);
    lv_obj_center(bl);

    lv_refr_now(NULL);
}

// Called from the main loop to poll XModem transfer state and update the UI
void UITabFiles::checkXModemProgress() {
    if (!FluidNCClient::isXModemTransferActive() && !xmodem_progress_dialog) {
        return; // Nothing to do
    }

    const XModemTransferState& state = FluidNCClient::getXModemState();

    if (state.active && !state.completed) {
        // Transfer in progress – update progress bar and label
        if (xmodem_progress_bar && state.totalBytes > 0) {
            int pct = (int)((state.bytesSent * 100) / state.totalBytes);
            lv_bar_set_value(xmodem_progress_bar, pct, LV_ANIM_OFF);
        }
        if (xmodem_progress_label && state.totalBytes > 0 && state.bytesSent > 0) {
            if (transfer_start_ms == 0) {
                transfer_start_ms = millis();
                transfer_start_bytes = state.bytesSent;
            }
            char prog_text[96];
            formatTransferProgress(prog_text, sizeof(prog_text), state.bytesSent, state.totalBytes,
                                   state.bytesSent - transfer_start_bytes, millis() - transfer_start_ms);
            lv_label_set_text(xmodem_progress_label, prog_text);
        }
        return;
    }

    // Only finalize once: xmodem_progress_bar is nulled by closeXModemProgress on first call
    if (state.completed && xmodem_progress_dialog && xmodem_progress_bar) {
        // Final update: fill bar to 100% if successful
        if (state.success) {
            lv_bar_set_value(xmodem_progress_bar, 100, LV_ANIM_OFF);
        }
        closeXModemProgress(state.success,
                            state.error[0] ? state.error : nullptr);
    }
}

