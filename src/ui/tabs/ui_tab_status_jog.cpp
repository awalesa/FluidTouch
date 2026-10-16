#include "ui/tabs/ui_tab_status_jog.h"
#include "ui/tabs/settings/ui_tab_settings_jog.h"
#include "ui/tabs/control/ui_tab_control_probe.h"
#include "ui/ui_theme.h"
#include "ui/ui_common.h"
#include "ui/wcs_config.h"
#include "network/fluidnc_client.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

UITabStatusJog::Dial UITabStatusJog::dials[UITabStatusJog::MAX_DIALS] = {};
int UITabStatusJog::dial_count = 0;
int UITabStatusJog::step_index_xy = 1;  // 1 by default
int UITabStatusJog::step_index_z = 1;   // 0.5 by default
lv_obj_t *UITabStatusJog::lbl_step_xy = nullptr;
lv_obj_t *UITabStatusJog::lbl_step_z = nullptr;
lv_timer_t *UITabStatusJog::send_timer = nullptr;

namespace {
const float STEPS_XY[] = {0.1f, 1.0f, 10.0f};
const char *const STEP_LABELS_XY[] = {"0.1", "1", "10"};
const int STEP_COUNT_XY = sizeof(STEPS_XY) / sizeof(STEPS_XY[0]);

// Z has a short travel (~40 mm on the plasma), so no 10 mm step
const float STEPS_Z[] = {0.1f, 0.5f, 1.0f};
const char *const STEP_LABELS_Z[] = {"0.1", "0.5", "1"};
const int STEP_COUNT_Z = sizeof(STEPS_Z) / sizeof(STEPS_Z[0]);

// Touches closer than this to the knob centre give a jumpy angle, so they're ignored
const int MIN_RADIUS_PX = 10;

lv_color_t axisColor(char axis) {
    switch (axis) {
        case 'X': return UITheme::AXIS_X;
        case 'Y': return UITheme::AXIS_Y;
        default:  return UITheme::AXIS_Z;
    }
}

float wrap180(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}
} // namespace

float UITabStatusJog::currentStep(char axis) {
    return axis == 'Z' ? STEPS_Z[step_index_z] : STEPS_XY[step_index_xy];
}

void UITabStatusJog::updateStepLabels() {
    char buf[24];
    if (lbl_step_xy) {
        snprintf(buf, sizeof(buf), "XY %s", STEP_LABELS_XY[step_index_xy]);
        lv_label_set_text(lbl_step_xy, buf);
    }
    if (lbl_step_z) {
        snprintf(buf, sizeof(buf), "Z %s", STEP_LABELS_Z[step_index_z]);
        lv_label_set_text(lbl_step_z, buf);
    }
}

bool UITabStatusJog::jogAllowed() {
    if (!FluidNCClient::isConnected()) return false;
    MachineState s = FluidNCClient::getStatus().state;
    return s == STATE_IDLE || s == STATE_JOG;
}

// ---------------------------------------------------------------------------
// Dials
// ---------------------------------------------------------------------------
void UITabStatusJog::createDials(lv_obj_t *tab, int x, int y, int w, int h) {
    // Step selectors across the top of the column: XY on the left, Z on the right
    const int step_h = 28, step_gap = 6;
    const int step_w = (w - step_gap) / 2;
    lv_obj_t **labels[] = {&lbl_step_xy, &lbl_step_z};
    lv_event_cb_t cbs[] = {onStepXYClicked, onStepZClicked};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *btn = lv_button_create(tab);
        lv_obj_set_size(btn, step_w, step_h);
        lv_obj_set_pos(btn, x + i * (w - step_w), y);
        lv_obj_set_style_bg_color(btn, UITheme::BG_BUTTON, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_ext_click_area(btn, 4);
        lv_obj_add_event_cb(btn, cbs[i], LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, i == 0 ? UITheme::AXIS_XY : UITheme::AXIS_Z, 0);
        lv_obj_center(lbl);
        *labels[i] = lbl;
    }
    updateStepLabels();

    // One dial per enabled linear axis
    dial_count = 0;
    const char axes[] = {'X', 'Y', 'Z'};
    for (char a : axes) {
        bool enabled = (a == 'X') ? UICommon::isXAxisEnabled()
                     : (a == 'Y') ? UICommon::isYAxisEnabled()
                                  : UICommon::isZAxisEnabled();
        if (enabled && dial_count < MAX_DIALS) dials[dial_count++].axis = a;
    }
    if (dial_count == 0) return;

    const int top = y + step_h + 6;
    const int cell_h = (h - (top - y)) / dial_count;
    const int knob_d = cell_h - 8 < w ? cell_h - 8 : w;

    for (int i = 0; i < dial_count; i++) {
        Dial &d = dials[i];
        d.tracking = false;
        d.accum = 0;
        d.knob_angle = -90.0f;  // marker starts at 12 o'clock
        d.pending_mm = 0;
        d.busy_until = 0;
        d.last_send = 0;
        d.speed = 0;

        // The whole cell takes the touch, so the finger can circle the knob
        // at any radius without slipping off it
        lv_obj_t *cell = lv_obj_create(tab);
        lv_obj_set_size(cell, w, cell_h);
        lv_obj_set_pos(cell, x, top + i * cell_h);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        // Keep turning even when the finger slides out of the cell mid-turn
        lv_obj_add_flag(cell, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(cell, onDialEvent, LV_EVENT_ALL, &d);

        d.knob = lv_obj_create(cell);
        lv_obj_set_size(d.knob, knob_d, knob_d);
        lv_obj_center(d.knob);
        lv_obj_set_style_radius(d.knob, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d.knob, UITheme::JOYSTICK_BG, 0);
        lv_obj_set_style_border_color(d.knob, axisColor(d.axis), 0);
        lv_obj_set_style_border_width(d.knob, 3, 0);
        lv_obj_set_style_pad_all(d.knob, 0, 0);
        lv_obj_clear_flag(d.knob, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(d.knob, LV_OBJ_FLAG_CLICKABLE);
        // Highlight the rim while the dial is being turned
        lv_obj_set_style_border_color(d.knob, lv_color_white(), LV_STATE_PRESSED);

        // Axis letter in the middle of the knob, rotating about its own centre
        d.label = lv_label_create(d.knob);
        char axis_txt[2] = {d.axis, 0};
        lv_label_set_text(d.label, axis_txt);
        lv_obj_set_style_text_font(d.label, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(d.label, axisColor(d.axis), 0);
        lv_obj_set_style_transform_pivot_x(d.label, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(d.label, lv_pct(50), 0);
        lv_obj_center(d.label);

        // Finger-grip dot that turns with the dial
        d.marker = lv_obj_create(d.knob);
        lv_obj_set_size(d.marker, 12, 12);
        lv_obj_set_style_radius(d.marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d.marker, axisColor(d.axis), 0);
        lv_obj_set_style_border_width(d.marker, 0, 0);
        lv_obj_clear_flag(d.marker, LV_OBJ_FLAG_CLICKABLE);
        d.marker_radius = knob_d / 2 - 11;  // dot sits just inside the rim
        placeMarker(d);
    }

    if (!send_timer) send_timer = lv_timer_create(onSendTimer, SEND_INTERVAL_MS, nullptr);
}

void UITabStatusJog::placeMarker(Dial &d) {
    int r = d.marker_radius;
    float rad = d.knob_angle * (float)M_PI / 180.0f;
    lv_obj_align(d.marker, LV_ALIGN_CENTER, (int)lroundf(cosf(rad) * r), (int)lroundf(sinf(rad) * r));
    // Letter upright when the marker is at 12 o'clock (knob_angle -90); 0.1 degree units
    int deg = ((int)lroundf(d.knob_angle + 90.0f) % 360 + 360) % 360;
    lv_obj_set_style_transform_rotation(d.label, deg * 10, 0);
}

void UITabStatusJog::onDialEvent(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    Dial &d = *static_cast<Dial *>(lv_event_get_user_data(e));

    if (code == LV_EVENT_RELEASED) {
        d.tracking = false;
        d.accum = 0;
        lv_obj_remove_state(d.knob, LV_STATE_PRESSED);
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) return;

    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(d.knob, &a);
    float dx = p.x - (a.x1 + a.x2) / 2.0f;
    float dy = p.y - (a.y1 + a.y2) / 2.0f;

    if (code == LV_EVENT_PRESSED) lv_obj_add_state(d.knob, LV_STATE_PRESSED);

    if (dx * dx + dy * dy < MIN_RADIUS_PX * MIN_RADIUS_PX) {
        d.tracking = false;  // too close to the centre; pick up again once outside
        return;
    }
    // Screen y grows downward, so a positive angle change is clockwise
    float angle = atan2f(dy, dx) * 180.0f / (float)M_PI;
    if (!d.tracking) {
        d.tracking = true;
        d.last_angle = angle;
        return;
    }
    float delta = wrap180(angle - d.last_angle);
    d.last_angle = angle;
    if (!jogAllowed()) return;  // don't let the dial "wind up" while the machine can't jog

    // Motion follows the finger continuously; the gear comes from how far the
    // finger is from the centre, relative to the knob's rim
    float knob_r = lv_obj_get_width(d.knob) / 2.0f;
    float gear = sqrtf(dx * dx + dy * dy) / knob_r;
    if (gear < GEAR_MIN) gear = GEAR_MIN;
    if (gear > GEAR_MAX) gear = GEAR_MAX;
    d.pending_mm += delta / DETENT_DEG * currentStep(d.axis) * gear;

    // The marker and letter still click round a detent at a time
    d.accum += delta;
    bool moved = false;
    while (d.accum >= DETENT_DEG) { d.accum -= DETENT_DEG; d.knob_angle += DETENT_DEG; moved = true; }
    while (d.accum <= -DETENT_DEG) { d.accum += DETENT_DEG; d.knob_angle -= DETENT_DEG; moved = true; }
    if (moved) placeMarker(d);
}

void UITabStatusJog::onSendTimer(lv_timer_t *) {
    uint32_t now = millis();
    for (int i = 0; i < dial_count; i++) {
        Dial &d = dials[i];
        if (fabsf(d.pending_mm) < MIN_SEND_MM) continue;  // keep collecting
        float mm = d.pending_mm;
        d.pending_mm = 0;
        if (!jogAllowed()) continue;

        // Clockwise = + for X; - for Y (owner's preference) and for Z, where it
        // lowers the torch towards the material (Z0), like a hand-wheel screwing down
        if (d.axis == 'Y' || d.axis == 'Z') mm = -mm;

        const int max_feed = (d.axis == 'Z') ? UITabSettingsJog::getMaxZFeed()
                                             : UITabSettingsJog::getMaxXYFeed();
        int32_t lag = (int32_t)(d.busy_until - now);
        if (lag < 0) lag = 0;

        // How fast the dial turns: this motion over the time since the previous
        // jog, smoothed so the feed doesn't jump from jog to jog
        int32_t gap = (int32_t)(now - d.last_send);
        if (gap < SEND_INTERVAL_MS) gap = SEND_INTERVAL_MS;
        // First jog after a pause: the motion was collected over about one
        // interval, not since the last jog, or the start would crawl
        if (gap > MAX_GAP_MS) gap = (lag == 0) ? SEND_INTERVAL_MS : MAX_GAP_MS;
        float speed = fabsf(mm) * 1000.0f / gap;
        d.speed = (lag == 0) ? speed : 0.6f * d.speed + 0.4f * speed;

        // Nudge the feed so the motion queued in FluidNC settles at
        // TARGET_LAG_MS: the next jog arrives while this one still runs and
        // the planner blends them into one move instead of stop-start
        float correction = 1.0f + (float)(lag - TARGET_LAG_MS) / (2.0f * TARGET_LAG_MS);
        if (correction < 0.7f) correction = 0.7f;
        if (correction > 1.5f) correction = 1.5f;
        float feed = d.speed * 60.0f * correction;
        if (feed > max_feed) feed = max_feed;
        if (feed < MIN_FEED) feed = MIN_FEED;

        // Never queue more than MAX_LAG_MS; turning faster than the axis can
        // follow is dropped, like a real hand-wheel
        float fit_mm = (MAX_LAG_MS - lag) * feed / 60000.0f;
        float send_mm = fabsf(mm) < fit_mm ? fabsf(mm) : fit_mm;
        if (send_mm < fabsf(mm)) {
            Serial.printf("[StatusJog] %c: %.3f mm dropped, %d ms queued\n", d.axis, fabsf(mm) - send_mm, (int)lag);
        }
        // Whole microns only; the rounding remainder goes with the next jog
        send_mm = roundf(send_mm * 1000.0f) / 1000.0f;
        if (send_mm < 0.001f) continue;
        float dist = mm < 0 ? -send_mm : send_mm;
        if (fabsf(mm) <= fit_mm) {
            float rest = mm - dist;
            d.pending_mm += (d.axis == 'Y' || d.axis == 'Z') ? -rest : rest;  // back in dial terms
        }

        char cmd[48];
        snprintf(cmd, sizeof(cmd), "$J=G91 %c%.3f F%.0f\n", d.axis, dist, feed);
        Serial.printf("[StatusJog] %s", cmd);
        FluidNCClient::sendCommand(cmd);
        float t_ms = send_mm * 60000.0f / feed;
        d.busy_until = now + lag + (uint32_t)t_ms + (lag == 0 ? START_ALLOWANCE_MS : 0);
        d.last_send = now;
    }
}

void UITabStatusJog::onStepXYClicked(lv_event_t *) {
    step_index_xy = (step_index_xy + 1) % STEP_COUNT_XY;
    updateStepLabels();
}

void UITabStatusJog::onStepZClicked(lv_event_t *) {
    step_index_z = (step_index_z + 1) % STEP_COUNT_Z;
    updateStepLabels();
}

// ---------------------------------------------------------------------------
// Action buttons
// ---------------------------------------------------------------------------
namespace {
// Colour = kind of action, so neighbours can't be mixed up; none of them is
// the X/Y axis blue/green or the red/yellow of Stop/Pause
const lv_color_t COLOR_MACHINE = LV_COLOR_MAKE(0x55, 0x55, 0x55);  // homing, G53 moves (grey)
const lv_color_t COLOR_ZERO = UITheme::AXIS_XY;                    // set work zero (teal, Control > "Zero All")
const lv_color_t COLOR_GOTO = LV_COLOR_MAKE(0xC7, 0x6A, 0x00);     // rapid to work zero (amber)
const lv_color_t COLOR_PROBE = UITheme::AXIS_Z;                    // probe Z (purple, Probe "Z-")

const char *pending_zero_axes = "";  // axes to zero once a locked-WCS dialog is confirmed
} // namespace

void UITabStatusJog::createActionButtons(lv_obj_t *tab, int x, int y, int w, int h) {
    const int gap = 10;
    const int rows = 4;
    const int bh = (h - gap * (rows - 1)) / rows;
    const int half_w = (w - gap) / 2;

    struct Spec {
        int row, col;  // col -1 = full width, else 0 / 1 for the left / right half
        const char *icon;
        const char *text;
        lv_color_t color;
        lv_event_cb_t cb;
    } specs[] = {
        // In the order they're used: home, set work zero, return to it,
        // then the same for Z, lifting the torch and touching off
        {0, -1, LV_SYMBOL_HOME, "Home All", COLOR_MACHINE, onHomeClicked},
        {1, 0, LV_SYMBOL_GPS, "Zero XY", COLOR_ZERO, onZeroXYClicked},
        {1, 1, LV_SYMBOL_PLAY, "Go XY0", COLOR_GOTO, onGoToWorkZeroClicked},
        {2, 0, LV_SYMBOL_GPS, "Zero Z", COLOR_ZERO, onZeroZClicked},
        {2, 1, LV_SYMBOL_DOWN, "Go Z0", COLOR_GOTO, onGoToWorkZ0Clicked},
        {3, 0, LV_SYMBOL_UPLOAD, "Z40 G53", COLOR_MACHINE, onZ40Clicked},
        {3, 1, LV_SYMBOL_DOWNLOAD, "Probe Z", COLOR_PROBE, onProbeClicked},
    };
    for (const Spec &sp : specs) {
        lv_obj_t *btn = lv_button_create(tab);
        bool full = sp.col < 0;
        lv_obj_set_size(btn, full ? w : half_w, bh);
        lv_obj_set_pos(btn, x + (full ? 0 : sp.col * (w - half_w)), y + sp.row * (bh + gap));
        lv_obj_set_style_bg_color(btn, sp.color, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_add_event_cb(btn, sp.cb, LV_EVENT_CLICKED, nullptr);
        if (full) {
            // Wide button: icon and text on one line
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text_fmt(lbl, "%s  %s", sp.icon, sp.text);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
            lv_obj_center(lbl);
        } else {
            // Half button: larger icon on top, short caption under it
            lv_obj_t *icon = lv_label_create(btn);
            lv_label_set_text(icon, sp.icon);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, -11);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, sp.text);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 14);
        }
    }
}

void UITabStatusJog::onProbeClicked(lv_event_t *) {
    if (!FluidNCClient::isConnected()) return;
    if (FluidNCClient::getStatus().state != STATE_IDLE) {
        Serial.println("[StatusJog] Probe ignored: machine not idle");
        return;
    }
    // Same as Control > Probe > Z-, including the locked-WCS confirmation
    UITabControlProbe::probeZ();
}

// axes: "XY" or "Z"; disabled axes are left out
void UITabStatusJog::sendZero(const char *axes) {
    char words[16] = "";
    for (const char *a = axes; *a; a++) {
        bool enabled = (*a == 'X') ? UICommon::isXAxisEnabled()
                     : (*a == 'Y') ? UICommon::isYAxisEnabled()
                                   : UICommon::isZAxisEnabled();
        if (!enabled) continue;
        size_t n = strlen(words);
        snprintf(words + n, sizeof(words) - n, " %c0", *a);
    }
    if (!words[0]) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "G10 L20 P0%s\n", words);
    Serial.printf("[StatusJog] Zero %s: %s", axes, cmd);
    FluidNCClient::sendCommand(cmd);
}

void UITabStatusJog::zeroAxes(const char *axes) {
    if (!FluidNCClient::isConnected()) return;
    // Same lock check as the Control tab's zero buttons
    if (WCSConfig::isCurrentWCSLocked()) {
        const FluidNCStatus &status = FluidNCClient::getStatus();
        char wcs_name[32];
        WCSConfig::getCurrentWCSName(wcs_name, sizeof(wcs_name));
        pending_zero_axes = axes;
        UICommon::showWCSLockDialog(status.modal_wcs, wcs_name, [](lv_event_t *) { sendZero(pending_zero_axes); });
        return;
    }
    sendZero(axes);
}

void UITabStatusJog::onZeroXYClicked(lv_event_t *) { zeroAxes("XY"); }
void UITabStatusJog::onZeroZClicked(lv_event_t *) { zeroAxes("Z"); }

// Move in the current coordinate system. Idle only; the other axes stay put
void UITabStatusJog::moveTo(const char *what, const char *words) {
    if (!FluidNCClient::isConnected()) return;
    if (FluidNCClient::getStatus().state != STATE_IDLE) {
        Serial.printf("[StatusJog] %s ignored: machine not idle\n", what);
        return;
    }
    if (!words[0]) return;
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "%s\n", words);
    Serial.printf("[StatusJog] %s: %s", what, cmd);
    FluidNCClient::sendCommand(cmd);
}

void UITabStatusJog::onGoToWorkZeroClicked(lv_event_t *) {
    bool x = UICommon::isXAxisEnabled(), y = UICommon::isYAxisEnabled();
    if (!x && !y) return;
    char words[32];
    snprintf(words, sizeof(words), "G90 G0%s%s", x ? " X0" : "", y ? " Y0" : "");
    moveTo("Go to work X0 Y0", words);
}

void UITabStatusJog::onGoToWorkZ0Clicked(lv_event_t *) {
    // Z0 is on the material, so the torch comes down at the Z jog feed
    // (Settings > Jog), not a rapid, in case Z0 is off or something is in the way
    if (!UICommon::isZAxisEnabled()) return;
    char words[32];
    snprintf(words, sizeof(words), "G90 G1 Z0 F%d", UITabSettingsJog::getDefaultZFeed());
    moveTo("Go to work Z0", words);
}

void UITabStatusJog::onZ40Clicked(lv_event_t *) {
    // Machine coordinates: lift the torch to the top regardless of work offsets
    if (!UICommon::isZAxisEnabled()) return;
    moveTo("Go to machine Z40", "G53 G0 Z40");
}

void UITabStatusJog::onHomeClicked(lv_event_t *) {
    if (!FluidNCClient::isConnected()) return;
    MachineState s = FluidNCClient::getStatus().state;
    if (s != STATE_IDLE && s != STATE_ALARM) {
        Serial.println("[StatusJog] Home ignored: machine busy");
        return;
    }
    Serial.println("[StatusJog] Home all ($H)");
    FluidNCClient::sendCommand("$H\n");
}
