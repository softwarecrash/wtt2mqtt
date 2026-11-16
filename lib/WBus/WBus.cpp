// SPDX-License-Identifier: GPL-3.0-or-later
#include "WBus.h"
namespace wtt {
int buildFrame(uint8_t* out, size_t cap, uint8_t cmd, const uint8_t* data, size_t n) {
    if (!out || n > 60 || cap < n + 4 || (n && !data)) return -1;
    out[0] = 0xF4; out[1] = uint8_t(n + 2); out[2] = cmd;
    if (n) memcpy(out + 3, data, n);
    uint8_t x = 0; for (size_t i = 0; i < n + 3; ++i) x ^= out[i];
    out[n + 3] = x; return int(n + 4);
}
int checkFrame(const uint8_t* b, size_t n, uint8_t cmd) {
    if (!b || n < 4 || n > 64 || b[1] < 2 || size_t(b[1]) + 2 != n || b[0] != 0x4F) return -1;
    uint8_t x = 0; for (size_t i = 0; i < n; ++i) x ^= b[i];
    if (x || b[2] != uint8_t(cmd | 0x80)) return -1;
    return int(n - 4);
}
bool parseSensors(const uint8_t* d, size_t n, Sensors& s) {
    if (!d || n < 4 || d[0] != 5) return false;
    s.temperature = int(d[1]) - 50; s.voltage = ((d[2] << 8) | d[3]) / 1000.0f;
    s.flame = n >= 5 ? d[4] : -1; s.power = n >= 7 ? (d[5] << 8) | d[6] : -1;
    s.glow = n >= 9 ? (d[7] << 8) | d[8] : -1;
    return true;
}
const char* errorText(Error e) {
    switch (e) {
    case Error::None: return "ok";
    case Error::Timeout: return "response_timeout";
    case Error::NoEcho: return "no_bus_echo";
    case Error::EchoMismatch: return "echo_mismatch";
    case Error::Checksum: return "bad_checksum";
    case Error::Length: return "invalid_length";
    case Error::Command: return "unexpected_command";
    case Error::Payload: return "invalid_payload";
    } return "unknown";
}
bool Bus::request(uint32_t now, uint8_t cmd, const uint8_t* data, size_t n) {
    if (!idle()) return false;
    int len = buildFrame(tx_, sizeof tx_, cmd, data, n); if (len < 0) return false;
    txN_ = size_t(len); rxN_ = echoN_ = wireN_ = 0; result_ = Result{}; result_.cmd = cmd;
    since_ = now;
    if (!talked_ || elapsed(now, last_, 10000)) { port_.drive(false); phase_ = BreakLow; }
    else send(now);
    return true;
}
void Bus::send(uint32_t now) {
    // Bounded drain; never wait for bytes or call flush().
    for (int i = 0; i < 256 && port_.read() >= 0; ++i) {}
    port_.write(tx_, txN_); since_ = now; phase_ = Receive;
    if (trace) trace("TX", tx_, txN_);
}
void Bus::finish(uint32_t now, Error e) {
    result_.error = e; result_.echo = echoN_ == txN_;
    result_.txLength = uint8_t(txN_); result_.echoLength = uint8_t(echoN_);
    result_.durationMs = uint16_t(now - since_);
    if (trace && echoN_) trace("ECHO", tx_, echoN_);
    if (trace && e != Error::None && wireN_) trace("RAW", wire_, wireN_);
    if (trace && rxN_) trace("RX", rx_, rxN_);
    if (e == Error::None) { result_.size = uint8_t(rxN_ - 4); memcpy(result_.data, rx_ + 3, result_.size); }
    ready_ = true; phase_ = Idle; last_ = now; talked_ = true;
    if (completed) completed(result_);
}
void Bus::tick(uint32_t now) {
    if (phase_ == BreakLow && elapsed(now, since_, 50)) { port_.drive(true); phase_ = BreakHigh; since_ = now; }
    else if (phase_ == BreakHigh && elapsed(now, since_, 50)) { port_.uart(); send(now); }
    if (phase_ != Receive && phase_ != Drain) return;
    for (int budget = 0; budget < 128; ++budget) {
        int value = port_.read(); if (value < 0) break;
        uint8_t c = uint8_t(value);
        if (wireN_ < sizeof wire_) wire_[wireN_++] = c;
        if (result_.wireLength < 65535) ++result_.wireLength;
        // On an early bad echo the UART is still transmitting our request.
        // Drain this transaction's remaining bytes before permitting another TX.
        if (phase_ == Drain) continue;
        if (echoN_ < txN_) {
            if (c != tx_[echoN_]) {
                result_.echoIndex = uint8_t(echoN_); result_.expectedEcho = tx_[echoN_]; result_.actualEcho = c;
                result_.error = Error::EchoMismatch; phase_ = Drain; continue;
            }
            ++echoN_; continue;
        }
        if (rxN_ == 0 && c != 0x4F) continue;
        rx_[rxN_++] = c;
        if (rxN_ == 2 && (rx_[1] < 2 || rx_[1] > 62)) { finish(now, Error::Length); return; }
        if (rxN_ >= 2 && rxN_ == size_t(rx_[1]) + 2) {
            uint8_t x = 0; for (size_t i = 0; i < rxN_; ++i) x ^= rx_[i];
            if (x) finish(now, Error::Checksum);
            else if (rx_[2] != uint8_t(result_.cmd | 0x80)) finish(now, Error::Command);
            else finish(now, Error::None);
            return;
        }
    }
    if (elapsed(now, since_, 750)) finish(now, phase_ == Drain ? result_.error : echoN_ < txN_ ? Error::NoEcho : Error::Timeout);
}
bool Bus::take(Result& out) { if (!ready_) return false; out = result_; ready_ = false; return true; }
}
