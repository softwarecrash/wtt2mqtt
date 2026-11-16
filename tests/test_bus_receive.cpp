// SPDX-License-Identifier: GPL-3.0-or-later
// Byte-level UART fixtures for the real transport; no heater model.
#include <WBus.h>
#include <deque>
#include <cstdio>
#include <cstdlib>
struct BytePort final : wtt::Port {
    std::deque<uint8_t> incoming;
    int sends = 0;
    void drive(bool) override {}
    void uart() override {}
    int read() override { if (incoming.empty()) return -1; int c=incoming.front(); incoming.pop_front(); return c; }
    void write(const uint8_t*,size_t) override { ++sends; }
};
static int checks=0,completions=0;
static void complete(const wtt::Result&) { ++completions; }
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static void start(wtt::Bus& bus,BytePort& port) {
    const uint8_t sub=5;
    CHECK(bus.request(0,0x50,&sub,1)); bus.tick(50); bus.tick(100); CHECK(port.sends==1);
}
int main() {
    { BytePort port; wtt::Bus bus(port); bus.completed=complete; start(bus,port); wtt::Result r;
      bus.tick(850); CHECK(bus.take(r)); CHECK(r.error==wtt::Error::NoEcho);
      CHECK(r.echoLength==0 && r.txLength==5 && r.wireLength==0 && r.durationMs==750); }
    { BytePort port; wtt::Bus bus(port); start(bus,port); wtt::Result r;
      port.incoming={0xF4,0x03,0x50,0x05,0xA2}; bus.tick(125); bus.tick(850);
      CHECK(bus.take(r)); CHECK(r.error==wtt::Error::Timeout && r.echo && r.echoLength==5 && r.wireLength==5); }
    { BytePort port; wtt::Bus bus(port); start(bus,port); wtt::Result r;
      port.incoming={0x00}; bus.tick(105);
      CHECK(!bus.take(r) && !bus.idle()); const uint8_t sub=6; CHECK(!bus.request(105,0x50,&sub,1));
      port.incoming={0x03,0x50,0x05,0xA2}; bus.tick(130); bus.tick(849); CHECK(!bus.take(r));
      bus.tick(850); CHECK(bus.take(r)); CHECK(r.error==wtt::Error::EchoMismatch);
      CHECK(r.echoIndex==0 && r.expectedEcho==0xF4 && r.actualEcho==0 && r.wireLength==5);
      CHECK(bus.idle() && port.sends==1); CHECK(bus.request(851,0x50,&sub,1)); CHECK(port.sends==2); }
    { BytePort port; wtt::Bus bus(port); start(bus,port); wtt::Result r;
      port.incoming={0xF4,0x03}; bus.tick(110); bus.tick(850); CHECK(bus.take(r));
      CHECK(r.error==wtt::Error::NoEcho && r.echoLength==2 && r.wireLength==2); }
    { BytePort port; wtt::Bus bus(port); start(bus,port); wtt::Result r;
      // Sensor request echo and an explicit checksum-valid reply (20 C, 12.288 V).
      const uint8_t bytes[]={0xF4,0x03,0x50,0x05,0xA2,0x4F,0x06,0xD0,0x05,0x46,0x30,0x00,0xEA};
      uint32_t now=100; for (uint8_t byte:bytes) { port.incoming.push_back(byte); now+=5; bus.tick(now); }
      CHECK(bus.take(r)); CHECK(r.error==wtt::Error::None && r.echo && r.size==4);
      CHECK(r.wireLength==13 && r.durationMs==65 && r.data[0]==5); }
    CHECK(completions==1);
    std::printf("PASS %d UART checks: no echo, partial echo, no reply, bad echo drain, receive diagnostics and valid frame\n",checks);
}
