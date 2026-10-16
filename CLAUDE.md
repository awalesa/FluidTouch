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
- Right column: three jog dials X / Y / Z (~82 px, each a full third of the
  column height). The step is shown large (26 px) in the middle of each
  knob; the axis letter, large (32 px) in the axis colour, stands to the
  right of the knob. (Tried before: letter on the knob turning with it, which
  looked off-centre; letter above the step.)
  X and Y share one step (tapping either changes both); Z has its own. The knob's centre (45% of its
  radius) is a button: a quick tap there (finger within 10 px, under 500 ms)
  cycles the step: X and Y 0.1 / 1 / 10 (default 1), Z 0.1 / 0.5 / 1
  (default 0.5). Turning only counts outside the centre, where the angle is
  steady; a press that starts in the centre and moves out turns the dial.
  (Before: separate XY / Z step buttons, first above the dials, then upright
  beside them.) Hand-wheel behaviour: 12 detents per
  turn, clockwise = + for X, − for Y (owner's preference) and − for Z (torch
  down towards the material; Z0 is on the material, ~40 at the top). The
  marker dot clicks round per detent, but the motion is continuous: step per
  30 degrees, fractions included. Finger distance from the centre gears it: x1 anywhere on the
  knob, up to x2 well outside it (a x0.4 zone near the centre was tried and
  dropped: too close to the centre to turn reliably). Motion is
  sent as one `$J=G91` every 50 ms, nothing collected unless Idle/Jog.
  MPG-style smoothing: the jog feed follows how fast the dial turns
  (smoothed, capped at the Jog settings max feed) and is nudged so ~150 ms of
  motion stays queued in FluidNC, so jogs blend instead of stop-start; never
  more than 350 ms queued, so the axis stops/reverses quickly; turning faster
  than that is dropped. Tunables in `ui_tab_status_jog.h` (TARGET_LAG_MS,
  MAX_LAG_MS, MAX_GAP_MS, GEAR_MIN/MAX)
- Layout below the state row: three columns. Left: positions, message box
  under them (440 wide). Middle (x 452, 168 wide; where FEED RATE / RAPID / SPINDLE
  was; removed): action buttons in four rows, workflow order:
  **Home All** (`$H`, Idle or Alarm) across the column; **Zero XY**
  (`G10 L20 P0 X0 Y0`) | **Go XY0** (`G90 G0 X0 Y0`); **Zero Z**
  (`G10 L20 P0 Z0`) | **Go Z0** (`G90 G1 Z0 F<Jog default Z feed>`, not a
  rapid: torch down onto the material); **Z40 G53** (`G53 G0 Z40`, torch to
  the top in machine coordinates) | **Probe Z** (Control > Probe "Z-" with
  that tab's parameters, `UITabControlProbe::probeZ()`). Half-width buttons:
  icon on top, short caption under it. Zeroing asks for confirmation on a locked WCS
  like the Control tab; moves and probe are Idle only. Colour = kind of
  action: grey machine (home, G53), teal zero, amber go to work zero,
  purple probe. The zero / go-to-zero pairs sit in a yellow frame (like the
  WCS button); the active WCS (G54...) sits on the frame's right border like
  a dimension line: turned sideways (reads top to bottom), centred on the
  line, with a gap cut in the border (a strip in the background colour, taken
  from the first opaque ancestor). Refreshed every 250 ms. Middle column is
  x 452-619; all the small buttons are the same size, Home All as wide as
  the pairs, and every row and pair has the same 10 px gap; the frame's
  top and bottom lines run through the middle of the gaps around the two
  framed rows (measured: 4/5 px either side of the line). Label offset 1 px to centre the rotated capitals on the line.
  Right (x 620-780): knobs centred between the frame's border (619) and the
  right edge (780, like the WCS button), checked by measuring a screenshot
  (39/39 px); each dial's touch area runs x 621-780
- Code: `src/ui/tabs/ui_tab_status_jog.cpp` + `include/ui/tabs/ui_tab_status_jog.h`;
  `ui_tab_status.cpp` only calls into it (kept small to ease upstream merges)

Bottom status bar (also on `my-ui`): on the Status tab it shows feed
(mm/min), overrides (F / R / S %) and spindle RPM instead of the state and
WPos/MPos the tab already shows; other tabs unchanged, job progress still
takes priority. `UICommon::refreshStatusBarMode()` picks the mode (called on
tab change and from `updateFileProgress`), `UICommon::updateFeedSpindle()`
is fed from `main.cpp`. Also fixed upstream's shadowed `lbl_wpos_label`
local, which kept "WPos:" visible over the job progress. The machine line on the right uses a
loop-arrows icon, LV_SYMBOL_LOOP (was the same WiFi/USB icon as the
link line under it; a gear read as "settings").

Files tab (also on `my-ui`): listing every file via `$SD/ListJSON` /
`$LocalFS/ListJSON` was tried and reverted: on the owner's real controller it
gave "Error parsing file list" (the fake server and FluidNC's current source
looked fine; the controller's actual reply is still to be captured). Lists use
`$Files/ListGcode` again, so non-G-code files (an uploaded settings `.json`)
stay hidden. The list request state is now reset per request and tied to the
storage it was made for: a failed or late reply used to leak into the next
list or wipe the Display SD list (its Upload buttons "vanished").

Open question: knobs are ~70 px (~13 mm on the real panel). May need to be
bigger on hardware.

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
