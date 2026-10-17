#include "ui/work_zero.h"
#include "network/fluidnc_client.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

char WorkZero::pending_axes[8] = "";
uint32_t WorkZero::sent_ms = 0;
lv_timer_t *WorkZero::check_timer = nullptr;

void WorkZero::zero(const char *axes) {
    char cmd[48] = "G10 L20 P0";
    for (const char *a = axes; *a; a++) {
        size_t n = strlen(cmd);
        snprintf(cmd + n, sizeof(cmd) - n, " %c0", *a);
    }
    Serial.printf("[WorkZero] %s\n", cmd);
    strncat(cmd, "\n", sizeof(cmd) - strlen(cmd) - 1);
    FluidNCClient::sendCommand(cmd);

    strncpy(pending_axes, axes, sizeof(pending_axes) - 1);
    pending_axes[sizeof(pending_axes) - 1] = '\0';
    sent_ms = millis();
    if (!check_timer) check_timer = lv_timer_create(onCheck, 100, nullptr);
}

void WorkZero::onCheck(lv_timer_t *) {
    uint32_t now = millis();
    const FluidNCStatus &s = FluidNCClient::getStatus();
    bool done = !FluidNCClient::isConnected() || now - sent_ms > GIVE_UP_MS;
    // Wait for the machine to stand still and for a report made after the G10
    if (!done && (s.state != STATE_IDLE || (int32_t)(s.last_update_ms - sent_ms) < (int32_t)SETTLE_MS)) return;

    if (!done) {
        char cmd[64] = "G10 L20 P0";
        bool any = false;
        for (const char *a = pending_axes; *a; a++) {
            float w = (*a == 'X') ? s.wpos_x : (*a == 'Y') ? s.wpos_y : (*a == 'Z') ? s.wpos_z : s.wpos_a;
            if (fabsf(w) < MIN_CORRECTION || fabsf(w) >= MAX_CORRECTION) continue;
            // The planned position is 0 now and the axis stands at w, so call
            // the planned position -w to bring the actual one to 0
            size_t n = strlen(cmd);
            snprintf(cmd + n, sizeof(cmd) - n, " %c%.3f", *a, -w);
            any = true;
        }
        if (any) {
            Serial.printf("[WorkZero] Step rounding correction: %s\n", cmd);
            strncat(cmd, "\n", sizeof(cmd) - strlen(cmd) - 1);
            FluidNCClient::sendCommand(cmd);
        }
    }
    lv_timer_delete(check_timer);
    check_timer = nullptr;
}
