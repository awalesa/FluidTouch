# Changelog

All notable changes to FluidTouch will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - TBD

### Added

- **ESP-NOW Connection** - Connect to FluidNC directly over ESP-NOW, with no WiFi network needed (requires FluidNC v4.1.0 or later)
  - Pair from the machine editor: tap **Pair**, then run `$espnow/pair` on FluidNC within 60 seconds
  - Machine cards show the paired FluidNC hostname, or "Not paired"
  - File upload isn't available over ESP-NOW
- **Wired UART Connection** (Advance only) - Connect to FluidNC through the display's UART0 header (TX=43, RX=44), which also powers the display
  - Unplug the display's USB-C while wired; the debug console is off in this mode
  - The baud rate must match FluidNC's `uartN: baud:` setting
  - Upload files from the display's SD card to FluidNC's SD card or flash over XModem
  - FluidNC v4.1.1 or later is recommended so FluidTouch re-syncs when FluidNC restarts
- **Wired USB CDC Connection** (Advance only) - Connect to FluidNC's USB host port through the display's USB-C jack
  - The baud rate must match FluidNC's `uart3: usb_host: baud:` setting
  - Upload files from the display's SD card to FluidNC's SD card or flash over XModem
- **Upload Destination** - The upload dialog lets you choose FluidNC's SD card or flash and the folder, for WiFi as well as wired uploads
  - Pick an existing folder with a folder browser laid out like the Files tab
  - Defaults to `/fluidtouch/uploads/`; the choice is remembered until restart
  - Uploads only go to existing folders, except the default folder, which WiFi uploads create if needed
  - **Open Folder** after a successful upload shows the destination in the file list
- **Load This Machine at Startup** - Option in the machine editor that skips the machine list and connects straight to this machine (on by default for new wired machines)
- **Axis Configuration** - Turn the X, Y and Z axes on or off per machine (Settings → General), alongside the existing A axis switch
  - Status tab hides the rows of disabled axes
  - Machines with only the X axis get a single **Main** tab: work position with jog buttons, saved positions (up to 8, tap to go, long-press to delete) and STOP / Home X / Zero X / Unlock buttons
  - Settings backups now include each machine's axis configuration
- **3D Printed Case and Stand** - FreeCAD source and print-ready files for a CrowPanel Advance 7" case with room for a large battery, plus a matching table stand, in [`hardware/`](hardware/)
- **Advance v1.4 and v1.5** - Confirmed supported; they use the same firmware as v1.3
- **Desktop Simulator** - Run and develop the UI on a PC, connected to a real or fake FluidNC controller (see [`sim/README.md`](sim/README.md))

### Changed

- **Machine Editor** - Shows only the fields the selected connection type uses; machine cards show the baud rate for wired connections
- **Startup Machine** - With the machine list turned off, FluidTouch now loads the last machine used instead of the first configured one
- **G-code Parser State on Connect** - FluidTouch requests `$G` as soon as it connects, so the work coordinate system, units, tool, feed and spindle are correct right away when connecting mid-session (thanks @junchao98, #60)
- **Unlock Button Color** - Unlock buttons are now dark cyan, so they no longer look like the X axis color
- **Official ESP32 Platform** - Moved to the official pioarduino platform (Arduino 3.3.12, ESP-IDF 5.5.5)
- **LovyanGFX 1.2.29** - Updated display driver

### Fixed

- **Wrong Alarm Message** - Alarm popups show the correct message, or "Unknown Alarm" for unrecognized alarm numbers (#56)
- **File Upload with mDNS** - Uploading files works when the machine is addressed by an mDNS name such as `fluidnc.local` (#55, thanks @Jfhow)
- **Messages Arriving Together** - FluidNC v4 output is now handled line by line, so status reports sent together with other messages are no longer missed (e.g. the HOLD popup after M0 appeared late, and the Files list loaded slowly on port 80)
- **Busy During Dwells** - A running file job counts as busy even when FluidNC reports Idle during a G4 dwell, so the Files list, Go To position fields and screen dimming don't act mid-job
- **Failed Upload Dialog** - A failed WiFi upload now shows the error and a Close button; previously the dialog couldn't be dismissed
- **Position Fields Bouncing** - The work and machine position fields no longer jump while editing

### Known Issues

- **Basic and Advance v1.2** - This release has had minimal testing on the Basic display and none on the Advance v1.2. Support for both may be dropped in a future release.
- **USB CDC needs a newer FluidNC** - FluidNC v4.1.1 and earlier can attach to the display's USB serial chip with the wrong driver, so the connection doesn't work reliably. The fix ([bdring/FluidNC#1875](https://github.com/bdring/FluidNC/pull/1875)) is merged but not yet in a FluidNC release. Until then, USB CDC needs a FluidNC build from its main branch. UART and ESP-NOW are not affected.

## [1.0.5] - 2026-04-24

### Added

- **Probe / Limit Switch Indicators** - Real-time pin status indicators on Control tabs and Status tab
  - Probe indicator on **Control → Probe** and on the Status tab
  - Limit-switch indicators (per axis) on **Control → Actions** and on the Status tab
  - Triggered indicators remain visible for 500ms so brief activations are easy to see
- **Terminal History** - Terminal tab now retains command history; recall previous commands for re-sending
- **FluidNC Version on About Tab** - Settings → About now displays the connected FluidNC controller's firmware version
- **M7 + M8 Coolant Display** - Status tab correctly shows both flood (M8) and mist (M7) coolant when active simultaneously

### Changed

- **A-Axis Setting is Per-Machine** - A-axis enable/disable is now stored per machine instead of as a global setting, allowing mixed 3-axis and 4-axis machine configurations
- **LVGL 9.5.0** - Updated UI library from 9.4.x to 9.5.0
- **LovyanGFX 1.2.19** - Updated display driver
- **ArduinoJson 7.4.3** - Updated JSON library
- **ESP32 Platform Branch** - Switched to the `Arduino/IDF53_gcc15` branch of the Jason2866 ESP32 platform after the previous `Arduino/IDF53` branch was removed/renamed upstream

### Fixed

- **Build Compatibility** - Resolved build break caused by the LovyanGFX update

## [1.0.4] - 2026-03-23

### Added

- **A-Axis Support** - Optional 4th-axis support for rotary and A-axis machines (#34)
  - Enable/disable via **Settings → General → Enable A-Axis** (takes effect after restart)
  - A-axis position display in status bar and Status tab (orange color)
  - A-axis jog section in **Control → Jog** tab (mirrors Z-axis layout with its own step buttons)
  - Z/A toggle on the right slider in **Control → Joystick** tab when A-axis is enabled
  - **Zero A** button in **Control → Actions** when A-axis is enabled
  - A-axis max feed rate and step size configuration in **Settings → Jog**
- **Configurable Jog Steps** - Per-machine jog step button values now configurable in **Settings → Jog**
  - XY, Z, and A-axis step lists are fully customizable (comma-separated values)
  - Changes saved per machine

### Fixed

- **File List Out of Memory** - Resolved crash when browsing directories with large numbers of files (#35)
- **Message Display Filtering** - Status bar and Status tab now only show informational and error messages, reducing noise from status report data
- **Wakeup Touch Suppression** - First touch input after wake from deep sleep or display-off state no longer triggers unintended UI actions

### Changed

- **Serial Input Forwarding** - Commands entered in a serial terminal (e.g., Arduino IDE Serial Monitor at 115200 baud) are now forwarded to FluidNC for debugging convenience

## [1.0.3] - 2026-02-10

### Added

- **Advance Hardware v1.2 Support** - Added support for Elecrow CrowPanel 7" Advance v1.2 displays (⚠️ untested) (#11)
  - Note: v1.2 requires same DIP switch configuration as v1.3 (S0 and S1 both set to position 1)
- **mDNS Hostname Resolution** - Connect to FluidNC by hostname (e.g., `fluidnc.local`) instead of IP address (#26)
- **Upload Directory Auto-Creation** - Automatically creates upload directory if it doesn't exist when uploading files from Display SD
- **Open Upload Folder Button** - Quick access button to open the uploaded files folder on FluidNC
- **WCS Display Enhancements** - Improved Work Coordinate System labeling and lock status indicators

### Fixed

- **Display Flicker** - Eliminated screen flicker by migrating to ESP-IDF 5.3 and optimizing display timing parameters
- **Basic Hardware Touch Response** - Improved touch sensitivity and reliability on Basic hardware variant (#21, #23)
- **Probe After WCS Confirmation** - Fixed probe operations failing after confirming Work Coordinate System updates
- **Web Installer Caching** - Prevented browser caching of versions.json to ensure latest version list is always displayed

## [1.0.2] - 2026-01-24

### Changed

- **Status Tab Updates**
  - **Pause/Resume and Stop Buttons** - Pause/Resume/Stop from status tab (#13)
  - **Position Update on Status Tab** - Click positions on status tab to update (#14)
  - **WCS Update on Status Tab** - Click work position values to update Work Coordinate System (#15)
- **Settings Updates**
  - **Display Rotation Support** - Can now rotate display 180 degrees via Settings → General (#12)
- **Web Installer Version Selection** - Dropdown to select from multiple firmware versions including preview builds

### Fixed

- **File Upload with Spaces** - Fixed handling of filenames containing spaces in upload operations

## [1.0.1] - 2025-12-04

### Fixed

- **Touch Screen Deep Sleep Bug** - Fixed touch screen not working after deep sleep wake on Basic hardware
- **Boot Display Flash** - Fixed garbled screen flash on startup

### Changed

- **Documentation Updates** - Product links now use affiliate codes to help support development
- **Hardware Recommendation** - Advance display model now marked as recommended (superior IPS display, optional battery case)

## [1.0.0] - 2025-11-17

### Initial Release

FluidTouch 1.0.0 is the first stable release of the ESP32-S3 touchscreen CNC controller for FluidNC-based machines.

#### Features

- **Real-time Machine Control** - Monitor position, state, feed/spindle rates with live updates
- **Multi-Machine Support** - Store and switch between up to 4 different CNC configurations
- **Intuitive Jogging** - Button-based and analog joystick interfaces with configurable step sizes
- **Touch Probe Operations** - Automated probing with customizable parameters
- **Macro Support** - Configure and store up to 9 file-based macros per machine
- **File Management** - Browse and manage files from FluidNC SD, FluidNC Flash, and Display SD card
- **Settings Backup & Restore** - Export settings to JSON, auto-import on fresh install
- **Power Management** - Configurable display dimming, sleep, and deep sleep modes
- **WiFi Connectivity** - WebSocket connection to FluidNC with automatic status reporting
- **Terminal** - Execute custom commands and view FluidNC messages

#### Supported Hardware

- Elecrow CrowPanel 7" Basic ESP32-S3 HMI Display (4MB Flash + 8MB PSRAM)
- Elecrow CrowPanel 7" Advance ESP32-S3 HMI Display (16MB Flash + 8MB PSRAM)
  - Hardware Version 1.3 only

#### Documentation

- Complete user interface guide with screenshots
- Usage instructions and workflows
- Configuration guide for WiFi, machines, and settings
- Development guide for building from source

[1.0.5]: https://github.com/jeyeager65/FluidTouch/releases/tag/v1.0.5
[1.0.4]: https://github.com/jeyeager65/FluidTouch/releases/tag/v1.0.4
[1.0.3]: https://github.com/jeyeager65/FluidTouch/releases/tag/v1.0.3
[1.0.2]: https://github.com/jeyeager65/FluidTouch/releases/tag/v1.0.2
[1.0.1]: https://github.com/jeyeager65/FluidTouch/releases/tag/v1.0.1
[1.0.0]: https://github.com/jeyeager65/FluidTouch/releases/tag/v1.0.0
