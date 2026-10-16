# FluidTouch: my fork

Personal fork of [jeyeager65/FluidTouch](https://github.com/jeyeager65/FluidTouch):
touchscreen pendant firmware (ESP32-S3, LVGL 9.5, 800×480) for FluidNC, used
on a plasma CNC. The owner works on macOS and speaks Polish; reply in Polish.

## Branches

| Branch | Purpose |
|---|---|
| `main` | Mirror of upstream. Sync with GitHub "Sync fork" or `git fetch upstream && git merge upstream/main` |
| `macos-sim` | Desktop simulator ported to macOS. Small and generic, could go upstream as a PR |
| `my-ui` | Owner's own UI changes, based on `macos-sim`. **Work here** |

Remote `upstream` = jeyeager65/FluidTouch (add it if missing).

## Desktop simulator (macOS)

```bash
brew install sdl2 cmake ninja      # once; Xcode Command Line Tools needed
./sim/build.sh --run               # incremental build + launch
./sim/build.sh --clean --run       # after moving the folder or CMake changes
python3 sim/tools/fake_fluidnc.py --port 81   # fake controller, in a second terminal
```

In the simulator add a machine: URL `127.0.0.1`, port `81`, any non-empty
SSID/password. `--script` drives touch input and takes screenshots (see
`sim/README.md`), useful for checking UI changes without clicking.

What the `macos-sim` branch changed: `sim/build.sh`, Homebrew SDL2 lookup in
`sim/CMakeLists.txt`, SIGPIPE ignored and exe path via `_NSGetExecutablePath`
in `sim/src/sim_main.cpp`, quoting of the force-included header path (the
checkout used to live under a path with spaces, `.../Plasma CNC/...`).

## UI changes on `my-ui` (Status tab)

- MODAL list in the right column removed; the WCS (G54) button stays
- Right column: three jog dials X / Y / Z (axis letter on the knob), step
  button above them cycles 0.1 / 1 / 10. Hand-wheel behaviour: 12 detents per
  turn, clockwise = + for X/Y but − for Z (torch down towards the material;
  Z0 is on the material, ~40 at the top). Detents are batched into one
  `$J=G91` every 100 ms,
  max 20 per jog, nothing collected unless Idle/Jog. Feed from Jog settings
- Bottom row next to a narrower message box: **Zero X Y** (`G10 L20 P0 X0 Y0`,
  locked-WCS confirmation like the Control tab), **X0 Y0** (`G90 G0 X0 Y0`,
  Idle only, Z untouched), **Home** (`$H`, Idle or Alarm)
- Code: `src/ui/tabs/ui_tab_status_jog.cpp` + `include/ui/tabs/ui_tab_status_jog.h`;
  `ui_tab_status.cpp` only calls into it (kept small to ease upstream merges)

Open question: knobs are ~70 px (~13 mm on the real panel). May need to be
bigger on hardware, e.g. at the cost of the FEED/SPINDLE column.

## Where things are

| Change | File |
|---|---|
| Colours | `include/ui/ui_theme.h` |
| Status tab | `src/ui/tabs/ui_tab_status.cpp` |
| Control tab (jog, joystick, probe, overrides) | `src/ui/tabs/control/*.cpp` |
| Tab bar / bottom status bar | `src/ui/ui_tabs.cpp`, `src/ui/tabs/ui_tab_main.cpp` |
| Dialogs, keyboard | `src/ui/ui_common.cpp` |
| Fonts enabled | `include/lv_conf.h` (Montserrat 12–26, 32) |
| Sending G-code | `FluidNCClient::sendCommand()` in `src/network/fluidnc_client.cpp` |

## Flashing the device

PlatformIO (VS Code extension or `brew install platformio`). Environments:
`elecrow-crowpanel-7-basic`, `elecrow-crowpanel-7-advance-v12`,
`elecrow-crowpanel-7-advance-v13` (owner hasn't said which board yet; ask).

```bash
pio run -e elecrow-crowpanel-7-advance-v13 -t upload
```

USB upload keeps saved machines/settings. If upload can't connect: hold BOOT,
tap RESET, release BOOT, retry.

Known issue: the owner's default Python is 3.15 from python.org, which lacks
`lzma`, so PlatformIO can't unpack toolchains. Fix: `brew install python@3.12`,
`rm -rf ~/.platformio/penv`, and in VS Code settings
`"platformio-ide.customPATH": "/opt/homebrew/opt/python@3.12/libexec/bin"`.

The project path must not contain spaces: ESP-IDF stops with "Detected a
whitespace character in project paths" (a symlink doesn't help, PlatformIO
resolves the real path). The checkout is now at `.../dom/Plasma_CNC/...`.

`my-ui` builds cleanly for `elecrow-crowpanel-7-advance-v13` (first build
~6 min, flash 33.5%); warnings come only from ESP-IDF managed components.

Alternative: GitHub Actions "Build Branch Preview" (run manually on `my-ui`)
publishes to the fork's web installer at https://awalesa.github.io/FluidTouch/
once Actions and Pages (branch `gh-pages`) are enabled.

## Ideas parked for later

- OTA self-update: possible on Advance (16 MB, two OTA partitions), not on
  Basic (4 MB, single app). Options: from SD card, web upload over Wi-Fi, or
  fetch from GitHub
