/*
 * DynEQ - Dynamic EQ plugin, DSP side.
 *
 * Signal chain per band slot (fixed kMaxBands slots, first `numBands` active):
 *   1. RBJ biquad (Bell / LowShelf / HighShelf) evaluated at the *current*
 *      gain, which is the static target gain modulated by the dynamic
 *      gain-reduction computed from a peak detector fed by the signal
 *      energy *inside that band*.
 *   2. Coefficient updates are smoothed with per-block interpolation to
 *      avoid zipper noise; state variables are reset on discontinuities.
 */

#include "DistrhoPlugin.hpp"
#include "DynEQShared.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

// ---------------------------------------------------------------------------

static const float kPi  = 3.14159265358979323846f;
static const float kLog2 = 0.6931471805599453f;

static inline float db2lin(float db) { return std::pow(10.0f, db * 0.05f); }
static inline float lin2db(float v)  { v = v < 1e-9f ? 1e-9f : v; return 20.0f * std::log(v) / kLog2; }

// ---------------------------------------------------------------------------
// Biquad (transposed direct form II) + RBJ cookbook design formulas

struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;

    // previous & target coefficients, for per-sample smoothing
    float pb0, pb1, pb2, pa1, pa2;
    float tb0, tb1, tb2, ta1, ta2;
    float inc0, inc1, inc2, inca1, inca2;
    int stepsLeft = 0;

    // NOTE: caller must pass already-normalised coefficients (a0 == 1)
    void setCoeffs(float nb0, float nb1, float nb2, float na1, float na2)
    {
        pb0 = b0; pb1 = b1; pb2 = b2; pa1 = a1; pa2 = a2;
        tb0 = nb0; tb1 = nb1; tb2 = nb2; ta1 = na1; ta2 = na2;
        inc0 = (tb0 - pb0); inc1 = (tb1 - pb1); inc2 = (tb2 - pb2);
        inca1 = (ta1 - pa1); inca2 = (ta2 - pa2);
        if (stepsLeft <= 0)
            snap();
    }

    // called at block start: begin interpolation over `n` samples
    void beginBlock(int n)
    {
        if (stepsLeft > 0) // previous ramp not finished -> snap to its target
            snap();
        stepsLeft = n;
        if (n > 0)
        {
            inc0 /= n; inc1 /= n; inc2 /= n; inca1 /= n; inca2 /= n;
        }
    }

    inline float process(float x)
    {
        if (stepsLeft > 0)
        {
            b0 += inc0; b1 += inc1; b2 += inc2; a1 += inca1; a2 += inca2;
            --stepsLeft;
            if (stepsLeft == 0)
            {
                b0 = tb0; b1 = tb1; b2 = tb2; a1 = ta1; a2 = ta2;
                inc0 = inc1 = inc2 = inca1 = inca2 = 0.f;
            }
        }

        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void resetState() { z1 = z2 = 0.f; }

    void snapToTarget() { snap(); }

private:
    void snap()
    {
        b0 = tb0; b1 = tb1; b2 = tb2; a1 = ta1; a2 = ta2;
        inc0 = inc1 = inc2 = inca1 = inca2 = 0.f;
    }
};

// Design helpers -------------------------------------------------------------

struct BandCoeffs
{
    float b0, b1, b2, a1, a2;
};

static BandCoeffs designFilter(int type, float freq, float q, float gainDb, float sampleRate)
{
    // clamp parameters to safe values
    const float fmin = 10.f, fmax = sampleRate * 0.49f;
    if (freq < fmin) freq = fmin;
    if (freq > fmax) freq = fmax;
    if (q < 0.1f) q = 0.1f;
    if (q > 10.f) q = 10.f;
    if (gainDb < -30.f) gainDb = -30.f;
    if (gainDb >  30.f) gainDb =  30.f;

    const float A     = db2lin(gainDb);
    const float w0    = 2.f * kPi * freq / sampleRate;
    const float cw    = std::cos(w0);
    const float sw    = std::sin(w0);
    const float alpha = sw / (2.f * q);
    const float sqA   = std::sqrt(A);

    BandCoeffs c {1.f, 0.f, 0.f, 0.f, 0.f};
    float a0 = 1.f;

    switch (type)
    {
        case kFilterLowShelf:
        {
            // RBJ low shelf
            const float twoAsw = 2.f * sqA * sw;
            c.b0 =      A * ((A + 1.f) - (A - 1.f) * cw + twoAsw);
            c.b1 =  2.f * A * ((A - 1.f) - (A + 1.f) * cw);
            c.b2 =      A * ((A + 1.f) - (A - 1.f) * cw - twoAsw);
            a0   =           (A + 1.f) + (A - 1.f) * cw + twoAsw;
            c.a1 = -2.f *      ((A - 1.f) + (A + 1.f) * cw);
            c.a2 =           (A + 1.f) + (A - 1.f) * cw - twoAsw;
            break;
        }
        case kFilterHighShelf:
        {
            const float twoAsw = 2.f * sqA * sw;
            c.b0 =      A * ((A + 1.f) + (A - 1.f) * cw + twoAsw);
            c.b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cw);
            c.b2 =      A * ((A + 1.f) + (A - 1.f) * cw - twoAsw);
            a0   =           (A + 1.f) - (A - 1.f) * cw + twoAsw;
            c.a1 =  2.f *      ((A - 1.f) - (A + 1.f) * cw);
            c.a2 =           (A + 1.f) - (A - 1.f) * cw - twoAsw;
            break;
        }
        default: // Bell / peaking
        {
            c.b0 = 1.f + alpha * A;
            c.b1 = -2.f * cw;
            c.b2 = 1.f - alpha * A;
            a0   = 1.f + alpha / A;
            c.a1 = -2.f * cw;
            c.a2 = 1.f - alpha / A;
            break;
        }
    }

    const float inv = 1.f / a0;
    c.b0 *= inv; c.b1 *= inv; c.b2 *= inv;
    c.a1 *= inv; c.a2 *= inv;
    return c;
}

// Magnitude response (dB) of arbitrary biquad coefficients at frequency f
static float biquadMagnitudeDb(const BandCoeffs& c, float freq, float sampleRate)
{
    const float w = 2.f * kPi * freq / sampleRate;
    const float cw = std::cos(w), sw = std::sin(w);
    const float c2w = std::cos(2.f * w), s2w = std::sin(2.f * w);

    const float nr = c.b0 + c.b1 * cw + c.b2 * c2w;
    const float ni = -(c.b1 * sw + c.b2 * s2w);
    const float dr = 1.f + c.a1 * cw + c.a2 * c2w;
    const float di = -(c.a1 * sw + c.a2 * s2w);

    const float num = nr * nr + ni * ni;
    const float den = dr * dr + di * di;
    if (den < 1e-20f) return 0.f;
    return 10.f * std::log(num / den) / kLog2;
}

// ---------------------------------------------------------------------------
// Per-band dynamics: envelope follower on the band's own output level

struct BandDynamics
{
    float envDb = -120.f;   // smoothed envelope in dB domain
    float smoothUp = 0.f;   // attack coefficient
    float smoothDown = 0.f; // release coefficient

    void setTimes(float attackMs, float releaseMs, float sampleRate)
    {
        const float fa = 1.f / (attackMs  * 0.001f * sampleRate);
        const float fr = 1.f / (releaseMs * 0.001f * sampleRate);
        smoothUp   = fa >= 1.f ? 1.f : fa;
        smoothDown = fr >= 1.f ? 1.f : fr;
    }

    inline void update(float sample)
    {
        const float absdb = lin2db(std::fabs(sample));
        const float coeff = absdb > envDb ? smoothUp : smoothDown;
        envDb += coeff * (absdb - envDb);
    }

    // returns negative-or-zero gain reduction in dB given threshold/ratio/range
    inline float gainReductionDb(float threshDb, float ratio, float rangeDb) const
    {
        const float over = envDb - threshDb;
        if (over <= 0.f)
            return 0.f;
        float red = over * (1.f - 1.f / ratio);
        if (red > rangeDb) red = rangeDb;
        return -red;
    }
};

// ---------------------------------------------------------------------------

class DynEQPlugin : public Plugin
{
public:
    DynEQPlugin()
        : Plugin(kTotalParams, 0, 1 + kMaxBands)
    {
        std::memset(fSlots, 0, sizeof(fSlots));

        // sensible defaults: 4 bands
        fNumBands = kDefaultNumBands;
        const float defs[kDefaultNumBands][5] = {
            // type, freq, gain, q, dAmount
            { kFilterLowShelf,  100.f,  -2.f, 0.7f, 0.5f },
            { kFilterBell,      800.f,   3.f, 1.0f, 0.8f },
            { kFilterBell,     3000.f,  -4.f, 1.2f, 0.8f },
            { kFilterHighShelf,10000.f,  2.f, 0.7f, 0.5f },
        };
        for (int i = 0; i < kMaxBands; ++i)
        {
            Slot& s = fSlots[i];
            s.enabled = (i < kDefaultNumBands) ? 1.f : 0.f;
            s.bypass  = 0.f;
            if (i < kDefaultNumBands)
            {
                s.type = defs[i][0]; s.freq = defs[i][1];
                s.gain = defs[i][2]; s.q = defs[i][3]; s.amount = defs[i][4];
            }
            else
            {
                s.type = kFilterBell; s.freq = 1000.f;
                s.gain = 0.f; s.q = 1.f; s.amount = 1.f;
            }
        }

        fThreshold = -20.f;
        fRatio = 4.f;
        fAttack = 15.f;
        fRelease = 150.f;
        fRange = 12.f;
        fMix = 1.f;
        fOutputGain = 0.f;

        pushDefaultsToParams();
    }

    ~DynEQPlugin() override = default;

    /* ------------------------------------------------------------------------------------------------
     * Information about the plugin itself */

    const char* getName() const override { return "DynEQ"; }
    const char* getLabel() const override { return "dyneq"; }
    const char* getDescription() const override { return "Dynamic equaliser with per-band dynamics (bell/shelf filters)."; }
    const char* Maker() const override { return "OpenAudio"; }
    const char* getMakerURL() const override { return "https://github.com/openaudio"; }
    const char* getLicense() const override { return "ISC"; }

    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueIdentifier() const override { return 0x44794E71; }

    /* ------------------------------------------------------------------------------------------------
     * Parameters */

    bool isParameterAutomatable(uint32_t index) const override
    {
        return index < (uint32_t)(kMaxBands * kParamsPerBand);
    }

    ParameterHints getParameterHints(uint32_t index) const override
    {
        ParameterHints hints = kParameterIsAutomable;
        const int slot = index / kParamsPerBand;
        if (slot < kMaxBands)
        {
            const int off = index % kParamsPerBand;
            if (off == kBypass || off == kEnabled)
                hints |= kParameterIsBoolean;
            else if (off == kType)
                hints |= kParameterIsInteger;
            else if (off == kFreq)
                hints |= kParameterIsLogarithmic;
        }
        else
        {
            if (index == kRatio) hints |= kParameterIsInteger;
        }
        return hints;
    }

    float getDefaultParameterValue(uint32_t index) const override
    {
        return fParamCache[index];
    }

    float getParameterMinimumValue(uint32_t index) const override
    {
        return paramMin(index);
    }

    float getParameterMaximumValue(uint32_t index) const override
    {
        return paramMax(index);
    }

    const char* getParameterName(uint32_t index) const override
    {
        static const char* globalNames[7] = {
            "Threshold", "Ratio", "Attack", "Release", "Range", "Mix", "Output Gain"
        };
        static char names[kMaxBands][kParamsPerBand][24];
        static bool initDone = false;

        if (!initDone)
        {
            static const char* suffixes[kParamsPerBand] = {
                "Bypass", "Type", "On", "Freq", "Gain", "Q", "Amount"
            };
            for (int i = 0; i < kMaxBands; ++i)
                for (int p = 0; p < kParamsPerBand; ++p)
                    std::snprintf(names[i][p], sizeof(names[i][p]), "B%d %s", i + 1, suffixes[p]);
            initDone = true;
        }

        if (index < (uint32_t)(kMaxBands * kParamsPerBand))
            return names[index / kParamsPerBand][index % kParamsPerBand];
        if (index < kTotalParams)
            return globalNames[index - kMaxBands * kParamsPerBand];
        return "?";
    }

    const char* getParameterSymbol(uint32_t index) const override
    {
        static char symbols[kTotalParams][24];
        static bool initDone = false;
        if (!initDone)
        {
            for (uint32_t i = 0; i < kTotalParams; ++i)
                std::snprintf(symbols[i], sizeof(symbols[i]), "p%02u", i);
            initDone = true;
        }
        return symbols[index];
    }

    const char* getParameterUnit(uint32_t index) const override
    {
        static const char* units[kTotalParams];
        static bool initDone = false;
        if (!initDone)
        {
            for (int i = 0; i < kMaxBands; ++i)
            {
                units[i*kParamsPerBand+0] = nullptr;
                units[i*kParamsPerBand+1] = nullptr;
                units[i*kParamsPerBand+2] = nullptr;
                units[i*kParamsPerBand+3] = "Hz";
                units[i*kParamsPerBand+4] = "dB";
                units[i*kParamsPerBand+5] = nullptr;
                units[i*kParamsPerBand+6] = nullptr;
            }
            units[kThreshold] = "dB";
            units[kRatio] = nullptr;
            units[kAttack] = "ms";
            units[kRelease] = "ms";
            units[kRange] = "dB";
            units[kMix] = nullptr;
            units[kOutputGain] = "dB";
            initDone = true;
        }
        return units[index];
    }

    void setParameter(uint32_t index, float value) override
    {
        if (index >= kTotalParams)
            return;

        const float lo = paramMin(index), hi = paramMax(index);
        if (value < lo) value = lo;
        if (value > hi) value = hi;
        fParamCache[index] = value;

        const int slot = index / kParamsPerBand;
        if (slot < kMaxBands)
        {
            Slot& s = fSlots[slot];
            switch (index % kParamsPerBand)
            {
                case kBypass:  s.bypass = value; break;
                case kType:    s.type = value; break;
                case kEnabled: s.enabled = value; break;
                case kFreq:    s.freq = value; break;
                case kGain:    s.gain = value; break;
                case kQ:       s.q = value; break;
                case kDAmount: s.amount = value; break;
            }
        }
        else
        {
            switch (index)
            {
                case kThreshold:  fThreshold = value; break;
                case kRatio:      fRatio = value; break;
                case kAttack:     fAttack = value; break;
                case kRelease:    fRelease = value; break;
                case kRange:      fRange = value; break;
                case kMix:        fMix = value; break;
                case kOutputGain: fOutputGain = value; break;
            }
        }

        fParamsDirty = true;
    }

    /* ------------------------------------------------------------------------------------------------
     * State (band layout persistence) */

    void initState(uint32_t index, State& state) override
    {
        if (index == 0)
        {
            state.hints = kStateIsHostReadable | kStateIsHostWritable;
            state.key = kStateNumBands;
            state.defaultValue = "4";
            state.label = "Number of bands";
        }
        else
        {
            state.hints = kStateIsHostReadable | kStateIsHostWritable;
            state.key = String(kStateBandFmt) + String(index - 1);
            state.defaultValue = "";
            state.label = String("Band ") + String(index);
        }
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateNumBands) == 0)
        {
            int n = std::atoi(value);
            if (n < 1) n = 1;
            if (n > kMaxBands) n = kMaxBands;
            fNumBands = n;
            fParamsDirty = true;
            return;
        }

        if (std::strncmp(key, kStateBandFmt, std::strlen(kStateBandFmt)) == 0)
        {
            const int idx = std::atoi(key + std::strlen(kStateBandFmt));
            if (idx < 0 || idx >= kMaxBands)
                return;
            Slot& s = fSlots[idx];
            float type, freq, gain, q, amount, enabled, bypass;
            if (std::sscanf(value, "%f,%f,%f,%f,%f,%f,%f",
                            &type, &freq, &gain, &q, &amount, &enabled, &bypass) == 7)
            {
                s.type = type; s.freq = freq; s.gain = gain; s.q = q;
                s.amount = amount; s.enabled = enabled; s.bypass = bypass;
                // refresh parameter cache so UI/host sees restored values
                fParamCache[idx*kParamsPerBand+0] = bypass;
                fParamCache[idx*kParamsPerBand+1] = type;
                fParamCache[idx*kParamsPerBand+2] = enabled;
                fParamCache[idx*kParamsPerBand+3] = freq;
                fParamCache[idx*kParamsPerBand+4] = gain;
                fParamCache[idx*kParamsPerBand+5] = q;
                fParamCache[idx*kParamsPerBand+6] = amount;
                fParamsDirty = true;
            }
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateNumBands) == 0)
            return String((int)fNumBands);

        if (std::strncmp(key, kStateBandFmt, std::strlen(kStateBandFmt)) == 0)
        {
            const int idx = std::atoi(key + std::strlen(kStateBandFmt));
            if (idx >= 0 && idx < kMaxBands)
            {
                const Slot& s = fSlots[idx];
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%g,%g,%g,%g,%g,%g,%g",
                              (double)s.type, (double)s.freq, (double)s.gain,
                              (double)s.q, (double)s.amount, (double)s.enabled,
                              (double)s.bypass);
                return String(buf);
            }
        }
        return String();
    }

    /* ------------------------------------------------------------------------------------------------
     * Audio */

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float sr = getSampleRate();

        // update dynamics times once per block
        for (int i = 0; i < kMaxBands; ++i)
            fDyn[i].setTimes(fAttack, fRelease, sr);

        // ---- prepare per-slot coefficient targets --------------------------
        for (int i = 0; i < kMaxBands; ++i)
        {
            Slot& s = fSlots[i];
            const bool active = (i < fNumBands) && s.enabled >= 0.5f;
            if (!active)
            {
                // identity filter target
                s.filter.setCoeffs(1.f, 0.f, 0.f, 0.f, 0.f);
                continue;
            }
            const float g = currentBandGainDb(i);
            const BandCoeffs c = designFilter((int)s.type, s.freq, s.q, g, sr);
            s.filter.setCoeffs(c.b0, c.b1, c.b2, c.a1, c.a2);
        }

        const bool dryWet = (fMix < 0.999f);
        const float outLin = db2lin(fOutputGain);

        // copy input to output if passthrough needed
        if (outputs[0] != inputs[0])
            std::memcpy(outputs[0], inputs[0], frames * sizeof(float));
        if (DISTRHO_PLUGIN_NUM_OUTPUTS > 1 && outputs[1] != inputs[1])
            std::memcpy(outputs[1], inputs[1], frames * sizeof(float));

        float* l = outputs[0];
        float* r = outputs[1];

        // per-block sub-ramp of coefficients
        for (int i = 0; i < kMaxBands; ++i)
            fSlots[i].filter.beginBlock(frames);

        for (uint32_t f = 0; f < frames; ++f)
        {
            const float xl = l[f], xr = r[f];
            float ol = xl, orr = xr;

            for (int i = 0; i < kMaxBands; ++i)
            {
                Slot& s = fSlots[i];
                if (i >= fNumBands || s.enabled < 0.5f)
                    continue;
                if (s.bypass >= 0.5f)
                {
                    // still track the band level so re-enable is smooth
                    continue;
                }
                const float bl = s.filter.process(ol);
                const float br = s.filter.process(orr);
                // band level detection uses the filtered (in-band) signal
                fDyn[i].update(0.5f * (std::fabs(bl) + std::fabs(br)));
                ol = bl; orr = br;
            }

            if (dryWet)
            {
                l[f] = xl + (ol - xl) * fMix;
                r[f] = xr + (orr - xr) * fMix;
            }
            else
            {
                l[f] = ol;
                r[f] = orr;
            }
            l[f] *= outLin;
            r[f] *= outLin;
        }

        // snapshot current gains for the next block
        for (int i = 0; i < kMaxBands; ++i)
            fSlots[i].lastAppliedGain = currentBandGainDb(i);

        fParamsDirty = false;
    }

    void activateState() override
    {
        resetAll();
    }

    void deactivateState() override
    {
        resetAll();
    }

    void changeSampleRate() override
    {
        resetAll();
    }

    /* ------------------------------------------------------------------------------------------------
     * Helpers exposed for potential future use (also used by tests) */

    // Total response magnitude in dB at freq, including the *current* dynamic
    // gain reduction of each band (used by the UI to draw the shaded area).
    float responseDb(float freq, bool includeDynamics) const
    {
        const float sr = getSampleRate();
        float total = 0.f;
        for (int i = 0; i < fNumBands; ++i)
        {
            const Slot& s = fSlots[i];
            if (s.enabled < 0.5f || s.bypass >= 0.5f)
                continue;
            const float g = includeDynamics ? currentBandGainDb(i) : s.gain;
            const BandCoeffs c = designFilter((int)s.type, s.freq, s.q, g, sr);
            total += biquadMagnitudeDb(c, freq, sr);
        }
        total += fOutputGain;
        return total;
    }

    // compute the gain actually applied to a band right now:
    // static gain blended toward (static + reduction) by the dynamic amount
    float currentBandGainDb(int i) const
    {
        const Slot& s = fSlots[i];
        const float red = fDyn[i].gainReductionDb(fThreshold, fRatio, fRange);
        return s.gain + s.amount * red;
    }

private:
    struct Slot
    {
        float type = kFilterBell;
        float freq = 1000.f;
        float gain = 0.f;
        float q = 1.f;
        float amount = 1.f;
        float enabled = 0.f;
        float bypass = 0.f;
        Biquad filter;
    };

    Slot fSlots[kMaxBands];
    BandDynamics fDyn[kMaxBands];
    float fParamCache[kTotalParams];

    int fNumBands = kDefaultNumBands;
    float fThreshold, fRatio, fAttack, fRelease, fRange, fMix, fOutputGain;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynEQPlugin)

    void resetAll()
    {
        for (int i = 0; i < kMaxBands; ++i)
        {
            fSlots[i].filter.resetState();
            fDyn[i].envDb = -120.f;
        }
    }

    static float paramMin(uint32_t index)
    {
        const int slot = index / kParamsPerBand;
        if (slot < kMaxBands)
        {
            switch (index % kParamsPerBand)
            {
                case kBypass:  return 0.f;
                case kType:    return 0.f;
                case kEnabled: return 0.f;
                case kFreq:    return 20.f;
                case kGain:    return -24.f;
                case kQ:       return 0.1f;
                case kDAmount: return 0.f;
            }
        }
        switch (index)
        {
            case kThreshold:  return -60.f;
            case kRatio:      return 1.f;
            case kAttack:     return 0.1f;
            case kRelease:    return 10.f;
            case kRange:      return 0.f;
            case kMix:        return 0.f;
            case kOutputGain: return -24.f;
        }
        return 0.f;
    }

    static float paramMax(uint32_t index)
    {
        const int slot = index / kParamsPerBand;
        if (slot < kMaxBands)
        {
            switch (index % kParamsPerBand)
            {
                case kBypass:  return 1.f;
                case kType:    return 2.f;
                case kEnabled: return 1.f;
                case kFreq:    return 20000.f;
                case kGain:    return 24.f;
                case kQ:       return 10.f;
                case kDAmount: return 1.f;
            }
        }
        switch (index)
        {
            case kThreshold:  return 0.f;
            case kRatio:      return 20.f;
            case kAttack:     return 200.f;
            case kRelease:    return 1000.f;
            case kRange:      return 24.f;
            case kMix:        return 1.f;
            case kOutputGain: return 24.f;
        }
        return 1.f;
    }

    void pushDefaultsToParams()
    {
        for (int i = 0; i < kMaxBands; ++i)
        {
            const Slot& s = fSlots[i];
            fParamCache[i*kParamsPerBand+0] = s.bypass;
            fParamCache[i*kParamsPerBand+1] = s.type;
            fParamCache[i*kParamsPerBand+2] = s.enabled;
            fParamCache[i*kParamsPerBand+3] = s.freq;
            fParamCache[i*kParamsPerBand+4] = s.gain;
            fParamCache[i*kParamsPerBand+5] = s.q;
            fParamCache[i*kParamsPerBand+6] = s.amount;
        }
        fParamCache[kThreshold] = fThreshold;
        fParamCache[kRatio] = fRatio;
        fParamCache[kAttack] = fAttack;
        fParamCache[kRelease] = fRelease;
        fParamCache[kRange] = fRange;
        fParamCache[kMix] = fMix;
        fParamCache[kOutputGain] = fOutputGain;
    }
};

// ---------------------------------------------------------------------------

Plugin* createPlugin() { return new DynEQPlugin(); }

// ---------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
