#ifndef UI_TAB_STATUS_JOG_H
#define UI_TAB_STATUS_JOG_H

#include <lvgl.h>

// Status tab additions: one jog dial per axis (X/Y/Z) plus quick
// "go to work X0 Y0" and "home ($H)" buttons.
//
// Each dial works like a hand-wheel (MPG): turning it by one detent moves
// the axis by the selected step. Detents are collected and sent as one
// $J=G91 jog every SEND_INTERVAL_MS, so a fast spin can't flood FluidNC.
class UITabStatusJog {
public:
    // Dials, stacked vertically, with a step selector on top.
    static void createDials(lv_obj_t *tab, int x, int y, int w, int h);
    // The two quick-action buttons, side by side.
    static void createQuickButtons(lv_obj_t *tab, int x, int y, int w, int h);

private:
    struct Dial {
        char axis;            // 'X', 'Y' or 'Z'
        lv_obj_t *knob;       // round knob
        lv_obj_t *marker;     // dot on the knob rim showing rotation
        int marker_radius;    // distance of the marker from the knob centre (px)
        float last_angle;     // finger angle at the previous event (degrees)
        bool tracking;        // finger is far enough from the centre to read an angle
        float accum;          // rotation not yet turned into a detent (degrees)
        float knob_angle;     // drawn rotation of the marker (degrees)
        int pending;          // detents waiting to be sent (+ = clockwise)
    };

    static constexpr int MAX_DIALS = 3;
    static constexpr float DETENT_DEG = 30.0f;    // 12 detents per turn
    static constexpr int SEND_INTERVAL_MS = 100;
    static constexpr int MAX_DETENTS_PER_SEND = 20;

    static Dial dials[MAX_DIALS];
    static int dial_count;
    static int step_index;
    static lv_obj_t *lbl_step;
    static lv_timer_t *send_timer;

    static float currentStep();
    static void placeMarker(Dial &d);
    static bool jogAllowed();

    static void onDialEvent(lv_event_t *e);
    static void onStepClicked(lv_event_t *e);
    static void onSendTimer(lv_timer_t *t);
    static void onGoToWorkZeroClicked(lv_event_t *e);
    static void onHomeClicked(lv_event_t *e);
};

#endif
