// SPDX-License-Identifier: GPL-3.0-or-later
#include "Heater.h"
#include <stdio.h>
namespace wtt {
void Heater::begin(uint32_t now) { coolAt_ = pollAt_ = faultPoll_ = now; }
void Heater::stop(uint32_t now) {
    startPending_ = starting_ = false; state_.requested = false; state_.cooldown = true; coolAt_ = now; stopTries_ = 3;
}
bool Heater::command(Action a, uint8_t minutes, uint32_t now) {
    if (a == Action::Stop) { stop(now); strcpy(state_.commandResult, "stop queued"); return true; }
    if (a == Action::Diagnose) { diagStep_ = 1; return true; }
    if (a != Action::Start) return false; // scan is owned by main, never a heating action
    if (!minutes || minutes > 120 || !state_.online || !state_.sensorsValid || state_.cooldown ||
        state_.requested || starting_ || startPending_ || state_.sensors.flame != 0 || stopTries_) {
        strcpy(state_.commandResult, "start rejected: stale data, busy, flame or cooldown"); return false;
    }
    minutes_ = minutes; startPending_ = starting_ = true; strcpy(state_.commandResult, "start queued"); return true;
}
bool Heater::updateSafe(uint32_t now) const {
    // Firmware maintenance does not require an attached/powered heater, sensor
    // replies or the inferred cooldown timer. Only our own active/pending start
    // conflicts with the service window. The caller blocks new starts via OTA.
    (void)now;
    return restartSafe();
}
void Heater::send(uint32_t now, uint8_t cmd, uint8_t sub) { pendingSub_ = sub; bus_.request(now, cmd, &sub, 1); }
void Heater::result(const Result& r, uint32_t now) {
    if (r.error != Error::None) {
        ++state_.failures; state_.error = r.error;
        if (r.cmd == 0x21) { strcpy(state_.commandResult, "start unconfirmed: stopping conservatively"); stop(now); }
        if (r.cmd == 0x10) strcpy(state_.commandResult, "stop unconfirmed: retry/fail-safe keepalive expiry");
        return;
    }
    const uint8_t* d = r.data; size_t n = r.size;
    bool valid = false;
    switch (r.cmd) {
    case 0x21:
        if (!starting_) { stop(now); strcpy(state_.commandResult, "start cancelled while awaiting ACK; stop queued"); return; }
        starting_ = false;
        valid = n == 1 && d[0] == minutes_;
        if (valid) {
            state_.requested = true; runAt_ = keepAt_ = now; runMs_ = uint32_t(minutes_) * 60000;
            strcpy(state_.commandResult, "start acknowledged; actual combustion requires flame");
        } else { stop(now); strcpy(state_.commandResult, "start invalid response: stop queued"); }
        break;
    case 0x10:
        valid = n == 1 && d[0] == 0;
        if (valid) { stopTries_ = 0; strcpy(state_.commandResult, "stop acknowledged; cooldown estimated 180 s"); }
        break;
    case 0x44:
        valid = n == 1 && d[0] <= 1;
        if (valid && d[0] == 1) { stop(now); strcpy(state_.commandResult, "heater no longer accepts active command"); }
        break;
    case 0x50:
        if (pendingSub_ == 5) {
            valid = parseSensors(d, n, state_.sensors);
            if (valid) { state_.sensorsValid = true; state_.sensorAt = now; }
        } else if (pendingSub_ == 6 && n >= 9 && d[0] == 6 && d[3] < 60 && d[6] < 60) {
            state_.workHours = (d[1] << 8) | d[2]; state_.workMinutes = d[3];
            state_.operatingHours = (d[4] << 8) | d[5]; state_.operatingMinutes = d[6];
            state_.starts = (d[7] << 8) | d[8]; state_.countersValid = valid = true;
        } break;
    case 0x56:
        if (n >= 2 && d[0] == 1 && d[1] <= 29 && n == size_t(2 + d[1] * 2)) {
            state_.faultCount = d[1];
            for (uint8_t i = 0; i < d[1]; ++i) { state_.codes[i] = d[2 + i * 2]; state_.counts[i] = d[3 + i * 2]; }
            state_.faultsValid = valid = true; state_.faultAt = now;
        } break;
    case 0x51:
        if (n >= 2 && d[0] == pendingSub_) {
            if (pendingSub_ == 0x0B) {
                size_t k = 0;
                for (size_t i = 1; i < n && k < sizeof(state_.device) - 1; ++i)
                    if (d[i] >= 32 && d[i] <= 126) state_.device[k++] = char(d[i]);
                state_.device[k] = 0; valid = k > 0;
            } else if (pendingSub_ == 0x0A) {
                snprintf(state_.version, sizeof state_.version, "%u.%u", d[1] >> 4, d[1] & 15); valid = true;
            } else if (pendingSub_ == 5 && n >= 4 && d[1] >= 1 && d[1] <= 31 && d[2] >= 1 && d[2] <= 12 && d[3] <= 99) {
                snprintf(state_.manufacture, sizeof state_.manufacture, "%02u/%02u/%04u", d[1], d[2], 2000 + d[3]); valid = true;
            }
        } break;
    }
    if (valid) { state_.error = Error::None; ++state_.successes; lastOk_ = now; state_.online = true; }
    else { state_.error = Error::Payload; ++state_.failures; }
}
void Heater::tick(uint32_t now, bool polling) {
    bus_.tick(now); Result r; if (bus_.take(r)) result(r, now);
    if (state_.online && elapsed(now, lastOk_, 15000)) state_.online = false;
    if (state_.sensorsValid && elapsed(now, state_.sensorAt, 15000)) state_.sensorsValid = false;
    if (state_.cooldown && elapsed(now, coolAt_, 180000)) state_.cooldown = false;
    if (state_.requested && (elapsed(now, runAt_, runMs_) || elapsed(now, lastOk_, 25000))) stop(now);
    state_.remainingSeconds = state_.requested ? (runMs_ - uint32_t(now - runAt_)) / 1000 : 0;
    if (!state_.sensorsValid) state_.phase = "unknown";
    else if (state_.sensors.flame > 0) state_.phase = "combustion_observed";
    else if (state_.cooldown) state_.phase = "cooldown_estimated";
    else if (state_.requested) state_.phase = "start_or_control_pause_inferred";
    else state_.phase = "no_flame_observed";
    if (!bus_.idle() || !polling) return;
    if (stopTries_) { --stopTries_; bus_.request(now, 0x10, nullptr, 0); return; }
    if (startPending_) { startPending_ = false; send(now, 0x21, minutes_); return; }
    if (state_.requested && elapsed(now, keepAt_, 5000)) {
        uint8_t d[] = {0x21, 0}; keepAt_ = now; bus_.request(now, 0x44, d, 2); return;
    }
    // Start with read-only identification after the transport's 50/50-ms wakeup.
    // Attempt both even if one times out; a missing optional register must not
    // prevent normal polling. Explicit stop and keepalive retain priority.
    if (identifyStep_) {
        send(now, 0x51, identifyStep_ == 1 ? 0x0A : 0x0B);
        identifyStep_ = identifyStep_ == 1 ? 2 : 0;
        return;
    }
    if (firstPoll_ || elapsed(now, pollAt_, 2000)) { firstPoll_ = false; pollAt_ = now; send(now, 0x50, 5); return; }
    if (diagStep_) {
        switch (diagStep_++) {
        case 1: state_.device[0] = state_.version[0] = state_.manufacture[0] = 0;
                state_.countersValid = state_.faultsValid = false; send(now, 0x51, 0x0B); break;
        case 2: send(now, 0x51, 5); break;
        case 3: send(now, 0x51, 0x0A); break;
        case 4: send(now, 0x50, 6); break;
        default: send(now, 0x56, 1); diagStep_ = 0; faultPoll_ = now; break;
        } return;
    }
    if (elapsed(now, faultPoll_, 60000)) { faultPoll_ = now; send(now, 0x56, 1); }
}
}
