#ifndef UI_TAB_STATUS_JOG_H
#define UI_TAB_STATUS_JOG_H

#include <lvgl.h>

// Status tab additions: one jog dial per axis (X/Y/Z) plus a column of
// action buttons: home ($H); zero work XY / go to work X0 Y0; zero work Z /
// go to work Z0; go to machine Z40 (G53) / probe Z.
//
// Each dial works like a hand-wheel (MPG): turning it by one detent (30
// degrees) moves the axis by the selected step, continuously in between, so
// the axis follows the finger rather than jumping a step at a time. The
// finger's distance from the centre gears it: anywhere on the knob x1,
// outside the knob coarser (up to x2). (A finer zone near the centre was
// dropped: too close to the centre the angle is too jumpy to turn.)
// The motion is sent as one $J=G91 jog every SEND_INTERVAL_MS. The feed of
// each jog is matched to how fast the dial turns, so consecutive jogs blend
// into one smooth move, and the motion queued in FluidNC is kept short
// (MAX_LAG_MS) so the axis stops or reverses soon after the dial does;
// turning faster than that is dropped.
class UITabStatusJog {
public:
    // Dials, stacked vertically in x..x+w; each takes touches across
    // touch_x..touch_x+touch_w, so the finger can circle outside the knob
    // (coarser gear) without slipping off. A tap on a knob's centre changes
    // its step, shown large in the middle of the knob (the axis letter is to
    // its right); X and Y share one step.
    static void createDials(lv_obj_t *tab, int x, int y, int w, int h, int touch_x, int touch_w);
    // The action buttons in four rows: Home All across, then pairs side by
    // side; the zero / go-to-zero pairs sit in a frame named after the active
    // WCS (G54...). Probe Z runs Control > Probe "Z-" with that tab's parameters.
    static void createActionButtons(lv_obj_t *tab, int x, int y, int w, int h);

private:
    struct Dial {
        char axis;            // 'X', 'Y' or 'Z'
        lv_obj_t *knob;       // round knob
        lv_obj_t *marker;     // dot on the knob rim showing rotation
        lv_obj_t *label;      // axis letter, right of the knob
        lv_obj_t *step_label; // this dial's step, in the middle of the knob
        int step_index;       // into the X/Y or Z step list (X and Y kept equal)
        int marker_radius;    // distance of the marker from the knob centre (px)
        float last_angle;     // finger angle at the previous event (degrees)
        bool tracking;        // finger is far enough from the centre to read an angle
        float accum;          // rotation not yet turned into a detent (degrees)
        float knob_angle;     // drawn rotation of the marker (degrees)
        float pending_mm;     // motion waiting to be sent (+ = clockwise)
        uint32_t busy_until;  // estimated millis() when the jogs sent so far finish
        uint32_t last_send;   // millis() of the previous jog
        float speed;          // smoothed dial speed (mm/s)
        uint32_t press_ms;    // when the finger went down
        int32_t press_x, press_y;  // where it went down
        bool tap;             // so far a tap on the centre (changes the step)
    };

    static constexpr int MAX_DIALS = 3;
    static constexpr float DETENT_DEG = 30.0f;    // 12 detents per turn
    static constexpr int SEND_INTERVAL_MS = 50;
    static constexpr int TARGET_LAG_MS = 150;  // motion kept queued so the planner never runs dry
    static constexpr int MAX_GAP_MS = 250;     // slowest turning rate followed smoothly
    static constexpr int MAX_LAG_MS = 350;     // never queue more than this
    static constexpr float MIN_SEND_MM = 0.005f;   // smaller motion waits for more
    static constexpr float GEAR_MIN = 1.0f;    // finger on the knob
    static constexpr float GEAR_MAX = 2.0f;    // finger well outside the knob
    static constexpr int START_ALLOWANCE_MS = 50;  // acceleration from rest
    static constexpr int MIN_FEED = 30;        // mm/min

    static Dial dials[MAX_DIALS];
    static int dial_count;
    static lv_timer_t *send_timer;
    static lv_obj_t *lbl_wcs_frame;  // WCS name on the frame round the work-zero buttons
    static lv_timer_t *wcs_timer;
    static char wcs_shown[8];

    static float stepOf(const Dial &d);
    static void cycleStep(Dial &d);
    static void updateWcsLabel();
    static lv_color_t backgroundColor(lv_obj_t *obj);
    static void placeMarker(Dial &d);
    static bool jogAllowed();

    static void onDialEvent(lv_event_t *e);
    static void onSendTimer(lv_timer_t *t);
    static void onZeroXYClicked(lv_event_t *e);
    static void onZeroZClicked(lv_event_t *e);
    static void zeroAxes(const char *axes);
    static void sendZero(const char *axes);
    static void moveTo(const char *what, const char *words);
    static void onGoToWorkZeroClicked(lv_event_t *e);
    static void onGoToWorkZ0Clicked(lv_event_t *e);
    static void onZ40Clicked(lv_event_t *e);
    static void onHomeClicked(lv_event_t *e);
    static void onProbeClicked(lv_event_t *e);
};

#endif
