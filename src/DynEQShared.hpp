/*
 * DynEQ - shared constants between the DSP and UI sides.
 *
 * IMPORTANT: both the plugin library and the UI library must agree on the
 * parameter layout. Keep this header included from both sides.
 */

#ifndef DYNEQ_SHARED_HPP_INCLUDED
#define DYNEQ_SHARED_HPP_INCLUDED

#include <cstdint>

static constexpr const int kMaxBands = 8;    // fixed internal slot count
static constexpr const int kDefaultNumBands = 4;

// Per-band parameter block (7 parameters per band)
enum BandParam : int {
    kBypass    = 0,  // 0/1
    kType      = 1,  // 0=Bell, 1=LowShelf, 2=HighShelf
    kEnabled   = 2,  // 0/1
    kFreq      = 3,  // Hz, log range
    kGain      = 4,  // dB, -24..+24 (static target gain)
    kQ         = 5,  // Q factor, 0.1..10
    kDAmount   = 6,  // dynamic amount 0..1 (0 = static EQ, 1 = fully dynamic)
};

static constexpr const int kParamsPerBand = 7;

// Dynamics block (global, applied to every band)
enum GlobalParam : int {
    kThreshold = kMaxBands * kParamsPerBand,   // dB, -60..0
    kRatio     = kThreshold + 1,               // 1..20
    kAttack    = kRatio + 1,                   // ms, 0.1..200
    kRelease   = kAttack + 1,                  // ms, 10..1000
    kRange     = kRelease + 1,                 // dB max reduction, 0..24
    kMix       = kRange + 1,                   // output mix 0..1
    kOutputGain= kMix + 1,                     // dB, -24..+24
    kTotalParams                               // == 64
};

// Filter types
enum FilterType : int {
    kFilterBell = 0,
    kFilterLowShelf = 1,
    kFilterHighShelf = 2,
};

// LV2 state keys (saved with the session)
static constexpr const char* kStateNumBands = "num-bands";
static constexpr const char* kStateBandFmt  = "band-fmt-";   // + index -> CSV of band params

#endif // DYNEQ_SHARED_HPP_INCLUDED
