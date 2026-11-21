// SPDX-License-Identifier: GPL-3.0-or-later
#include "PsaKwp.h"

namespace wtt {
namespace {
constexpr uint8_t dids[PsaKwp::ReadCount] = {
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCC,0xCD,0xCE,0xA0
};
// WTT's PSA coolant thermistor lookup: temperature °C, then raw ECU byte.
constexpr int16_t tempC[] = {-40,-35,-30,-25,-20,-15,-10,-5,0,5,10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,95,100,105,110,115,120,125,130,135};
constexpr uint8_t tempRaw[] = {241,240,238,236,234,230,227,222,217,211,204,196,187,177,167,156,145,134,123,112,102,92,83,75,67,60,53,47,42,38,34,30,27,24,21,19};
int rawSlot(uint8_t did) {
    for (uint8_t i=0;i<PsaKwp::ReadCount;++i) if(dids[i]==did) return i;
    return -1;
}
uint8_t commonFaultCode(uint16_t psaCode) {
    switch(psaCode & 0x0FFF) {
    case 0x001:return 0x01; case 0x002:return 0x02; case 0x003:return 0x03;
    case 0x004:return 0xFF; case 0x005:return 0xFF; case 0x006:return 0x05;
    case 0x013:return 0x83;
    case 0x608:return 0x08; case 0x609:return 0x09; case 0x60A:return 0x0A;
    case 0x60B:return 0x0B; case 0x60C:return 0x13;
    case 0x818:return 0x88; case 0x819:return 0x89; case 0x81A:return 0x8A; case 0x81B:return 0x8B;
    case 0xA05:return 0x84; case 0xC04:return 0x04; case 0xC07:return 0x06;
    default:return 0xFF;
    }
}
}

void PsaKwp::begin(uint32_t now) {
    phase_=Gap; step_=0; since_=now; rxSize_=txSize_=txAt_=0; lastFaultPoll_=now-60000;
}
void PsaKwp::pause() {
    phase_=Idle; rxSize_=txSize_=txAt_=0;
}
void PsaKwp::send(uint32_t now) {
    uint8_t pdu[3]{}; size_t pduSize;
    if(step_==FaultStep) { pdu[0]=0x17; pdu[1]=0xFF; pdu[2]=0x00; pduSize=3; }
    else { pdu[0]=0x21; pdu[1]=dids[step_]; pduSize=2; }
    tx_[0]=uint8_t(0x80+pduSize); tx_[1]=0x01; tx_[2]=0xF1;
    for(size_t i=0;i<pduSize;++i) tx_[3+i]=pdu[i];
    uint8_t sum=0; for(size_t i=0;i<3+pduSize;++i) sum=uint8_t(sum+tx_[i]);
    tx_[3+pduSize]=sum; txSize_=4+pduSize; txAt_=1; rxSize_=0;
    if(trace) trace("PSA_TX",tx_,txSize_);
    port_.write(tx_,1); phase_=Transmit; since_=now;
}

void PsaKwp::finish(uint32_t now, Snapshot& state, bool valid, const uint8_t* pdu, size_t size) {
    bool faultRequest=step_==FaultStep;
    if(valid && pdu && size>=2) {
        if(step_==FaultStep) {
            if(pdu[0]==0x57 && pdu[1]<=12 && size==size_t(2+pdu[1]*5)) {
                state.psaFaultCount=pdu[1];
                for(uint8_t i=0;i<pdu[1];++i) {
                    size_t o=2+size_t(i)*5;
                    state.psaFaultCodes[i]=uint16_t((uint16_t(pdu[o])<<8)|pdu[o+1]);
                    state.psaFaultCommonCodes[i]=commonFaultCode(state.psaFaultCodes[i]);
                    state.psaFaultExtra[i][0]=pdu[o+2];
                    state.psaFaultExtra[i][1]=pdu[o+3];
                    state.psaFaultExtra[i][2]=pdu[o+4];
                }
                state.psaFaultsValid=true; state.psaFaultAt=now;
            } else valid=false;
        } else {
            uint8_t did=dids[step_]; int slot=rawSlot(did);
            if(pdu[0]!=0x61 || pdu[1]!=did || slot<0) valid=false;
            else if(did==0xCC || did==0xCD || did==0xCE) {
                if(size!=4) valid=false;
                else { state.psaRaw[slot]=pdu[2]; state.psaRaw2[slot]=pdu[3]; state.psaValidMask |= uint16_t(1u<<slot); }
            } else {
                if(size!=3) valid=false;
                else {
                    state.psaRaw[slot]=pdu[2];
                    if(did==0xC0) {
                        if(pdu[2]<19 || pdu[2]>241) valid=false;
                        else {
                            float value=tempC[0]; bool found=false;
                            for(size_t i=0;i+1<sizeof(tempC);++i) {
                                if(pdu[2]<=tempRaw[i] && pdu[2]>=tempRaw[i+1]) {
                                    float t=float(tempRaw[i]-pdu[2])/float(tempRaw[i]-tempRaw[i+1]);
                                    value=float(tempC[i]) + t*float(tempC[i+1]-tempC[i]); found=true; break;
                                }
                            }
                            if(!found && pdu[2]==tempRaw[sizeof(tempRaw)-1]) value=tempC[sizeof(tempC)-1];
                            state.psaTemperatureC=value; state.psaMeasurementsValid=true; state.psaMeasurementAt=now;
                        }
                    } else if(did==0xC1) {
                        // WTT scales this input byte by 0x1AAA / 100, then by 0.001.
                        uint32_t millivolts=(uint32_t(pdu[2])*0x1AAA+50)/100;
                        state.psaSupplyVoltage=float(millivolts)*0.001f;
                        state.psaMeasurementsValid=true; state.psaMeasurementAt=now;
                    }
                    if(valid) state.psaValidMask |= uint16_t(1u<<slot);
                }
            }
        }
    }
    if(valid) { state.psaOnline=true; state.psaAt=now; ++state.psaSuccesses; }
    else { ++state.psaFailures; if(elapsed(now,state.psaAt,15000)) state.psaOnline=false; }
    step_=uint8_t((step_+1)%TotalSteps);
    if(faultRequest) lastFaultPoll_=now;
    phase_=Gap; since_=now; txSize_=txAt_=rxSize_=0;
}

bool PsaKwp::parseFrame(const uint8_t* frame, size_t n, Snapshot& state) {
    if(n<5 || (frame[0]&0xC0)!=0x80 || frame[1]!=0xF1 || frame[2]!=0x01) return false;
    size_t payload=frame[0]&0x3F, frameSize=payload+4;
    if(!payload || frameSize!=n) return false;
    uint8_t sum=0; for(size_t i=0;i<n-1;++i) sum=uint8_t(sum+frame[i]);
    if(sum!=frame[n-1]) return false;
    if(trace) trace("PSA_RX",frame,n);
    const uint8_t* pdu=frame+3;
    size_t pduSize=payload;
    if(pdu[0]==0x7F && pduSize>=3 && pdu[1]==(step_==FaultStep?0x17:0x21)) {
        finish(millis(),state,false); return true;
    }
    bool valid=false;
    if(step_==FaultStep) valid=pdu[0]==0x57 && pduSize>=2 && pdu[1]<=12 && pduSize==size_t(2+pdu[1]*5);
    else valid=pduSize>=2 && pdu[0]==0x61 && pdu[1]==dids[step_];
    if(valid) finish(millis(),state,true,pdu,pduSize);
    return valid;
}

void PsaKwp::receive(uint32_t now, Snapshot& state) {
    for(unsigned budget=0;budget<128;++budget) {
        int c=port_.read(); if(c<0) break;
        if(rxSize_<sizeof rx_) rx_[rxSize_++]=uint8_t(c);
    }
    size_t off=0;
    if(rxSize_>=txSize_ && txSize_ && memcmp(rx_,tx_,txSize_)==0) off=txSize_;
    while(rxSize_-off>=4) {
        if((rx_[off]&0xC0)!=0x80 || rx_[off+1]!=0xF1 || rx_[off+2]!=0x01) { ++off; continue; }
        size_t frameSize=size_t(rx_[off]&0x3F)+4;
        if(frameSize>sizeof rx_) { ++off; continue; }
        if(rxSize_-off<frameSize) break;
        if(parseFrame(rx_+off,frameSize,state)) return;
        off+=frameSize;
    }
    if(elapsed(now,since_,1000)) finish(now,state,false);
}

void PsaKwp::tick(uint32_t now, Snapshot& state, bool enabled) {
    if(!enabled) return;
    if(state.psaOnline && elapsed(now,state.psaAt,15000)) state.psaOnline=false;
    if(phase_==Idle) begin(now);
    switch(phase_) {
    case Idle: break;
    case Gap:
        if(elapsed(now,since_,50)) {
            if(step_==FaultStep && !elapsed(now,lastFaultPoll_,60000)) step_=0;
            send(now);
        }
        break;
    case Transmit:
        if(elapsed(now,since_,10)) {
            if(txAt_<txSize_) port_.writePart(tx_+txAt_++,1);
            else phase_=Receive;
            since_=now;
        }
        receive(now,state); break;
    case Receive: receive(now,state); break;
    }
}
}
