// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol derived from WTTC WTTC.ino/logica.h; attribution in third_party/wttc/NOTICE.md.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace wtt {
constexpr size_t FrameMax = 64;
inline bool elapsed(uint32_t now, uint32_t then, uint32_t period) { return uint32_t(now - then) >= period; }
int buildFrame(uint8_t* out, size_t cap, uint8_t cmd, const uint8_t* data, size_t n);
int checkFrame(const uint8_t* b, size_t n, uint8_t cmd);
struct Sensors {
    int temperature = 0, flame = -1, power = -1, glow = -1;
    float voltage = 0;
};
bool parseSensors(const uint8_t* data, size_t n, Sensors& out);
struct Port {
    virtual ~Port() = default;
    virtual void drive(bool high) = 0; // detach UART and drive TX for break
    virtual void uart() = 0;         // restore 2400 8E1
    virtual bool configure(uint32_t baud, bool even) { if (baud != 2400 || !even) return false; uart(); return true; }
    virtual void timing(const char*, bool) {} // bounded RX observation around wakeup stages
    virtual int read() = 0;          // -1 if empty
    virtual void write(const uint8_t*, size_t) = 0; // buffered, no flush
    virtual void writePart(const uint8_t* b, size_t n) { write(b,n); } // continuation of a paced packet
};
enum class Error : uint8_t { None, Timeout, NoEcho, EchoMismatch, Checksum, Length, Command, Payload };
const char* errorText(Error error);
struct Result {
    uint8_t cmd = 0, size = 0, data[60] = {};
    Error error = Error::None;
    bool echo = false;
    uint8_t txLength = 0, echoLength = 0, echoIndex = 0, expectedEcho = 0, actualEcho = 0;
    uint16_t wireLength = 0, durationMs = 0;
};
class Bus {
public:
    explicit Bus(Port& port): port_(port) {}
    bool request(uint32_t now, uint8_t cmd, const uint8_t* data, size_t n);
    void tick(uint32_t now);
    bool take(Result& out);
    bool idle() const { return phase_ == Idle && !ready_; }
    void (*trace)(const char*, const uint8_t*, size_t) = nullptr;
    void (*completed)(const Result&) = nullptr;
private:
    enum Phase { Idle, BreakLow, BreakHigh, Receive, Drain } phase_ = Idle;
    Port& port_;
    uint8_t tx_[64] = {}, rx_[64] = {}, wire_[64] = {};
    size_t txN_ = 0, rxN_ = 0, echoN_ = 0, wireN_ = 0;
    uint32_t since_ = 0, last_ = 0;
    bool ready_ = false, talked_ = false;
    Result result_;
    void send(uint32_t now);
    void finish(uint32_t now, Error error);
};
}
