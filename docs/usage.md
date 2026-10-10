# FluidTouch Usage Guide

> Step-by-step instructions for operating your CNC machine with FluidTouch

## Table of Contents

- [First-Time Setup](#first-time-setup)
- [Importing Settings](#importing-settings)
- [Connecting to Your Machine](#connecting-to-your-machine)
- [Basic Operations](#basic-operations)
- [Jogging](#jogging)
- [Probing](#probing)
- [Running Jobs](#running-jobs)
- [Troubleshooting](#troubleshooting)

---

## First-Time Setup

### 1. Power On

Connect the display to power via USB-C or external power supply. The device will boot and show the FluidTouch splash screen.

### 2. Add Your Machine

After the splash screen you'll see the machine selection screen. If no machines are configured, the Add Machine dialog opens; otherwise tap **Edit**, then **Add**.

1. Enter a **Name** (e.g., "CNC Router", "Laser")
2. Choose the **Connection** and fill in its fields:
   - **WiFi:** WiFi SSID and password, FluidNC's IP address or hostname (e.g., `192.168.1.100` or `fluidnc.local`), and the WebSocket port
     - Port 80 for FluidNC v4.0+
     - Port 81 for FluidNC v3.x (WebUI v2)
     - Port 82 for FluidNC v3.x (WebUI v3)
   - **ESP-NOW:** tap **Pair**, then run `$espnow/pair` on FluidNC within 60 seconds (FluidNC v4.1.0 or later)
   - **UART** (Advance only) or **USB CDC:** the baud rate, which must match FluidNC's setting

   The [Configuration Guide](./configuration.md#connection-types) covers the FluidNC settings and wiring for each connection type.
3. Optionally turn on **Load this machine at startup** to skip the machine list from now on
4. Tap **Save**, then **Done**
5. Tap the machine to connect

### 3. Verify Connection

Check the status bar:
- **Left:** Should show machine state (IDLE, ALARM, etc.)
- **Center:** Shows position coordinates
- **Right:** Shows machine name and connection (WiFi network, ESP-NOW, UART or USB CDC)

If showing ALARM, clear it: **Control → Actions → Unlock**

---

## Importing Settings

If you have a settings backup file from another FluidTouch device or a previous installation:

### Automatic Import on Boot

1. Copy `fluidtouch_settings.json` to the root of your Display SD card
2. Power on FluidTouch (with no machines configured)
3. Settings are automatically imported during startup
4. Device restarts after successful import
5. **Important:** Re-enter WiFi passwords for each machine (passwords are not included in backups for security)

### Manual Import After Setup

1. Copy `fluidtouch_settings.json` to Display SD card root
2. Go to **Settings → General**
3. Tap **Clear All Settings** button
4. Confirm the warning dialog
5. Device restarts and auto-imports settings
6. Configure WiFi passwords for WiFi machines, and pair ESP-NOW machines again

### After Import

1. Check each machine configuration
2. Enter WiFi passwords (Settings → Machine Selection → Edit)
3. Test connection to each machine
4. Verify jog/probe settings are correct
5. Check that macros are properly loaded

---

## Connecting to Your Machine

### Machine Selection

On startup, FluidTouch shows saved machines:
1. **Tap a machine** to connect
2. **Edit** button - Modify machine settings
3. **Delete** button - Remove machine
4. **Up/Down** buttons - Reorder machines
5. **Add** button (green) - Add new machine

### Switching Machines

1. Tap **right side of status bar** (machine name area)
2. Confirm restart dialog
3. Device restarts and shows machine selection
4. Select different machine

---

## Basic Operations

### Understanding Machine States

FluidTouch displays the current machine state in the status bar (left side):

- **IDLE** (Green) - Ready for commands
- **RUN** (Cyan) - Executing G-code
- **JOG** (Cyan) - Manual jogging active
- **HOLD** (Yellow) - Feed hold (paused)
- **ALARM** (Red) - Error state, requires clearing
- **CHECK** (Orange) - G-code check mode
- **HOME** (Blue) - Homing cycle active
- **SLEEP** (Gray) - Sleep mode

### Clearing Alarms

If the machine enters ALARM state:

**Method 1 - Use Popup:**
1. ALARM popup appears automatically
2. Tap **Clear Alarm** button
3. Wait for state to change to IDLE

**Method 2 - Use Actions Tab:**
1. Go to **Control → Actions**
2. Tap **Unlock** button
3. If needed, tap **Reset** to fully restart

### Homing the Machine

Before first use, home your machine:

1. Go to **Control → Actions**
2. Tap **Home All** to home all axes
3. Or tap individual **Home X/Y/Z** buttons
4. Wait for homing cycle to complete
5. Status bar shows "IDLE" when done

---

## Jogging

### Button-Based Jogging (Jog Tab)

**XY Jogging:**
1. Go to **Control → Jog**
2. Select **step size** (100, 50, 10, 1, 0.1 mm)
3. Tap **direction buttons** on jog pad
   - Diagonal buttons move both axes
   - N/S buttons move Y only
   - E/W buttons move X only
4. Adjust **feed rate** with ±100 / ±1000 buttons
5. Tap **STOP** to cancel movement

**Z Jogging:**
1. Select **Z step size** (50, 25, 10, 1, 0.1 mm)
2. Tap **Z+** or **Z-** buttons
3. Adjust **feed rate** independently
4. Tap **STOP** to cancel

### Analog Jogging (Joystick Tab)

**XY Joystick:**
1. Go to **Control → Joystick**
2. Drag the **circular joystick** knob
3. Distance from center = speed (quadratic response)
4. Release to stop

**Z Slider:**
1. Drag the **vertical slider** knob
2. Up = Z+, Down = Z-
3. Distance from center = speed
4. Release to stop

**Feed Rate:**
- Set max feed rates in **Settings → Jog**
- Joystick/slider position scales from 0% to 100% of max

---

## Probing

### Setting Up Touch Probe

1. Attach touch probe to spindle/tool holder
2. Connect probe wire to FluidNC
3. Go to **Control → Probe**
4. Configure parameters:
   - **Feed Rate:** Probe speed (default: 100 mm/min)
   - **Max Distance:** How far to search (default: 50 mm)
   - **Retract:** Pull-off distance after contact (default: 2 mm)
   - **Probe Thickness:** Probe plate/puck thickness (default: 0 mm)

### Probing Operations

**Z-Axis Probing (Tool Length):**
1. Position tool above probe plate/work surface
2. Ensure within Max Distance
3. Tap **Z-** probe button
4. Wait for probe cycle to complete
5. Check results display:
   - "SUCCESS" - Shows Z position found
   - "FAILED" - No contact detected

**Offset Adjustment:**
- If using probe plate, enter thickness in **Probe Thickness**
- FluidTouch automatically accounts for thickness in result

---

## Running Jobs

### Before Starting

1. **Home machine** (Control → Actions → Home All)
2. **Load material** and secure workpiece
3. **Zero work coordinates:**
   - Jog to work zero position
   - Control → Actions → Zero All (or individual axes)
4. **Load G-code file** to FluidNC (via SD card or WebUI)

### Starting a Job

1. On the Files tab, start the G-code file
2. FluidTouch Status tab shows:
   - File name
   - Progress bar (0-100%)
   - Elapsed time
   - Estimated completion time
3. Monitor machine state and positions

### During a Job

**Feed Hold (Pause):**
- Press Pause button (Control → Actions)
- FluidTouch shows HOLD popup
- Tap **Resume** to continue

**Stop:**
- Press Stop button (Control → Actions)
- Not to be used as an Emergency stop

### Job Monitoring

Status tab displays:
- **Real-time positions** - Work and machine coordinates
- **Feed rate** - Current vs. programmed
- **Spindle speed** - Current vs. programmed
- **File progress** - Percentage and time remaining
- **Messages** - FluidNC feedback

---

## Troubleshooting

### WiFi Won't Connect

**Check:**
- SSID and password are correct (case-sensitive)
- WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
- Router is in range and operational

**Note:** If importing settings, WiFi passwords must be manually re-entered for security reasons.

### Can't Connect to FluidNC

**Check:**
- FluidNC is powered on and responsive
- IP address/hostname is correct
- Port is correct (80 for v4.0+, 81 for v3.x default)
- Both devices on same WiFi network
- WiFi password is configured (check machine settings)

**Try:**
- Access FluidNC WebUI from browser
- Check FluidNC YAML configuration for WebSocket port
- Restart both FluidTouch and FluidNC

### ESP-NOW Won't Connect

**Check:**
- FluidNC is v4.1.0 or later and within range
- The machine card shows a paired FluidNC hostname, not "Not paired"
- Pair again if FluidNC was unpaired, or switched between AP and STA WiFi mode

### Wired (UART / USB CDC) Won't Connect

**Check:**
- The baud rate matches FluidNC's `baud:` (UART) or `usb_host: baud:` (USB CDC) setting
- UART: TX and RX are crossed, and the display's USB-C cable is unplugged
- USB CDC: FluidNC is a release newer than v4.1.1 (see the [Configuration Guide](./configuration.md#usb-cdc))

If a wired machine shows OFFLINE after FluidNC restarts, it reconnects by itself once FluidNC answers. To reconnect at once, tap the machine name in the status bar and choose **Reconnect**.

### Settings Lost After Power Cycle

FluidTouch stores settings in non-volatile memory, but if settings are lost:

**Prevention:**
1. Go to **Settings → General**
2. Tap **Export Settings**
3. Keep backup file on Display SD card
4. Copy backup to computer for safekeeping

**Recovery:**
1. Insert Display SD card with `fluidtouch_settings.json`
2. Go to **Settings → General → Clear All Settings**
3. Restart triggers automatic import
4. Re-enter WiFi passwords

### Import/Export Issues

**Export Failed:**
- Ensure Display SD card is inserted
- Check SD card has free space
- Try removing and re-inserting SD card
- Restart device and try again

**Auto-Import Not Working:**
- Verify file is named exactly `fluidtouch_settings.json`
- File must be in root directory of Display SD card
- Auto-import only works when no machines are configured
- Check file is valid JSON (view in text editor)

---

*For UI details and screenshots, see [User Interface Guide](./ui-guide.md)*
