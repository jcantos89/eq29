// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eq29 {
constexpr unsigned MaxBands = 8;
enum Field : unsigned { Enabled, Type, Frequency, Gain, Q, Threshold, Ratio,
                        Attack, Release, Range, Mode, Dynamics, Fields };
enum FilterType : unsigned { Bell, LowShelf, HighShelf };
constexpr unsigned MeterBase = MaxBands * Fields;
constexpr unsigned Bypass = MeterBase + MaxBands;
constexpr unsigned OutputGain = Bypass + 1;
constexpr unsigned SampleRate = OutputGain + 1;
constexpr unsigned ParameterCount = SampleRate + 1;
constexpr unsigned param(unsigned band, Field field) { return band * Fields + field; }
struct Spec { const char* name; const char* symbol; const char* unit; float min, max, def; bool integer, logarithmic; };
inline Spec spec(unsigned index) {
    static constexpr Spec fields[Fields] = {
        {"Enabled", "enabled", "", 0,1,1,true,false},
        {"Filter type", "type", "", 0,2,0,true,false},
        {"Frequency", "frequency", "Hz", 20,20000,1000,false,true},
        {"Gain", "gain", "dB", -24,24,0,false,false},
        {"Q", "q", "", .15f,12,.70710678f,false,true},
        {"Threshold", "threshold", "dB", -72,0,-24,false,false},
        {"Ratio", "ratio", "", 1,20,4,false,true},
        {"Attack", "attack", "ms", .1f,200,10,false,true},
        {"Release", "release", "ms", 5,2000,150,false,true},
        {"Range", "range", "dB", 0,24,6,false,false},
        {"Mode", "mode", "", 0,1,0,true,false},
        {"Dynamics", "dynamics", "", 0,1,1,true,false}
    };
    if (index < MeterBase) {
        Spec s = fields[index % Fields];
        const unsigned b = index / Fields;
        if (index % Fields == Enabled) s.def = b < 4 ? 1.f : 0.f;
        if (index % Fields == Frequency) {
            constexpr float frequencies[MaxBands] = {100,500,2000,8000,200,1000,4000,12000};
            s.def = frequencies[b];
        }
        if (index % Fields == Type) s.def = b == 0 ? LowShelf : b == 3 ? HighShelf : Bell;
        return s;
    }
    if (index < Bypass) return {"Dynamic gain", "dynamic_gain", "dB", -24,24,0,false,false};
    if (index == Bypass) return {"Bypass", "bypass", "", 0,1,0,true,false};
    if (index == OutputGain) return {"Output gain", "output_gain", "dB", -24,12,0,false,false};
    return {"Sample rate", "sample_rate", "Hz", 1000,768000,48000,false,false};
}
inline bool isOutput(unsigned i) { return (i >= MeterBase && i < Bypass) || i == SampleRate; }
inline float sanitize(unsigned i, float v) {
    const auto s = spec(i);
    if (!std::isfinite(v)) v = s.def;
    v = std::max(s.min, std::min(s.max, v));
    return s.integer ? std::round(v) : v;
}
inline std::array<float, ParameterCount> defaults() {
    std::array<float, ParameterCount> p{};
    for (unsigned i=0; i<ParameterCount; ++i) p[i] = spec(i).def;
    return p;
}
inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
inline double gainToDb(double g) { return 20.0 * std::log10(std::max(g, 1.e-12)); }
}
