// SPDX-License-Identifier: GPL-3.0-or-later
// Independently authored bounded read-only probes; wire evidence: docs/PROTOCOL_VARIANTS.md.
#pragma once
#include "WBus.h"
namespace wtt {
class ProtocolScan {
public:
    static constexpr unsigned Count = 7;
    enum Outcome : uint8_t { Untested, NoEcho, EchoOnly, Invalid, Negative, Positive, Unsupported };
    explicit ProtocolScan(Port& port): port_(port) {}
    bool active() const { return phase_ != Idle; }
    void start(uint32_t now);
    void cancel();
    void tick(uint32_t now);
    uint8_t profile() const { return profile_; }
    const uint8_t* results() const { return results_; }
    static const char* name(unsigned index);
    static const char* outcome(unsigned value);
    void (*trace)(const char*, const uint8_t*, size_t) = nullptr;
    void (*report)(unsigned, unsigned) = nullptr;
    void (*attempt)(unsigned, uint8_t, unsigned, size_t, bool) = nullptr;
    // Separately reusable validator for captured UART fixtures, not a heater model.
    static Outcome validate(bool kwp, const uint8_t* bytes, size_t n, uint8_t service);
    static Outcome validateProfile(unsigned profile, const uint8_t* bytes, size_t n, uint8_t service);
private:
    enum Phase { Idle, Quiet, Low, High, ExtraLow, Settle, Transmit, Receive, Gap } phase_ = Idle;
    Port& port_;
    uint8_t profile_=0, step_=0, tries_=0, results_[Count]{};
    uint8_t tx_[8]{}, raw_[128]{};
    size_t txN_=0, rawN_=0, txAt_=0;
    uint32_t since_=0;
    void beginProfile(uint32_t now);
    void send(uint32_t now);
    void finishAttempt(uint32_t now);
    void next(uint32_t now);
    void receive();
    void configureAndSend(uint32_t now);
    uint8_t service() const;
};
}
