// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real controller code with no attached device; no heater model.
#include <Heater.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
struct DisconnectedPort final : wtt::Port {
    std::vector<std::vector<uint8_t>> sent;
    std::vector<int> wake;
    void drive(bool high) override { wake.push_back(high ? 1 : 0); }
    void uart() override { wake.push_back(2); }
    int read() override { return -1; }
    void write(const uint8_t* data, size_t n) override { sent.emplace_back(data,data+n); }
};
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
int main() {
    DisconnectedPort port; wtt::Bus bus(port); wtt::Heater heater(bus);
    heater.begin(0);
    CHECK(!heater.snapshot().online && !heater.snapshot().sensorsValid);
    CHECK(heater.snapshot().cooldown);
    CHECK(heater.updateSafe(0) && heater.restartSafe());
    for (uint32_t now=0; now<100; ++now) heater.tick(now);
    CHECK(port.sent.empty());
    heater.tick(100);
    CHECK(port.wake == std::vector<int>({0,1,2}));
    CHECK(port.sent.size() == 1);
    CHECK(port.sent[0] == std::vector<uint8_t>({0xF4,0x03,0x51,0x0A,0xAC}));
    for (uint32_t now=101; now<4000; ++now) heater.tick(now);
    CHECK(port.sent.size() >= 3);
    CHECK(port.sent[1] == std::vector<uint8_t>({0xF4,0x03,0x51,0x0B,0xAD}));
    CHECK(port.sent[2] == std::vector<uint8_t>({0xF4,0x03,0x50,0x05,0xA2}));
    for (const auto& frame : port.sent) CHECK(frame[2] != 0x10);
    CHECK(heater.snapshot().failures >= 3);
    CHECK(!heater.snapshot().online && !heater.snapshot().sensorsValid);
    CHECK(heater.updateSafe(4000));
    CHECK(!heater.command(wtt::Action::Start,30,4000));
    CHECK(heater.updateSafe(4000));
    CHECK(heater.command(wtt::Action::Stop,30,4000));
    CHECK(heater.snapshot().cooldown && heater.updateSafe(4000));
    for (uint32_t now=4000; now<7000; ++now) heater.tick(now);
    unsigned stops=0;
    for (const auto& frame : port.sent) if (frame[2] == 0x10) ++stops;
    CHECK(stops == 3);
    std::puts("PASS startup: wakeup then version/name/sensors; no boot stop; explicit stop retries retained");
    std::puts("PASS maintenance: OTA/restart allowed without device, replies or measurements, including boot and inferred cooldown");
}
