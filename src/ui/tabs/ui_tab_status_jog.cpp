#include "ui/tabs/ui_tab_status_jog.h"
#include "ui/tabs/settings/ui_tab_settings_jog.h"
#include "ui/ui_theme.h"
#include "ui/ui_common.h"
#include "ui/wcs_config.h"
#include "network/fluidnc_client.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

UITabStatusJog::Dial UITabStatusJog::dials[UITabStatusJog::MAX_DIALS] = {};
int UITabStatusJog::dial_count = 0;
int UITabStatusJog::step_index = 1;  // 1.0 by default
lv_obj_t *UITabStatusJog::lbl_step = nullptr;
lv_timer_t *UITabStatusJog::send_timer = nullptr;

namespace {
const float STEPS[] = {0.1f, 1.0f, 10.0f};
const char *const STEP_LABELS[] = {"0.1", "1", "10"};
const int STEP_COUNT = sizeof(STEPS) / sizeof(STEPS[0]);

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

float UITabStatusJog::currentStep() { return STEPS[step_index]; }

bool UITabStatusJog::jogAllowed() {
    if (!FluidNCClient::isConnected()) return false;
    MachineState s = FluidNCClient::getStatus().state;
    return s == STATE_IDLE || s == STATE_JOG;
}

// ---------------------------------------------------------------------------
// Dials
// ---------------------------------------------------------------------------
void UITabStatusJog::createDials(lv_obj_t *tab, int x, int y, int w, int h) {
    // Step selector across the top of the column
    const int step_h = 28;
    lv_obj_t *btn_step = lv_button_create(tab);
    lv_obj_set_size(btn_step, w, step_h);
    lv_obj_set_pos(btn_step, x, y);
    lv_obj_set_style_bg_color(btn_step, UITheme::BG_BUTTON, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_step, 0, 0);
    lv_obj_set_ext_click_area(btn_step, 8);
    lv_obj_add_event_cb(btn_step, onStepClicked, LV_EVENT_CLICKED, nullptr);
    lbl_step = lv_label_create(btn_step);
    lv_obj_set_style_text_font(lbl_step, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_step, UITheme::POS_MODAL, 0);
    lv_obj_center(lbl_step);
    char buf[24];
    snprintf(buf, sizeof(buf), "Step %s", STEP_LABELS[step_index]);
    lv_label_set_text(lbl_step, buf);

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
        d.pending = 0;

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

        // Axis letter in the middle of the knob
        lv_obj_t *lbl_axis = lv_label_create(d.knob);
        char axis_txt[2] = {d.axis, 0};
        lv_label_set_text(lbl_axis, axis_txt);
        lv_obj_set_style_text_font(lbl_axis, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(lbl_axis, axisColor(d.axis), 0);
        lv_obj_center(lbl_axis);

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

    d.accum += delta;
    while (d.accum >= DETENT_DEG) { d.accum -= DETENT_DEG; d.pending++; d.knob_angle += DETENT_DEG; }
    while (d.accum <= -DETENT_DEG) { d.accum += DETENT_DEG; d.pending--; d.knob_angle -= DETENT_DEG; }
    placeMarker(d);
}

void UITabStatusJog::onSendTimer(lv_timer_t *) {
    for (int i = 0; i < dial_count; i++) {
        Dial &d = dials[i];
        if (d.pending == 0) continue;
        int detents = d.pending;
        d.pending = 0;
        if (!jogAllowed()) continue;
        // Cap one burst so a wild spin can't queue a long move
        if (detents > MAX_DETENTS_PER_SEND) detents = MAX_DETENTS_PER_SEND;
        if (detents < -MAX_DETENTS_PER_SEND) detents = -MAX_DETENTS_PER_SEND;

        int feed = (d.axis == 'Z') ? UITabSettingsJog::getDefaultZFeed()
                                   : UITabSettingsJog::getDefaultXYFeed();
        // Z: clockwise lowers the torch towards the material (Z0), like a hand-wheel
        // screwing down; X and Y: clockwise = +
        if (d.axis == 'Z') detents = -detents;
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "$J=G91 %c%.3f F%d\n", d.axis, detents * currentStep(), feed);
        Serial.printf("[StatusJog] %s", cmd);
        FluidNCClient::sendCommand(cmd);
    }
}

void UITabStatusJog::onStepClicked(lv_event_t *) {
    step_index = (step_index + 1) % STEP_COUNT;
    char buf[24];
    snprintf(buf, sizeof(buf), "Step %s", STEP_LABELS[step_index]);
    lv_label_set_text(lbl_step, buf);
}

// ---------------------------------------------------------------------------
// Quick-action buttons
// ---------------------------------------------------------------------------
void UITabStatusJog::createQuickButtons(lv_obj_t *tab, int x, int y, int w, int h) {
    const int gap = 8;
    const int count = 3;
    const int bw = (w - gap * (count - 1)) / count;

    struct Spec {
        const char *text;
        lv_color_t color;
        lv_event_cb_t cb;
    } specs[count] = {
        // Colours and icons follow the Control > Actions tab ("Zero All", "Home All")
        {LV_SYMBOL_GPS " Zero\nX Y", UITheme::AXIS_XY, onZeroXYClicked},
        {LV_SYMBOL_PLAY " Go\nX0 Y0", UITheme::AXIS_XY, onGoToWorkZeroClicked},
        {LV_SYMBOL_HOME " Home\nAll", UITheme::AXIS_XY, onHomeClicked},
    };
    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_button_create(tab);
        lv_obj_set_size(btn, bw, h);
        lv_obj_set_pos(btn, x + i * (bw + gap), y);
        lv_obj_set_style_bg_color(btn, specs[i].color, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_add_event_cb(btn, specs[i].cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, specs[i].text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
}

void UITabStatusJog::sendZeroXY() {
    bool x = UICommon::isXAxisEnabled(), y = UICommon::isYAxisEnabled();
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "G10 L20 P0%s%s\n", x ? " X0" : "", y ? " Y0" : "");
    Serial.printf("[StatusJog] Zero XY: %s", cmd);
    FluidNCClient::sendCommand(cmd);
}

void UITabStatusJog::onZeroXYClicked(lv_event_t *) {
    if (!FluidNCClient::isConnected()) return;
    if (!UICommon::isXAxisEnabled() && !UICommon::isYAxisEnabled()) return;
    // Same lock check as the Control tab's zero buttons
    if (WCSConfig::isCurrentWCSLocked()) {
        const FluidNCStatus &status = FluidNCClient::getStatus();
        char wcs_name[32];
        WCSConfig::getCurrentWCSName(wcs_name, sizeof(wcs_name));
        UICommon::showWCSLockDialog(status.modal_wcs, wcs_name, [](lv_event_t *) { sendZeroXY(); });
        return;
    }
    sendZeroXY();
}

void UITabStatusJog::onGoToWorkZeroClicked(lv_event_t *) {
    if (!FluidNCClient::isConnected()) return;
    if (FluidNCClient::getStatus().state != STATE_IDLE) {
        Serial.println("[StatusJog] Go to work zero ignored: machine not idle");
        return;
    }
    // XY only, in the current work coordinate system; Z stays where it is
    bool x = UICommon::isXAxisEnabled(), y = UICommon::isYAxisEnabled();
    if (!x && !y) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "G90 G0%s%s\n", x ? " X0" : "", y ? " Y0" : "");
    Serial.printf("[StatusJog] Go to work zero: %s", cmd);
    FluidNCClient::sendCommand(cmd);
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
