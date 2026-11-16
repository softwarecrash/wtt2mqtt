// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
struct RxTiming {
    static constexpr unsigned Limit = 256, Samples = 12;
    uint32_t lastAt = 0, longestLow = 0, duration[Samples]{};
    bool level = true, sampleHigh[Samples]{};
    uint16_t edges = 0;
    uint8_t stored = 0;
    void start(uint32_t now, bool high) { *this = RxTiming{}; lastAt = now; level = high; }
    void edge(uint32_t now, bool high) {
        if (high == level || edges >= Limit) return;
        uint32_t dt = now - lastAt;
        if (!level && dt > longestLow) longestLow = dt;
        if (stored < Samples) { duration[stored] = dt; sampleHigh[stored++] = level; }
        ++edges; lastAt = now; level = high;
    }
    void finish(uint32_t now) {
        if (edges < Limit && !level && uint32_t(now - lastAt) > longestLow) longestLow = now - lastAt;
    }
};
