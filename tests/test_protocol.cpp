// SPDX-License-Identifier: GPL-3.0-or-later
#include <Heater.h>
#include "webasto_modelo.h"
#include <deque>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
static int checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr); std::exit(1); } } while (0)
struct FakePort: wtt::Port {
    wf::Webasto model;
    std::deque<uint8_t> bytes;
    std::vector<std::pair<uint32_t,uint8_t>> commands;
    uint32_t now = 0, nextByte = 0;
    bool noEcho = false, corrupt = false, wrongCommand = false, shortSensors = false, malformedFaults = false;
    bool echoMismatch = false, invalidLength = false, echoOnly = false;
    int lowCount = 0, highCount = 0, uartCount = 0;
    void drive(bool high) override { if (high) ++highCount; else ++lowCount; }
    void uart() override { ++uartCount; }
    int read() override {
        if (bytes.empty() || int32_t(now-nextByte) < 0) return -1;
        int c = bytes.front(); bytes.pop_front(); nextByte = now + 5; return c;
    }
    void write(const uint8_t* b, size_t n) override {
        commands.emplace_back(now,b[2]);
        if (noEcho) return;
        for (size_t i=0; i<n; ++i) bytes.push_back(echoMismatch && i==0 ? uint8_t(b[i]^1) : b[i]);
        nextByte = now + 5;
        if (echoMismatch || echoOnly || model.env.mute || model.env.noPower) return;
        wf::Reader reader; bool complete = false;
        for (size_t i=0;i<n;++i) complete = reader.feed(b[i]);
        CHECK(complete);
        uint8_t data[64], frame[64]; int k = model.handle(reader.cmd(),reader.data(),reader.dataLen(),data);
        if (k < 0) return;
        if (shortSensors && reader.cmd()==0x50 && reader.data()[0]==5) k = 4;
        if (malformedFaults && reader.cmd()==0x56) { data[0]=1; data[1]=2; k=2; }
        int count = wf::reply(frame,sizeof frame,wrongCommand ? uint8_t(reader.cmd()+1) : reader.cmd(),data,k);
        if (corrupt) frame[count-1] ^= 1;
        if (invalidLength) frame[1]=255;
        for (int i=0;i<count;++i) bytes.push_back(frame[i]);
    }
};
struct Rig {
    FakePort port; wtt::Bus bus{port}; wtt::Heater heater{bus}; uint32_t now=0;
    Rig(uint32_t start=0): now(start) { port.now=start; heater.begin(start); }
    void run(uint32_t duration) { for (uint32_t i=0;i<duration;++i) { ++now; port.now=now; port.model.tick(1); heater.tick(now); } }
    void ready() { run(181000); CHECK(heater.snapshot().online); CHECK(!heater.snapshot().cooldown); CHECK(heater.updateSafe(now)); }
};
static wtt::Result transaction(FakePort& p, uint8_t cmd=0x50, uint8_t sub=5) {
    wtt::Bus b(p); CHECK(b.request(0,cmd,&sub,1)); CHECK(!b.request(0,cmd,&sub,1));
    wtt::Result r;
    for (uint32_t t=0;t<1200;++t) { p.now=t; b.tick(t); if (b.take(r)) return r; }
    CHECK(false); return r;
}
static void frames() {
    uint8_t f[64], d=5; CHECK(wtt::buildFrame(f,sizeof f,0x50,&d,1)==5);
    const uint8_t expected[]={0xF4,3,0x50,5,0xA2}; CHECK(!memcmp(f,expected,5));
    CHECK(wtt::buildFrame(nullptr,5,0x50,&d,1)<0); CHECK(wtt::buildFrame(f,4,0x50,&d,1)<0);
    CHECK(wtt::buildFrame(f,64,0x50,nullptr,1)<0); CHECK(wtt::buildFrame(f,64,0x50,f,61)<0);
    uint8_t reply[64], sensors[]={5,55,0x31,0x38,1,0x13,0x88,0x05,0x14};
    int n=wf::reply(reply,64,0x50,sensors,9); CHECK(wtt::checkFrame(reply,n,0x50)==9);
    CHECK(wtt::checkFrame(reply,n-1,0x50)<0); CHECK(wtt::checkFrame(reply,n,0x56)<0);
    reply[n-1]^=1; CHECK(wtt::checkFrame(reply,n,0x50)<0);
    wtt::Sensors s; CHECK(wtt::parseSensors(sensors,9,s)); CHECK(s.temperature==5 && s.power==5000 && s.glow==1300 && s.flame==1);
    CHECK(std::fabs(s.voltage-12.6f)<.001f); CHECK(wtt::parseSensors(sensors,4,s)); CHECK(s.flame==-1 && s.power==-1 && s.glow==-1);
    CHECK(!wtt::parseSensors(sensors,3,s)); sensors[0]=6; CHECK(!wtt::parseSensors(sensors,9,s));
    std::puts("PASS frames, bounds, checksum and optional sensors");
}
static void transport() {
    { FakePort p; auto r=transaction(p); CHECK(r.error==wtt::Error::None && r.echo && r.size==9); CHECK(p.lowCount==1 && p.highCount==1 && p.uartCount==1); }
    { FakePort p; p.noEcho=true; CHECK(transaction(p).error==wtt::Error::NoEcho); }
    { FakePort p; p.echoOnly=true; CHECK(transaction(p).error==wtt::Error::Timeout); }
    { FakePort p; p.echoMismatch=true; CHECK(transaction(p).error==wtt::Error::EchoMismatch); }
    { FakePort p; p.corrupt=true; CHECK(transaction(p).error==wtt::Error::Checksum); }
    { FakePort p; p.wrongCommand=true; CHECK(transaction(p).error==wtt::Error::Command); }
    { FakePort p; p.invalidLength=true; CHECK(transaction(p).error==wtt::Error::Length); }
    // Every possible length byte: reject out-of-bounds without memory writes.
    for (int len=0;len<256;++len) { uint8_t f[]={0x4F,uint8_t(len),0xD0,0}; CHECK(wtt::checkFrame(f,4,0x50)<0 || len==2); }
    std::puts("PASS timed byte transport, echo, noise, wrong command, timeout and lengths");
}
static void lifecycle() {
    Rig r; CHECK(!r.heater.command(wtt::Action::Start,30,r.now)); r.ready();
    CHECK(r.heater.command(wtt::Action::Start,30,r.now)); r.run(200000);
    CHECK(r.heater.snapshot().requested); CHECK(r.port.model.flame()==1); CHECK(r.heater.snapshot().sensors.flame==1);
    CHECK(!strcmp(r.heater.snapshot().phase,"combustion_observed")); CHECK(!r.heater.updateSafe(r.now));
    uint32_t previous=0; int renewals=0;
    for (auto x:r.port.commands) if (x.second==0x44) { if (previous) CHECK(uint32_t(x.first-previous)<=5800); previous=x.first; ++renewals; }
    CHECK(renewals>=35); CHECK(!r.heater.command(wtt::Action::Start,30,r.now));
    CHECK(r.heater.command(wtt::Action::Stop,30,r.now)); r.run(1000); CHECK(r.port.model.st==wf::AFTER); CHECK(!r.heater.snapshot().requested);
    CHECK(!r.heater.command(wtt::Action::Start,30,r.now)); r.run(181000); CHECK(r.port.model.st==wf::OFF); CHECK(r.heater.updateSafe(r.now));
    CHECK(r.heater.command(wtt::Action::Start,1,r.now)); r.run(65000); CHECK(!r.heater.snapshot().requested); CHECK(r.heater.snapshot().cooldown);
    std::puts("PASS startup stop, start, 5s keepalive, observed flame, regular stop and duration");
}
static void faults() {
    Rig r; r.ready(); r.port.model.err(2); r.port.model.err(2); r.port.model.err(7);
    CHECK(r.heater.command(wtt::Action::Diagnose,30,r.now)); r.run(8000);
    CHECK(r.heater.snapshot().faultsValid); CHECK(r.heater.snapshot().faultCount==2);
    CHECK(r.heater.snapshot().codes[0]==2 && r.heater.snapshot().counts[0]==2);
    CHECK(r.heater.snapshot().device[0]==0 && r.heater.snapshot().version[0]==0); // upstream model does not implement identification
    CHECK(!r.heater.snapshot().countersValid);
    r.port.malformedFaults=true; r.heater.command(wtt::Action::Diagnose,30,r.now); r.run(8000); CHECK(!r.heater.snapshot().faultsValid);
    r.port.malformedFaults=false;
    CHECK(r.heater.command(wtt::Action::Start,30,r.now)); r.run(1000); r.port.model.env.mute=true; r.run(30000);
    CHECK(!r.heater.snapshot().online && !r.heater.snapshot().sensorsValid); CHECK(!r.heater.snapshot().requested);
    CHECK(!r.heater.updateSafe(r.now)); r.port.model.env.mute=false; r.run(4000); CHECK(r.heater.snapshot().online);
    r.port.shortSensors=true; r.run(3000); CHECK(r.heater.snapshot().sensors.flame==-1); CHECK(!r.heater.updateSafe(r.now));
    std::puts("PASS faults, malformed lists, unsupported ID, communication loss, recovery and stale sensors");
}
static void failSafe() {
    Rig r; r.ready(); r.port.model.env.noFuel=true; CHECK(r.heater.command(wtt::Action::Start,30,r.now)); r.run(260000);
    CHECK(r.port.model.errN>0 && r.port.model.errCode[0]==2); CHECK(r.port.model.flame()==0);
    r.run(60000); CHECK(!r.heater.snapshot().requested);
    Rig a; a.ready(); a.port.corrupt=true; CHECK(a.heater.command(wtt::Action::Start,30,a.now)); a.run(3000);
    CHECK(!a.heater.snapshot().requested); CHECK(a.heater.snapshot().cooldown); CHECK(a.port.model.st==wf::AFTER);
    Rig overflow(0xFFFFFF00u); overflow.ready(); CHECK(overflow.heater.command(wtt::Action::Start,1,overflow.now)); overflow.run(65000); CHECK(!overflow.heater.snapshot().requested);
    std::puts("PASS no-fuel fault, unconfirmed start fail-safe and millis rollover");
}
static void startRaces() {
    Rig r; r.ready(); CHECK(r.heater.command(wtt::Action::Start,30,r.now));
    r.run(2); CHECK(!r.heater.command(wtt::Action::Start,30,r.now)); CHECK(!r.heater.updateSafe(r.now));
    CHECK(r.heater.command(wtt::Action::Stop,30,r.now)); r.run(1500);
    CHECK(!r.heater.snapshot().requested); CHECK(r.port.model.st==wf::AFTER);
    CHECK(!r.heater.command(wtt::Action::Start,30,r.now));
    std::puts("PASS duplicate start while awaiting ACK and stop racing a start ACK");
}
int main() { frames(); transport(); lifecycle(); faults(); failSafe(); startRaces(); std::printf("PASS %d checks using unchanged WTTC simulator and actual firmware protocol/controller\n",checks); }
