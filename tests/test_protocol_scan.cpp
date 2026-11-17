// SPDX-License-Identifier: GPL-3.0-or-later
// Recorded-byte fixtures + disconnected transport; no heater simulation.
#include <ProtocolScan.h>
#include <vector>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
using Scan=wtt::ProtocolScan;
struct Capture final: wtt::Port {
    std::vector<std::vector<uint8_t>> writes;
    std::vector<uint8_t> incoming;
    size_t readAt=0;
    unsigned profile=0,writtenProfile=0;
    uint32_t now=0,lastByte=0; unsigned pacedParts=0;
    std::vector<uint32_t> bauds;
    uint32_t baud=2400; bool even=true;
    unsigned timingStarts=0,timingStops=0; bool observing=false;
    uint32_t timingAt=0; std::vector<uint32_t> wakeDurations;
    void timing(const char*,bool start) override {
        if(start) { CHECK(!observing); observing=true; timingAt=now; ++timingStarts; }
        else if(observing) { observing=false; wakeDurations.push_back(now-timingAt); ++timingStops; }
    }
    void drive(bool) override {}
    void uart() override { baud=2400; even=true; }
    bool configure(uint32_t b,bool e) override { baud=b; even=e; bauds.push_back(b); return true; }
    int read() override { return readAt<incoming.size()?incoming[readAt++]:-1; }
    void write(const uint8_t* b,size_t n) override { writes.emplace_back(b,b+n); writtenProfile=profile; lastByte=now; }
    void writePart(const uint8_t* b,size_t n) override { CHECK(now-lastByte>=10); CHECK(n==1); writes.back().insert(writes.back().end(),b,b+n); lastByte=now; ++pacedParts; }
};
int main() {
    const uint8_t kwp[]={0x83,0xF1,0x51,0xC1,0xC2,0x46,0x8E};
    const uint8_t info[]={0x8F,0xF1,0x51,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x1D,0x00,0x12,0x45,0x98,0x46,0x98,0x42,0x79};
    const uint8_t negative[]={0x83,0xF1,0x51,0x7F,0x81,0x11,0xD6};
    const uint8_t wbus[]={0x4F,0x04,0xD1,0x0A,0x33,0xA3};
    const uint8_t faults[]={0x84,0xF1,0x51,0xE1,0xFF,0xFF,0x00,0xA5};
    CHECK(Scan::validate(true,faults,sizeof faults,0xA1)==Scan::Positive);
    CHECK(Scan::validate(true,faults,sizeof faults-1,0xA1)==Scan::Invalid);
    CHECK(Scan::validate(true,faults,sizeof faults,0x81)==Scan::Invalid);
    CHECK(Scan::validate(true,kwp,sizeof kwp,0x81)==Scan::Positive);
    CHECK(Scan::validate(true,info,sizeof info,0x3C)==Scan::Positive);
    CHECK(Scan::validate(true,negative,sizeof negative,0x81)==Scan::Negative);
    CHECK(Scan::validate(false,wbus,sizeof wbus,0x0A)==Scan::Positive);
    CHECK(Scan::validate(false,wbus,sizeof wbus,0x0B)==Scan::Invalid);
    for(size_t n=1;n<sizeof kwp;++n) CHECK(Scan::validate(true,kwp,n,0x81)==Scan::Invalid);
    for(size_t i=0;i<sizeof kwp;++i) {
        std::vector<uint8_t> bad(kwp,kwp+sizeof kwp); bad[i]^=1;
        CHECK(Scan::validate(true,bad.data(),bad.size(),0x81)!=Scan::Positive);
    }
    CHECK(Scan::validate(true,wbus,sizeof wbus,0x81)==Scan::Invalid);
    const uint8_t echo[]={0x81,0x51,0xF1,0x81,0x44};
    CHECK(Scan::validate(true,echo,sizeof echo,0x81)==Scan::Invalid);
    Capture port; Scan scan(port); scan.start(0);
    for(uint32_t now=0;now<70000 && scan.active();++now) { port.now=now; scan.tick(now); }
    CHECK(!scan.active()); CHECK(port.baud==2400 && port.even);
    for(unsigned i=0;i<Scan::Count;++i) CHECK(scan.results()[i]==Scan::NoEcho);
    CHECK(port.writes.size()==20); // 2 W-Bus reads + 3 attempts per other profile
    CHECK(port.timingStarts==22 && port.timingStops==22 && !port.observing);
    for(const auto& b:port.writes) {
        if(b[0]==0xF4) CHECK(b.size()==5 && b[2]==0x51);
        else CHECK(b==std::vector<uint8_t>(echo,echo+sizeof echo) || b==std::vector<uint8_t>({0x81,0x01,0xF1,0x81,0xF4}) || b==std::vector<uint8_t>({0x3F,0x03,0x6B,0x00,0x57}));
    }
    // Feed an exact captured positive reply after an echoed session request.
    CHECK(port.wakeDurations==std::vector<uint32_t>({50,50,25,25,30,25,300,50,25,25,300,50,25,25,300,50,25,25,300,50,25,25}));
    CHECK(port.pacedParts==24);
    bool bmwBaud=false; for(auto b:port.bauds) if(b==9600) bmwBaud=true; CHECK(bmwBaud);
    scan.start(80000); size_t sent=port.writes.size(); bool fed=false;
    for(uint32_t now=80000;now<150000 && scan.active();++now) {
        port.now=now; scan.tick(now);
        if(scan.profile()==1 && port.writes.size()>sent && port.writes.back()==std::vector<uint8_t>(echo,echo+sizeof echo) && !fed) {
            port.incoming=std::vector<uint8_t>(echo,echo+sizeof echo);
            port.incoming.insert(port.incoming.end(),kwp,kwp+sizeof kwp); port.readAt=0; fed=true;
        }
    }
    CHECK(fed && scan.results()[1]==Scan::Positive);
    bool infoSent=false;
    for(const auto& b:port.writes) if(b==std::vector<uint8_t>({0x82,0x51,0xF1,0x3C,0,0})) infoSent=true;
    CHECK(infoSent);
    bool faultSent=false;
    for(const auto& b:port.writes) if(b==std::vector<uint8_t>({0x81,0x51,0xF1,0xA1,0x64})) faultSent=true;
    CHECK(faultSent);
    // WTT-derived session keys and OEM addresses must not cross-match.
    const uint8_t fiat[]={0x83,0xF1,0x51,0xC1,0xE9,0x8F,0xFE};
    const uint8_t psa[]={0x83,0xF1,0x01,0xC1,0xD0,0x8F,0x95};
    CHECK(Scan::validateProfile(4,fiat,sizeof fiat,0x81)==Scan::Positive);
    CHECK(Scan::validateProfile(5,psa,sizeof psa,0x81)==Scan::Positive);
    CHECK(Scan::validateProfile(4,psa,sizeof psa,0x81)==Scan::Invalid);
    CHECK(Scan::validateProfile(5,fiat,sizeof fiat,0x81)==Scan::Invalid);
    CHECK(Scan::validateProfile(4,kwp,sizeof kwp,0x81)==Scan::Invalid);
    const uint8_t fiatIdent[]={0x83,0xF1,0x51,0x5A,0x80,0x01,0xA0};
    const uint8_t psaIdent[]={0x83,0xF1,0x01,0x61,0x80,0x01,0x57};
    CHECK(Scan::validateProfile(4,fiatIdent,sizeof fiatIdent,0x1A)==Scan::Positive);
    CHECK(Scan::validateProfile(5,psaIdent,sizeof psaIdent,0x21)==Scan::Positive);
    for(size_t i=0;i<sizeof fiat;++i) {
        std::vector<uint8_t> bad(fiat,fiat+sizeof fiat); bad[i]^=1;
        CHECK(Scan::validateProfile(4,bad.data(),bad.size(),0x81)!=Scan::Positive);
    }
    // Frame-layout fixtures, not captured hardware responses.
    uint8_t bmw[18]={0x6B,16,0x3F,0xA0};
    for(size_t i=0;i<17;++i) bmw[17]^=bmw[i];
    CHECK(Scan::validateProfile(6,bmw,sizeof bmw,0)==Scan::Positive);
    for(size_t i=0;i<18;++i) {
        bmw[i]^=1; CHECK(Scan::validateProfile(6,bmw,sizeof bmw,0)!=Scan::Positive); bmw[i]^=1;
    }
    for(size_t n=1;n<18;++n) CHECK(Scan::validateProfile(6,bmw,n,0)==Scan::Invalid);
    // Inject each verified session fixture only after the complete paced request.
    // Identification is gated by the matching keys; no OEM fault/control writes.
    for(unsigned profile=4;profile<=5;++profile) {
        Capture paced; Scan probe(paced); probe.start(0); bool injected=false,ident=false;
        for(uint32_t now=0;now<70000 && probe.active();++now) {
            paced.now=now; paced.profile=probe.profile(); probe.tick(now);
            if(probe.profile()!=profile || paced.writtenProfile!=profile || paced.writes.empty()) continue;
            const auto& packet=paced.writes.back();
            if(packet.size()==5 && packet[3]==0x81 && !injected) {
                paced.incoming=packet;
                const uint8_t* reply=profile==4?fiat:psa;
                paced.incoming.insert(paced.incoming.end(),reply,reply+7); paced.readAt=0; injected=true;
            }
            if(packet.size()==6 && packet[3]==(profile==4?0x1A:0x21)) {
                CHECK(packet== (profile==4 ? std::vector<uint8_t>({0x82,0x51,0xF1,0x1A,0x80,0x5E}) : std::vector<uint8_t>({0x82,0x01,0xF1,0x21,0x80,0x15})));
                ident=true;
            }
        }
        CHECK(injected && ident && probe.results()[profile]==Scan::Positive);
    }
    scan.start(100000); scan.tick(101500); scan.cancel();
    CHECK(!scan.active() && port.baud==2400 && port.even && !port.observing);
    std::puts("PASS protocol scan: captured checksums/addresses/services, truncation, all profiles bounded, read-only, info after ACK, cancel restores UART");
}
