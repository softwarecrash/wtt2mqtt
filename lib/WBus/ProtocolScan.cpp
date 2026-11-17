// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProtocolScan.h"
namespace wtt {
const char* ProtocolScan::name(unsigned i) {
    static const char* names[]={"W-Bus 2400/8E1 50/50", "TT-C KWP 10400/8N1 25/25", "TT-C KWP 10400/8N1 30/25", "TT-C WTT 10400/8N1 300/50/25/25", "Fiat 10400/8N1 10ms", "PSA 10400/8N1 10ms", "BMW 9600/8E1"};
    return i<Count ? names[i] : "unknown";
}
const char* ProtocolScan::outcome(unsigned i) {
    static const char* names[]={"untested","no_echo","echo_only","invalid_response","negative_response","positive_response","unsupported_transport"};
    return i<=Unsupported ? names[i] : "unknown";
}
ProtocolScan::Outcome ProtocolScan::validate(bool kwp,const uint8_t* b,size_t n,uint8_t service) {
    if (!b || !n) return EchoOnly;
    bool negative=false;
    // Sliding bounded search: tolerate break bytes/noise, but never accept an
    // echo, truncated frame, wrong addresses, checksum or unrelated service.
    for(size_t i=0;i<n;++i) {
        if(kwp) {
            if(n-i<5 || (b[i]&0xC0)!=0x80 || b[i+1]!=0xF1 || b[i+2]!=0x51) continue;
            size_t payload=b[i]&0x3F, len=payload+4;
            if(!payload || len>n-i) continue;
            uint8_t sum=0; for(size_t k=0;k<len-1;++k) sum+=b[i+k];
            if(sum!=b[i+len-1]) continue;
            if(b[i+3]==uint8_t(service+0x40) && (service!=0x81 || payload>=3) && (service!=0x3C || (payload>=2 && b[i+4]==0)) && (service!=0xA1 || payload>=4)) return Positive;
            if(payload==3 && b[i+3]==0x7F && b[i+4]==service) negative=true;
        } else {
            if(n-i<4 || b[i]!=0x4F || b[i+1]<2 || b[i+1]>62) continue;
            size_t len=b[i+1]+2; if(len>n-i) continue;
            if(checkFrame(b+i,len,0x51)>=2 && b[i+3]==service) return Positive;
        }
    }
    return negative ? Negative : Invalid;
}
// Independently implemented from WTT wire-format evidence, not copied code.
ProtocolScan::Outcome ProtocolScan::validateProfile(unsigned profile,const uint8_t* b,size_t n,uint8_t service) {
    if(profile<4) return validate(profile!=0,b,n,service);
    if(!b || !n) return EchoOnly;
    bool negative=false;
    for(size_t i=0;i<n;++i) {
        if(profile==6) {
            if(n-i<18 || b[i]!=0x6B || b[i+1]!=16 || b[i+2]!=0x3F || b[i+3]!=0xA0) continue;
            uint8_t x=0; for(size_t k=0;k<18;++k) x^=b[i+k];
            if(!x) return Positive;
            continue;
        }
        uint8_t ecu=profile==5 ? 0x01:0x51;
        if(n-i<5 || (b[i]&0xC0)!=0x80 || b[i+1]!=0xF1 || b[i+2]!=ecu) continue;
        size_t payload=b[i]&0x3F, len=payload+4;
        if(!payload || len>n-i) continue;
        uint8_t sum=0; for(size_t k=0;k<len-1;++k) sum+=b[i+k];
        if(sum!=b[i+len-1]) continue;
        if(payload==3 && b[i+3]==0x7F && b[i+4]==service) negative=true;
        if(b[i+3]!=uint8_t(service+0x40)) continue;
        if(service==0x81 && payload==3 && b[i+4]==(profile==5?0xD0:0xE9) && b[i+5]==0x8F) return Positive;
        if((service==0x1A || service==0x21) && payload>=3 && b[i+4]==0x80) return Positive;
    }
    return negative ? Negative:Invalid;
}
uint8_t ProtocolScan::service() const {
    if(profile_==0) return step_==0?0x0A:0x0B;
    if(profile_==6) return 0x00;
    if(step_==0) return 0x81;
    if(profile_==4) return 0x1A;
    if(profile_==5) return 0x21;
    return step_==1?0x3C:0xA1;
}
void ProtocolScan::start(uint32_t now) {
    memset(results_,0,sizeof results_); profile_=step_=tries_=0;
    beginProfile(now);
}
void ProtocolScan::beginProfile(uint32_t now) {
    step_=tries_=0; port_.drive(true); phase_=Quiet; since_=now;
}
void ProtocolScan::cancel() {
    if(!active()) return;
    port_.timing("SCAN_WAKE_ABORT",false);
    port_.drive(true); port_.uart(); phase_=Idle;
}
void ProtocolScan::next(uint32_t now) {
    if(report) report(profile_,results_[profile_]);
    if(++profile_>=Count) { profile_=Count-1; cancel(); }
    else beginProfile(now);
}
void ProtocolScan::configureAndSend(uint32_t now) {
    if(!port_.configure(profile_==0?2400:profile_==6?9600:10400,profile_==0 || profile_==6)) {
        results_[profile_]=Unsupported; next(now);
    } else send(now);
}
void ProtocolScan::send(uint32_t now) {
    for(unsigned i=0;i<256 && port_.read()>=0;++i) {}
    rawN_=0;
    if(profile_==0) {
        uint8_t sub=service();
        txN_=size_t(buildFrame(tx_,sizeof tx_,0x51,&sub,1));
    } else if(profile_==6) {
        const uint8_t b[]={0x3F,0x03,0x6B,0x00,0x57}; memcpy(tx_,b,sizeof b); txN_=sizeof b;
    } else {
        tx_[0]=step_==1?0x82:0x81;
        tx_[1]=profile_==5?0x01:0x51; tx_[2]=0xF1; tx_[3]=service();
        txN_=step_==1?6:5;
        if(step_==1) tx_[4]=profile_>=4?0x80:0x00;
        uint8_t sum=0; for(size_t k=0;k<txN_-1;++k) sum+=tx_[k];
        tx_[txN_-1]=sum;
    }
    if(trace) trace("SCAN_TX",tx_,txN_);
    // WTT Fiat/PSA write individual bytes, separated by Sleep(10).
    // Keep RX capture running over the whole packet and collect concurrent echo.
    bool paced=profile_==4 || profile_==5;
    txAt_=paced?1:txN_; port_.write(tx_,txAt_);
    phase_=paced?Transmit:Receive; since_=now;
}
void ProtocolScan::receive() {
    for(unsigned budget=0;budget<128;++budget) {
        int c=port_.read(); if(c<0) break;
        if(rawN_<sizeof raw_) raw_[rawN_++]=uint8_t(c);
    }
}
void ProtocolScan::finishAttempt(uint32_t now) {
    if(trace && rawN_) trace("SCAN_RAW",raw_,rawN_);
    bool echo=rawN_>=txN_ && memcmp(raw_,tx_,txN_)==0;
    Outcome value=rawN_==0 ? NoEcho : validateProfile(profile_,raw_+(echo?txN_:0),rawN_-(echo?txN_:0),service());
    if(attempt) attempt(profile_,service(),value,rawN_,echo);
    if(value>results_[profile_]) results_[profile_]=value;
    if(value==Positive && step_==0 && profile_!=6) { step_=1; phase_=Gap; since_=now; tries_=0; return; }
    if(profile_==0 && step_==0) { step_=1; phase_=Gap; since_=now; return; }
    if(profile_>0 && profile_<4 && step_==1) { step_=2; phase_=Gap; since_=now; return; }
    if(profile_!=0 && step_==0 && value!=Positive && ++tries_<3) { phase_=Gap; since_=now; return; }
    next(now);
}
void ProtocolScan::tick(uint32_t now) {
    switch(phase_) {
    case Idle: return;
    case Quiet: if(elapsed(now,since_,1500)) { port_.drive(false); port_.timing("SCAN_WAKE_LOW",true); phase_=Low; since_=now; } break;
    case Low: if(elapsed(now,since_,profile_==0?50:profile_==1?25:profile_==2?30:300)) {
        port_.timing("SCAN_WAKE_LOW",false); port_.drive(true); port_.timing("SCAN_WAKE_HIGH",true); phase_=High; since_=now;
    } break;
    case High: if(elapsed(now,since_,profile_==0||profile_>=3?50:25)) {
        port_.timing("SCAN_WAKE_HIGH",false);
        if(profile_>=3) { port_.drive(false); port_.timing("SCAN_WAKE_LOW2",true); phase_=ExtraLow; since_=now; }
        else configureAndSend(now);
    } break;
    case ExtraLow: if(elapsed(now,since_,25)) {
        port_.timing("SCAN_WAKE_LOW2",false); port_.drive(true);
        port_.timing("SCAN_WAKE_HIGH2",true); phase_=Settle; since_=now;
    } break;
    case Settle: if(elapsed(now,since_,25)) { port_.timing("SCAN_WAKE_HIGH2",false); configureAndSend(now); } break;
    case Gap: if(elapsed(now,since_,step_ ? 50:1000)) send(now); break;
    case Transmit:
        receive();
        if(elapsed(now,since_,10)) {
            if(txAt_<txN_) port_.writePart(tx_+txAt_++,1);
            else phase_=Receive;
            since_=now;
        }
        break;
    case Receive: {
        receive();
        bool echo=rawN_>=txN_ && memcmp(raw_,tx_,txN_)==0;
        Outcome value=validateProfile(profile_,raw_+(echo?txN_:0),rawN_-(echo?txN_:0),service());
        // Continue a confirmed session promptly rather than waiting the full timeout.
        if(value==Positive || value==Negative || elapsed(now,since_,1000)) finishAttempt(now);
        break;
    }
    }
}
}
