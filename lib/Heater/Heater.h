// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <WBus.h>
#include <ProtocolScan.h>
namespace wtt {
enum class Action : uint8_t { Start, Stop, Diagnose, Scan, CancelScan };
struct Snapshot {
    Sensors sensors;
    bool online = false, sensorsValid = false, requested = false, cooldown = true;
    bool faultsValid = false, countersValid = false;
    uint32_t sensorAt = 0, faultAt = 0, failures = 0, successes = 0, remainingSeconds = 0;
    uint8_t faultCount = 0, codes[29] = {}, counts[29] = {};
    int workHours = -1, workMinutes = -1, operatingHours = -1, operatingMinutes = -1, starts = -1;
    char device[33] = {}, version[16] = {}, manufacture[16] = {};
    char commandResult[80] = "boot: reading W-Bus identification";
    Error error = Error::Timeout;
    const char* phase = "unknown"; // observed or explicitly inferred, never claimed ECU phase
    bool scanActive = false;
    uint8_t scanProfile = 0, scanResults[ProtocolScan::Count]{};
};
class Heater {
public:
    explicit Heater(Bus& bus): bus_(bus) {}
    void begin(uint32_t now);
    bool command(Action action, uint8_t minutes, uint32_t now);
    void tick(uint32_t now, bool polling = true);
    const Snapshot& snapshot() const { return state_; }
    bool updateSafe(uint32_t now) const;
    bool restartSafe() const { return !state_.requested && !starting_ && !startPending_; }
    void invalidateCommunication() { state_.online = state_.sensorsValid = false; state_.phase = "unknown"; }
private:
    Bus& bus_; Snapshot state_;
    uint32_t pollAt_ = 0, keepAt_ = 0, lastOk_ = 0, runAt_ = 0, coolAt_ = 0, faultPoll_ = 0;
    uint32_t runMs_ = 0;
    uint8_t minutes_ = 30, stopTries_ = 0, diagStep_ = 0, pendingSub_ = 0, identifyStep_ = 1;
    bool startPending_ = false, starting_ = false, firstPoll_ = true;
    void result(const Result& r, uint32_t now);
    void send(uint32_t now, uint8_t cmd, uint8_t sub);
    void stop(uint32_t now);
};
}
