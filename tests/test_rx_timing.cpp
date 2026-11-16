// SPDX-License-Identifier: GPL-3.0-or-later
#include "RxTiming.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
    RxTiming t; t.start(100,true); t.edge(200,false); t.edge(1450,true); t.finish(750000);
    CHECK(t.edges==2 && t.longestLow==1250 && t.stored==2);
    CHECK(t.sampleHigh[0] && t.duration[0]==100 && !t.sampleHigh[1] && t.duration[1]==1250);
    t.start(0,true); t.edge(10,false); t.edge(25010,true); t.finish(750000); CHECK(t.longestLow==25000);
    t.start(0,false); t.finish(750000); CHECK(t.edges==0 && t.longestLow==750000);
    t.start(0,true); t.finish(750000); CHECK(t.edges==0 && t.longestLow==0);
    t.start(0xfffffff0u,false); t.edge(0x10u,true); CHECK(t.longestLow==32);
    t.start(0,true); for(unsigned i=1;i<400;++i) t.edge(i*10,(i%2)==0);
    CHECK(t.edges==RxTiming::Limit && t.stored==RxTiming::Samples);
    std::puts("PASS RX timing: data pulses, long low, static levels, counter wrap and bounded capture");
}
