// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eq29 {
constexpr unsigned LegacyBands = 8;
constexpr unsigned MaxBands = 16;
enum Field : unsigned { Enabled, Type, Frequency, Gain, Q, Threshold, Ratio,
                        Attack, Release, Range, Mode, Dynamics, Fields };
enum FilterType : unsigned { Bell, LowShelf, HighShelf };
// Never renumber released LV2 parameters or the first 98 VST input parameters.
constexpr unsigned MeterBase = LegacyBands * Fields;
constexpr unsigned Bypass = MeterBase + LegacyBands;
constexpr unsigned OutputGain = Bypass + 1;
constexpr unsigned SampleRate = OutputGain + 1;
constexpr unsigned ExpandedBandBase = SampleRate + 1;
constexpr unsigned ExtraMeterBase = ExpandedBandBase + (MaxBands-LegacyBands)*Fields;
constexpr unsigned SidechainBase = ExtraMeterBase + MaxBands-LegacyBands;
constexpr unsigned AnalyzerMode = SidechainBase + MaxBands;
constexpr unsigned SpectrumBins = 64;
constexpr unsigned SpectrumInBase = AnalyzerMode+1;
constexpr unsigned SpectrumOutBase = SpectrumInBase+SpectrumBins;
constexpr unsigned ParameterCount = SpectrumOutBase+SpectrumBins;
constexpr unsigned InputCount = MaxBands*Fields+2+MaxBands+1;
constexpr unsigned param(unsigned band, Field field) {
    return band<LegacyBands ? band*Fields+field : ExpandedBandBase+(band-LegacyBands)*Fields+field;
}
constexpr unsigned meter(unsigned band) {
    return band<LegacyBands ? MeterBase+band : ExtraMeterBase+band-LegacyBands;
}
constexpr bool isBandParameter(unsigned i) {
    return i<MeterBase || (i>=ExpandedBandBase && i<ExtraMeterBase);
}
constexpr unsigned parameterBand(unsigned i) {
    return i<MeterBase ? i/Fields : LegacyBands+(i-ExpandedBandBase)/Fields;
}
constexpr unsigned parameterField(unsigned i) {
    return i<MeterBase ? i%Fields : (i-ExpandedBandBase)%Fields;
}
inline double spectrumFrequency(unsigned bin,double rate) {
    return 20*std::pow(std::min(20000.0,rate*.45)/20.0,double(bin)/(SpectrumBins-1));
}
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
    if (isBandParameter(index)) {
        const unsigned field=parameterField(index);
        Spec s = fields[field];
        const unsigned b = parameterBand(index);
        if (field == Enabled) s.def = b < 4 ? 1.f : 0.f;
        if (field == Frequency) {
            constexpr float frequencies[MaxBands] = {100,500,2000,8000,200,1000,4000,12000,60,300,750,1500,3000,6000,10000,16000};
            s.def = frequencies[b];
        }
        if (field == Type) s.def = b == 0 ? LowShelf : b == 3 ? HighShelf : Bell;
        return s;
    }
    if ((index>=MeterBase && index<Bypass) || (index>=ExtraMeterBase && index<SidechainBase)) return {"Dynamic gain", "dynamic_gain", "dB", -24,24,0,false,false};
    if (index == Bypass) return {"Bypass", "bypass", "", 0,1,0,true,false};
    if (index == OutputGain) return {"Output gain", "output_gain", "dB", -24,12,0,false,false};
    if (index>=SidechainBase && index<AnalyzerMode) return {"External sidechain", "external_sidechain", "", 0,1,0,true,false};
    if (index==AnalyzerMode) return {"Spectrum view", "spectrum_view", "", 0,3,3,true,false};
    if (index>=SpectrumInBase) return {"Spectrum", "spectrum", "dB", -100,12,-100,false,false};
    return {"Sample rate", "sample_rate", "Hz", 1000,768000,48000,false,false};
}
constexpr bool isOutput(unsigned i) {
    return (i>=MeterBase && i<Bypass) || i==SampleRate ||
           (i>=ExtraMeterBase && i<SidechainBase) || i>=SpectrumInBase;
}
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
