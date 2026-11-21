// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WBus.h"
#include "Heater.h"

namespace wtt {
// Read-only adapter for the legacy Webasto Thermo Top C/Z PSA KWP profile.
// It owns no control or clear-fault commands.
class PsaKwp {
public:
    static constexpr uint8_t ReadCount = 14;
    explicit PsaKwp(Port& port): port_(port) {}
    void begin(uint32_t now);
    void tick(uint32_t now, Snapshot& state, bool enabled = true);
    void pause();
    void (*trace)(const char*, const uint8_t*, size_t) = nullptr;
private:
    enum Phase : uint8_t { Idle, Transmit, Receive, Gap } phase_ = Idle;
    static constexpr uint8_t FaultStep = ReadCount;
    static constexpr uint8_t TotalSteps = ReadCount + 1;
    Port& port_;
    uint8_t tx_[8]{}, rx_[128]{};
    size_t txSize_ = 0, txAt_ = 0, rxSize_ = 0;
    uint8_t step_ = 0;
    uint32_t since_ = 0, lastFaultPoll_ = 0;
    void send(uint32_t now);
    void receive(uint32_t now, Snapshot& state);
    void finish(uint32_t now, Snapshot& state, bool valid, const uint8_t* pdu = nullptr, size_t size = 0);
    bool parseFrame(const uint8_t* frame, size_t n, Snapshot& state);
};
}
