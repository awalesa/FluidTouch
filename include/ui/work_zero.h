#ifndef WORK_ZERO_H
#define WORK_ZERO_H

#include <lvgl.h>

// Sets the work zero of the current coordinate system for the given axes.
//
// "G10 L20 P0 X0" zeroes the position FluidNC planned to reach, but the axis
// stops on the nearest motor step, so the reported work position can read
// 0.003 or -0.005 instead of 0.000 (more so after the Status tab's jog dials,
// which move by any fraction of a millimetre). So after the zero this asks
// for a status report ("?": while idle FluidNC only reports on changes, so
// none may come by itself) and, once the machine is idle, zeroes out any
// small difference left (under MAX_CORRECTION) with a second G10 L20. Works
// the same with a G92 offset active, unlike setting the offset with G10 L2.
class WorkZero {
public:
    // axes: e.g. "XY", "Z", "XYZA"; callers leave out disabled axes
    static void zero(const char *axes);

private:
    static constexpr float MAX_CORRECTION = 0.05f;    // mm; anything larger isn't step rounding
    static constexpr float MIN_CORRECTION = 0.0005f;  // reports have 3 decimals
    static constexpr uint32_t QUERY_DELAY_MS = 150;   // let FluidNC run the G10 first
    static constexpr uint32_t GIVE_UP_MS = 3000;

    static char pending_axes[8];
    static uint32_t sent_ms;
    static uint32_t next_query_ms;  // when to send "?"; 0 once sent
    static uint32_t query_ms;       // when "?" was sent
    static lv_timer_t *check_timer;

    static void onCheck(lv_timer_t *t);
};

#endif
