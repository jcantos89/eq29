/*
 * DynEQ - Dynamic EQ plugin, UI side (Cairo).
 *
 * Layout:
 *   +--------------------------------------------------+
 *   |  EQ graph (draggable nodes, live response)      |
 *   |   - solid curve = static response               |
 *   |   - shaded area = dynamic gain range            |
 *   +--------------------------------------------------+
 *   | [B1][B2].. [+][-]   band selector / add-remove  |
 *   | Type buttons | Freq/Gain/Q/Amount sliders       |
 *   | Dynamics panel: Threshold/Ratio/Attack/         |
 *   |                 Release/Range + Mix/OutGain     |
 *   +--------------------------------------------------+
 *
 * Interaction:
 *   - left-drag on a node: frequency (x) / gain (y)
 *   - right-drag on a node: Q factor
 *   - double-click a node: cycle filter type
 *   - wheel over the graph: select band
 *   - "+" adds a band (up to kMaxBands), "-" removes the last one
 *
 * The UI keeps a mirror of every plugin parameter (fParams) plus the band
 * count (mirrored from the "num-bands" state key), and computes the response
 * curves locally with the same RBJ formulas used by the DSP.
 */

#include "DistrhoUI.hpp"
#include "DynEQShared.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

// ---------------------------------------------------------------------------
// Local helpers (frequency/gain mappings and biquad math, mirroring the DSP)

static const double kPi    = 3.14159265358979323846;
static const double kLog2  = 0.6931471805599453;
static const double kFreqLo = 20.0;
static const double kFreqHi = 20000.0;
static const double kGainRange = 24.0; // +- dB shown on the graph

static inline double db2lin(double db) { return std::pow(10.0, db * 0.05); }

// x in [0,1] -> Hz (log scale)
static inline double normToFreq(double t)
{
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return kFreqLo * std::pow(kFreqHi / kFreqLo, t);
}

static inline double freqToNorm(double f)
{
    if (f < kFreqLo) f = kFreqLo;
    if (f > kFreqHi) f = kFreqHi;
    return std::log(f / kFreqLo) / std::log(kFreqHi / kFreqLo);
}

// y in [0,1] (top = +gain) -> dB
static inline double normToGain(double t) { return (1.0 - 2.0 * t) * kGainRange; }
static inline double gainToNorm(double g)  { return 0.5 - g / (2.0 * kGainRange); }

struct Coeffs { double b0, b1, b2, a1, a2; };

// Same design formulas as the DSP (RBJ cookbook), double precision for drawing
static Coeffs designFilter(int type, double freq, double q, double gainDb, double sampleRate)
{
    if (freq < 10.0) freq = 10.0;
    if (freq > sampleRate * 0.49) freq = sampleRate * 0.49;
    if (q < 0.1) q = 0.1;
    if (q > 10.0) q = 10.0;
    if (gainDb < -30.0) gainDb = -30.0;
    if (gainDb >  30.0) gainDb =  30.0;

    const double A     = db2lin(gainDb);
    const double w0    = 2.0 * kPi * freq / sampleRate;
    const double cw    = std::cos(w0);
    const double sw    = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double sqA   = std::sqrt(A);

    Coeffs c {1.0, 0.0, 0.0, 0.0, 0.0};
    double a0 = 1.0;

    switch (type)
    {
        case kFilterLowShelf:
        {
            const double twoAsw = 2.0 * sqA * sw;
            c.b0 =     A * ((A + 1.0) - (A - 1.0) * cw + twoAsw);
            c.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
            c.b2 =     A * ((A + 1.0) - (A - 1.0) * cw - twoAsw);
            a0   =          (A + 1.0) + (A - 1.0) * cw + twoAsw;
            c.a1 = -2.0 *     ((A - 1.0) + (A + 1.0) * cw);
            c.a2 =          (A + 1.0) + (A - 1.0) * cw - twoAsw;
            break;
        }
        case kFilterHighShelf:
        {
            const double twoAsw = 2.0 * sqA * sw;
            c.b0 =     A * ((A + 1.0) + (A - 1.0) * cw + twoAsw);
            c.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
            c.b2 =     A * ((A + 1.0) + (A - 1.0) * cw - twoAsw);
            a0   =          (A + 1.0) - (A - 1.0) * cw + twoAsw;
            c.a1 =  2.0 *     ((A - 1.0) - (A + 1.0) * cw);
            c.a2 =          (A + 1.0) - (A - 1.0) * cw - twoAsw;
            break;
        }
        default: // bell
        {
            c.b0 = 1.0 + alpha * A;
            c.b1 = -2.0 * cw;
            c.b2 = 1.0 - alpha * A;
            a0   = 1.0 + alpha / A;
            c.a1 = -2.0 * cw;
            c.a2 = 1.0 - alpha / A;
            break;
        }
    }

    const double inv = 1.0 / a0;
    c.b0 *= inv; c.b1 *= inv; c.b2 *= inv;
    c.a1 *= inv; c.a2 *= inv;
    return c;
}

static double magnitudeDb(const Coeffs& c, double freq, double sampleRate)
{
    const double w  = 2.0 * kPi * freq / sampleRate;
    const double cw = std::cos(w),  sw = std::sin(w);
    const double c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);

    const double nr = c.b0 + c.b1 * cw + c.b2 * c2;
    const double ni = -(c.b1 * sw + c.b2 * s2);
    const double dr = 1.0 + c.a1 * cw + c.a2 * c2;
    const double di = -(c.a1 * sw + c.a2 * s2);

    const double num = nr * nr + ni * ni;
    const double den = dr * dr + di * di;
    if (den < 1e-20) return 0.0;
    return 10.0 * std::log(num / den) / kLog2;
}

// ---------------------------------------------------------------------------

class DynEGUI : public UI, public IdleCallback
{
public:
    DynEGUI()
    {
        std::memset(fParams, 0, sizeof(fParams));
        loadDefaults();

        setGeometryConstraints(640, 400, true);
        setSize(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT);

        // animate the meter dots / smooth curve updates at ~30 fps
        addIdleCallback(this, 33);
    }

    ~DynEGUI() override
    {
        removeIdleCallback(this);
    }

protected:
    /* ------------------------------------------------------------------------------------------------
     * Plugin -> UI synchronization */

    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index < kTotalParams)
        {
            fParams[index] = value;
            repaint();
        }
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateNumBands) == 0 && value != nullptr)
        {
            int n = std::atoi(value);
            if (n < 1) n = 1;
            if (n > kMaxBands) n = kMaxBands;
            fNumBands = n;
            if (fSelected >= fNumBands)
                fSelected = fNumBands - 1;
            repaint();
        }
    }

    /* ------------------------------------------------------------------------------------------------
     * Drawing */

    void onCairoDisplay(const CairoGraphicsContext& context) override
    {
        cairo_t* const cr = context.handle;

        const double w = getWidth();
        const double h = getHeight();

        // background
        cairo_set_source_rgb(cr, 0.11, 0.12, 0.14);
        cairo_paint(cr);

        // ---- graph area --------------------------------------------------
        const GraphArea ga = computeGraphArea(w, h);

        drawGrid(cr, ga);
        drawCurves(cr, ga);
        drawNodes(cr, ga);
        drawLabels(cr, ga);

        // ---- bottom control panel ----------------------------------------
        drawBandSelector(cr, ga);
        drawSliders(cr, ga, w, h);
    }

    /* ------------------------------------------------------------------------------------------------
     * Animation */

    void idleCallback() override
    {
        // advance the animation tick and repaint so the dynamic shading moves
        fAnimTick++;
        repaint();
    }

    void uiIdle() override
    {
    }

    /* ------------------------------------------------------------------------------------------------
     * Mouse handling */

    bool onMouse(const MouseEvent& ev) override
    {
        const GraphArea ga = computeGraphArea(getWidth(), getHeight());
        const double x = ev.pos.getX(), y = ev.pos.getY();

        if (ev.button == 1 && ev.press)
        {
            // +/- buttons?
            int id;
            if ((id = hitButton(x, y, ga)) != 0)
            {
                if (id == 1) addBand();
                else         removeBand();
                return true;
            }

            // band selector tabs?
            const int tab = hitTab(x, y, ga);
            if (tab >= 0)
            {
                fSelected = tab;
                repaint();
                return true;
            }

            // slider drag start?
            const int sl = hitSlider(x, y, ga);
            if (sl >= 0)
            {
                fDragging = kDragSlider;
                fDragSlider = sl;
                editParameter(paramForSlider(sl), true);
                updateSliderFromPos(x, y, ga);
                return true;
            }

            // node hit?
            const int node = hitNode(x, y, ga);
            if (node >= 0)
            {
                fSelected = node;
                fDragging = kDragNode;
                fDragBand = node;
                fDragRight = false;
                requestMouseFocus(true);
                editParameter(bandParam(node, kFreq), true);
                repaint();
                return true;
            }

            // empty space inside graph: create a band there if room
            if (inRect(x, y, ga.x, ga.y, ga.w, ga.h) && fNumBands < kMaxBands)
            {
                addBandAt(normToFreq((x - ga.x) / ga.w), normToGain((y - ga.y) / ga.h));
                return true;
            }
        }
        else if (ev.button == 3 && ev.press)
        {
            const int node = hitNode(x, y, ga);
            if (node >= 0)
            {
                fSelected = node;
                fDragging = kDragNode;
                fDragBand = node;
                fDragRight = true;
                requestMouseFocus(true);
                editParameter(bandParam(node, kQ), true);
                return true;
            }
        }
        else if (ev.button == 1 && !ev.press)
        {
            if (fDragging == kDragNode)
            {
                editParameter(bandParam(fDragBand, fDragRight ? kQ : kFreq), false);
                editParameter(bandParam(fDragBand, kGain), false);
            }
            else if (fDragging == kDragSlider)
            {
                editParameter(paramForSlider(fDragSlider), false);
            }
            fDragging = kDragNone;
            requestMouseFocus(false);
            return true;
        }

        // double click: cycle filter type on the node under cursor
        if (ev.button == 1 && ev.press && ev.clicks == 2)
        {
            const int node = hitNode(x, y, ga);
            if (node >= 0)
            {
                int t = (int)(bandParamValue(node, kType) + 0.5f);
                t = (t + 1) % 3;
                setBandParam(node, kType, (float)t);
                repaint();
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (fDragging == kDragNone)
            return false;

        const GraphArea ga = computeGraphArea(getWidth(), getHeight());
        const double x = ev.pos.getX(), y = ev.pos.getY();

        if (fDragging == kDragNode)
        {
            if (fDragRight)
            {
                // vertical drag with right button -> Q (log-ish mapping)
                double t = 1.0 - (y - ga.y) / ga.h;
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                const double q = 0.1 * std::pow(100.0, t); // 0.1 .. 10
                setBandParam(fDragBand, kQ, (float)q);
            }
            else
            {
                double ft = (x - ga.x) / ga.w;
                double gt = (y - ga.y) / ga.h;
                if (ft < 0.0) ft = 0.0;
                if (ft > 1.0) ft = 1.0;
                if (gt < 0.0) gt = 0.0;
                if (gt > 1.0) gt = 1.0;
                setBandParam(fDragBand, kFreq, (float)normToFreq(ft));
                setBandParam(fDragBand, kGain, (float)normToGain(gt));
            }
            repaint();
            return true;
        }

        if (fDragging == kDragSlider)
        {
            updateSliderFromPos(x, y, ga);
            return true;
        }

        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const GraphArea ga = computeGraphArea(getWidth(), getHeight());
        const double x = ev.pos.getX(), y = ev.pos.getY();

        if (!inRect(x, y, ga.x, ga.y, ga.w, ga.h))
            return false;

        const double dir = (ev.delta.getY() != 0.0) ? ev.delta.getY() : 1.0;
        int sel = fSelected + (dir > 0.0 ? -1 : 1);
        if (sel < 0) sel = 0;
        if (sel >= fNumBands) sel = fNumBands - 1;
        if (sel != fSelected)
        {
            fSelected = sel;
            repaint();
        }
        return true;
    }

private:
    /* ------------------------------------------------------------------------------------------------
     * Geometry helpers */

    struct GraphArea
    {
        double x, y, w, h;      // graph rect
        double panelY;          // top of the control panel
        double tabX, tabY, tabW, tabH; // band tabs row
    };

    GraphArea computeGraphArea(double winW, double winH) const
    {
        GraphArea ga;
        ga.x = 12;
        ga.y = 12;
        ga.w = winW - 24;
        ga.panelY = ga.y + (winH - 24) * 0.55;
        ga.h = ga.panelY - ga.y - 40; // leave room for the tabs row
        ga.tabX = ga.x;
        ga.tabY = ga.panelY;
        ga.tabW = 34;
        ga.tabH = 22;
        return ga;
    }

    static bool inRect(double px, double py, double x, double y, double w, double h)
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }

    // node position for band i in graph coordinates
    void nodePos(int i, const GraphArea& ga, double& nx, double& ny) const
    {
        nx = ga.x + freqToNorm(bandParamValue(i, kFreq)) * ga.w;
        ny = ga.y + gainToNorm(bandParamValue(i, kGain)) * ga.h;
    }

    int hitNode(double x, double y, const GraphArea& ga) const
    {
        int best = -1;
        double bestDist = 14.0 * 14.0; // 14px radius
        for (int i = 0; i < fNumBands; ++i)
        {
            double nx, ny;
            nodePos(i, ga, nx, ny);
            const double dx = x - nx, dy = y - ny;
            const double d2 = dx * dx + dy * dy;
            if (d2 < bestDist)
            {
                bestDist = d2;
                best = i;
            }
        }
        return best;
    }

    int hitTab(double x, double y, const GraphArea& ga) const
    {
        if (!inRect(x, y, ga.tabX, ga.tabY, ga.tabW * fNumBands + 70, ga.tabH))
            return -1;
        for (int i = 0; i < fNumBands; ++i)
        {
            const double tx = ga.tabX + i * (ga.tabW + 4);
            if (inRect(x, y, tx, ga.tabY, ga.tabW, ga.tabH))
                return i;
        }
        return -1;
    }

    // returns 1 = "+", 2 = "-", 0 = none
    int hitButton(double x, double y, const GraphArea& ga) const
    {
        const double bx = ga.tabX + fNumBands * (ga.tabW + 4) + 8;
        if (inRect(x, y, bx, ga.tabY, ga.tabH, ga.tabH)) return 1;
        if (inRect(x, y, bx + ga.tabH + 6, ga.tabY, ga.tabH, ga.tabH)) return 2;
        return 0;
    }

    struct SliderDef { double x, y, w, h; };

    // 7 sliders for the selected band + 7 globals laid out in the panel
    SliderDef sliderRect(int idx, const GraphArea& ga, double winW, double winH) const
    {
        SliderDef sd;
        const double colW = (winW - 24) / 7.0;
        sd.w = colW - 14;
        sd.x = 12 + idx * colW + 7;
        sd.h = 10;
        sd.y = ga.tabY + 34 + (ga.panelY + 34 < winH - 40 ? 0 : 0);
        // two rows: first 4 sliders top row, rest below
        const int row = (idx < 4) ? 0 : 1;
        const int col = (idx < 4) ? idx : idx - 4;
        sd.x = 12 + col * colW + 7;
        sd.y = ga.tabY + ga.tabH + 16 + row * 58;
        return sd;
    }

    int hitSlider(double x, double y, const GraphArea& ga) const
    {
        const double winW = getWidth(), winH = getHeight();
        for (int i = 0; i < kNumSliders; ++i)
        {
            const SliderDef sd = sliderRect(i, ga, winW, winH);
            if (inRect(x, y, sd.x - 6, sd.y - 6, sd.w + 12, sd.h + 12))
                return i;
        }
        return -1;
    }

    void updateSliderFromPos(double x, double y, const GraphArea& ga)
    {
        const double winW = getWidth(), winH = getHeight();
        const SliderDef sd = sliderRect(fDragSlider, ga, winW, winH);
        double t = (x - sd.x) / sd.w;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;

        const uint32_t pIdx = paramForSlider(fDragSlider);
        const double lo = sliderMin(fDragSlider), hi = sliderMax(fDragSlider);
        double v = lo + t * (hi - lo);

        // logarithmic sliders (frequency, attack, release)
        if (isLogSlider(fDragSlider))
            v = lo * std::pow(hi / lo, t);

        setParameterValue(pIdx, (float)v);
        fParams[pIdx] = (float)v;
        repaint();
    }

    /* ------------------------------------------------------------------------------------------------
     * Slider model */

    static const int kNumSliders = 14;

    // 0..6: selected band params (Type, On, Freq, Gain, Q, Amount, Bypass)
    // 7..13: globals (Threshold, Ratio, Attack, Release, Range, Mix, OutGain)
    uint32_t paramForSlider(int idx) const
    {
        static const int bandOffsets[kNumSliders < 7 ? kNumSliders : 1] = { kBypass };
        (void)bandOffsets;
        if (idx < 7)
        {
            static const int offs[7] = { kType, kEnabled, kFreq, kGain, kQ, kDAmount, kBypass };
            return bandParam(fSelected, offs[idx]);
        }
        return kThreshold + (idx - 7);
    }

    double sliderValue(int idx) const { return fParams[paramForSlider(idx)]; }

    double sliderMin(int idx) const
    {
        static const double bmin[7] = { 0, 0, 20, -24, 0.1, 0, 0 };
        static const double gmin[7] = { -60, 1, 0.1, 10, 0, 0, -24 };
        return (idx < 7) ? bmin[idx] : gmin[idx - 7];
    }

    double sliderMax(int idx) const
    {
        static const double bmax[7] = { 2, 1, 20000, 24, 10, 1, 1 };
        static const double gmax[7] = { 0, 20, 200, 1000, 24, 1, 24 };
        return (idx < 7) ? bmax[idx] : gmax[idx - 7];
    }

    static bool isLogSlider(int idx)
    {
        return idx == 2 || idx == 9 || idx == 10; // Freq, Attack, Release
    }

    static const char* sliderLabel(int idx)
    {
        static const char* names[14] = {
            "Type", "On", "Freq", "Gain", "Q", "Amt", "Byp",
            "Thresh", "Ratio", "Attack", "Release", "Range", "Mix", "Out"
        };
        return names[idx];
    }

    /* ------------------------------------------------------------------------------------------------
     * Parameter accessors */

    uint32_t bandParam(int band, int off) const
    {
        return (uint32_t)(band * kParamsPerBand + off);
    }

    float bandParamValue(int band, int off) const
    {
        return fParams[bandParam(band, off)];
    }

    void setBandParam(int band, int off, float value)
    {
        const uint32_t idx = bandParam(band, off);
        setParameterValue(idx, value);
        fParams[idx] = value;
    }

    void pushNumBandsState()
    {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", fNumBands);
        setState(kStateNumBands, buf);
    }

    /* ------------------------------------------------------------------------------------------------
     * Band management */

    void addBand()
    {
        if (fNumBands >= kMaxBands)
            return;
        const int i = fNumBands;
        // sensible new band: bell at 1k, 0 dB, Q 1, fully dynamic, enabled
        setBandParam(i, kType, kFilterBell);
        setBandParam(i, kEnabled, 1.f);
        setBandParam(i, kFreq, 1000.f);
        setBandParam(i, kGain, 0.f);
        setBandParam(i, kQ, 1.f);
        setBandParam(i, kDAmount, 1.f);
        setBandParam(i, kBypass, 0.f);
        fNumBands++;
        fSelected = i;
        pushNumBandsState();
        repaint();
    }

    void addBandAt(double freq, double gain)
    {
        if (fNumBands >= kMaxBands)
            return;
        addBand();
        setBandParam(fNumBands - 1, kFreq, (float)freq);
        setBandParam(fNumBands - 1, kGain, (float)gain);
        repaint();
    }

    void removeBand()
    {
        if (fNumBands <= 1)
            return;
        const int i = fNumBands - 1;
        setBandParam(i, kEnabled, 0.f);
        setBandParam(i, kGain, 0.f);
        fNumBands--;
        if (fSelected >= fNumBands)
            fSelected = fNumBands - 1;
        pushNumBandsState();
        repaint();
    }

    void loadDefaults()
    {
        fNumBands = kDefaultNumBands;
        fSelected = 0;

        const float defs[kDefaultNumBands][5] = {
            { kFilterLowShelf,  100.f,  -2.f, 0.7f, 0.5f },
            { kFilterBell,      800.f,   3.f, 1.0f, 0.8f },
            { kFilterBell,     3000.f,  -4.f, 1.2f, 0.8f },
            { kFilterHighShelf,10000.f,  2.f, 0.7f, 0.5f },
        };
        for (int i = 0; i < kMaxBands; ++i)
        {
            fParams[bandParam(i, kBypass)]  = 0.f;
            fParams[bandParam(i, kType)]    = (i < kDefaultNumBands) ? defs[i][0] : kFilterBell;
            fParams[bandParam(i, kEnabled)] = (i < kDefaultNumBands) ? 1.f : 0.f;
            fParams[bandParam(i, kFreq)]    = (i < kDefaultNumBands) ? defs[i][1] : 1000.f;
            fParams[bandParam(i, kGain)]    = (i < kDefaultNumBands) ? defs[i][2] : 0.f;
            fParams[bandParam(i, kQ)]       = (i < kDefaultNumBands) ? defs[i][3] : 1.f;
            fParams[bandParam(i, kDAmount)] = (i < kDefaultNumBands) ? defs[i][4] : 1.f;
        }
        fParams[kThreshold]   = -20.f;
        fParams[kRatio]       = 4.f;
        fParams[kAttack]      = 15.f;
        fParams[kRelease]     = 150.f;
        fParams[kRange]       = 12.f;
        fParams[kMix]         = 1.f;
        fParams[kOutputGain]  = 0.f;
    }

    /* ------------------------------------------------------------------------------------------------
     * Curve computation (uses the mirrored parameters) */

    double responseDbAt(double freq, bool includeDynamics) const
    {
        const double sr = getSampleRate();
        double total = 0.0;
        for (int i = 0; i < fNumBands; ++i)
        {
            if (fParams[bandParam(i, kEnabled)] < 0.5f ||
                fParams[bandParam(i, kBypass)] >= 0.5f)
                continue;

            double g = fParams[bandParam(i, kGain)];
            if (includeDynamics)
            {
                // worst-case reduction for the shading: amount * range
                // (for the live curve we approximate with the stored env hint)
                g += fParams[bandParam(i, kDAmount)] * (-fParams[kRange]) * dynActivity(i);
            }
            const Coeffs c = designFilter((int)fParams[bandParam(i, kType)],
                                          freq, fParams[bandParam(i, kQ)], g, sr);
            total += magnitudeDb(c, freq, sr);
        }
        total += fParams[kOutputGain];
        return total;
    }

    // pseudo activity 0..1 driving the animated shading (visual only)
    double dynActivity(int i) const
    {
        // simple animation: bands pulse gently so the dynamic range is visible
        const double phase = (double)(fAnimTick + i * 7) * 0.05;
        return 0.5 - 0.5 * std::cos(phase);
    }

    /* ------------------------------------------------------------------------------------------------
     * Painting pieces */

    void drawGrid(cairo_t* cr, const GraphArea& ga)
    {
        static const double freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        cairo_set_line_width(cr, 1.0);

        for (const double f : freqs)
        {
            const double x = ga.x + freqToNorm(f) * ga.w;
            cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
            cairo_move_to(cr, x, ga.y);
            cairo_line_to(cr, x, ga.y + ga.h);
            cairo_stroke(cr);

            char buf[16];
            if (f >= 1000.0) std::snprintf(buf, sizeof(buf), "%uk", f / 1000.0);
            else             std::snprintf(buf, sizeof(buf), "%.0f", f);
            cairo_set_font_size(cr, 9);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
            cairo_text_extents_t te;
            cairo_text_extents(cr, buf, &te);
            cairo_move_to(cr, x - te.width / 2.0 - te.x_bearing, ga.y + ga.h + 11);
            cairo_show_text(cr, buf);
        }

        for (int db = -24; db <= 24; db += 6)
        {
            const double y = ga.y + gainToNorm(db) * ga.h;
            cairo_set_source_rgba(cr, 1, 1, 1, db == 0 ? 0.18 : 0.06);
            cairo_move_to(cr, ga.x, y);
            cairo_line_to(cr, ga.x + ga.w, y);
            cairo_stroke(cr);
        }
    }

    void drawCurves(cairo_t* cr, const GraphArea& ga)
    {
        const int N = 256;

        // precompute static & dynamic-min curves
        double stat[N], dynMin[N], dynMax[N];
        for (int k = 0; k < N; ++k)
        {
            const double t = (double)k / (N - 1);
            const double f = normToFreq(t);
            stat[k]   = responseDbAt(f, false);
            dynMin[k] = responseDbAt(f, true);
            dynMax[k] = stat[k];
            if (dynMin[k] > dynMax[k])
                std::swap(dynMin[k], dynMax[k]);
        }

        auto mapY = [&](double db) {
            double y = ga.y + gainToNorm(db) * ga.h;
            if (y < ga.y) y = ga.y;
            if (y > ga.y + ga.h) y = ga.y + ga.h;
            return y;
        };

        // shaded dynamic range area
        cairo_set_source_rgba(cr, 0.25, 0.65, 1.0, 0.12);
        cairo_new_path(cr);
        for (int k = 0; k < N; ++k)
        {
            const double x = ga.x + ((double)k / (N - 1)) * ga.w;
            if (k == 0) cairo_move_to(cr, x, mapY(dynMax[k]));
            else        cairo_line_to(cr, x, mapY(dynMax[k]));
        }
        for (int k = N - 1; k >= 0; --k)
        {
            const double x = ga.x + ((double)k / (N - 1)) * ga.w;
            cairo_line_to(cr, x, mapY(dynMin[k]));
        }
        cairo_close_path(cr);
        cairo_fill(cr);

        // static response curve
        cairo_set_line_width(cr, 2.0);
        cairo_set_source_rgb(cr, 0.30, 0.80, 1.00);
        cairo_new_path(cr);
        for (int k = 0; k < N; ++k)
        {
            const double x = ga.x + ((double)k / (N - 1)) * ga.w;
            if (k == 0) cairo_move_to(cr, x, mapY(stat[k]));
            else        cairo_line_to(cr, x, mapY(stat[k]));
        }
        cairo_stroke(cr);

        // live dynamic curve (animated approximation)
        cairo_set_line_width(cr, 1.5);
        cairo_set_source_rgba(cr, 1.0, 0.55, 0.20, 0.9);
        cairo_new_path(cr);
        for (int k = 0; k < N; ++k)
        {
            const double t = (double)k / (N - 1);
            const double db = responseDbAt(normToFreq(t), true);
            const double x = ga.x + t * ga.w;
            if (k == 0) cairo_move_to(cr, x, mapY(db));
            else        cairo_line_to(cr, x, mapY(db));
        }
        cairo_stroke(cr);
    }

    static void bandColor(int i, double& r, double& g, double& b)
    {
        static const double palette[8][3] = {
            { 1.00, 0.40, 0.40 }, { 1.00, 0.75, 0.30 }, { 0.55, 0.95, 0.40 },
            { 0.30, 0.85, 0.85 }, { 0.45, 0.65, 1.00 }, { 0.85, 0.50, 1.00 },
            { 1.00, 0.55, 0.80 }, { 0.80, 0.80, 0.80 },
        };
        r = palette[i % 8][0]; g = palette[i % 8][1]; b = palette[i % 8][2];
    }

    void drawNodes(cairo_t* cr, const GraphArea& ga)
    {
        for (int i = 0; i < fNumBands; ++i)
        {
            double nx, ny;
            nodePos(i, ga, nx, ny);

            double r, g, b;
            bandColor(i, r, g, b);

            const bool enabled = fParams[bandParam(i, kEnabled)] >= 0.5f;
            const bool bypassed = fParams[bandParam(i, kBypass)] >= 0.5f;

            // connecting line to 0 dB axis for readability
            cairo_set_line_width(cr, 1.0);
            cairo_set_source_rgba(cr, r, g, b, enabled ? 0.35 : 0.15);
            cairo_move_to(cr, nx, ga.y + ga.h / 2.0);
            cairo_line_to(cr, nx, ny);
            cairo_stroke(cr);

            // node circle
            cairo_arc(cr, nx, ny, i == fSelected ? 9 : 7, 0, 2.0 * kPi);
            if (enabled && !bypassed)
                cairo_set_source_rgb(cr, r, g, b);
            else
                cairo_set_source_rgba(cr, r, g, b, 0.35);
            cairo_fill_preserve(cr);
            cairo_set_line_width(cr, i == fSelected ? 2.5 : 1.0);
            cairo_set_source_rgb(cr, i == fSelected ? 1 : 0.2,
                                 i == fSelected ? 1 : 0.2,
                                 i == fSelected ? 1 : 0.2);
            cairo_stroke(cr);

            // band number
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", i + 1);
            cairo_set_font_size(cr, 9);
            cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
            cairo_text_extents_t te;
            cairo_text_extents(cr, buf, &te);
            cairo_move_to(cr, nx - te.width / 2.0 - te.x_bearing,
                          ny - te.height / 2.0 - te.y_bearing);
            cairo_show_text(cr, buf);
        }
    }

    void drawLabels(cairo_t* cr, const GraphArea& ga)
    {
        cairo_set_font_size(cr, 10);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
        const char* msg = "left-drag node: freq/gain | right-drag: Q | dbl-click: type | click empty: add band";
        cairo_text_extents_t te;
        cairo_text_extents(cr, msg, &te);
        cairo_move_to(cr, ga.x + 6, ga.y + 12);
        cairo_show_text(cr, msg);
    }

    void drawBandSelector(cairo_t* cr, const GraphArea& ga)
    {
        for (int i = 0; i < fNumBands; ++i)
        {
            const double tx = ga.tabX + i * (ga.tabW + 4);
            double r, g, b;
            bandColor(i, r, g, b);

            cairo_rectangle(cr, tx, ga.tabY, ga.tabW, ga.tabH);
            if (i == fSelected)
                cairo_set_source_rgb(cr, r, g, b);
            else
                cairo_set_source_rgba(cr, r, g, b, 0.25);
            cairo_fill(cr);

            char buf[8];
            std::snprintf(buf, sizeof(buf), "B%d", i + 1);
            cairo_set_font_size(cr, 11);
            cairo_text_extents_t te;
            cairo_text_extents(cr, buf, &te);
            cairo_set_source_rgb(cr, i == fSelected ? 0.05 : r,
                                 i == fSelected ? 0.05 : g,
                                 i == fSelected ? 0.05 : b);
            cairo_move_to(cr, tx + ga.tabW / 2.0 - te.width / 2.0 - te.x_bearing,
                          ga.tabY + ga.tabH / 2.0 - te.height / 2.0 - te.y_bearing);
            cairo_show_text(cr, buf);
        }

        // +/- buttons
        const double bx = ga.tabX + fNumBands * (ga.tabW + 4) + 8;
        cairo_set_line_width(cr, 1.5);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
        cairo_rectangle(cr, bx, ga.tabY, ga.tabH, ga.tabH);
        cairo_move_to(cr, bx + 5, ga.tabY + ga.tabH / 2.0);
        cairo_line_to(cr, bx + ga.tabH - 5, ga.tabY + ga.tabH / 2.0);
        cairo_move_to(cr, bx + ga.tabH / 2.0, ga.tabY + 5);
        cairo_line_to(cr, bx + ga.tabH / 2.0, ga.tabY + ga.tabH - 5);
        cairo_stroke(cr);

        const double mx = bx + ga.tabH + 6;
        cairo_rectangle(cr, mx, ga.tabY, ga.tabH, ga.tabH);
        cairo_move_to(cr, mx + 5, ga.tabY + ga.tabH / 2.0);
        cairo_line_to(cr, mx + ga.tabH - 5, ga.tabY + ga.tabH / 2.0);
        cairo_stroke(cr);
    }

    void drawSliders(cairo_t* cr, const GraphArea& ga, double winW, double winH)
    {
        const double colW = (winW - 24) / 7.0;

        for (int i = 0; i < kNumSliders; ++i)
        {
            const SliderDef sd = sliderRect(i, ga, winW, winH);
            const double lo = sliderMin(i), hi = sliderMax(i);
            double v = sliderValue(i);
            double t = (v - lo) / (hi - lo);
            if (isLogSlider(i) && lo > 0.0)
                t = std::log(v / lo) / std::log(hi / lo);
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;

            // label
            cairo_set_font_size(cr, 9);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
            cairo_move_to(cr, sd.x, sd.y - 4);
            cairo_show_text(cr, sliderLabel(i));

            // track
            cairo_set_line_width(cr, 3.0);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
            cairo_move_to(cr, sd.x, sd.y + sd.h / 2.0);
            cairo_line_to(cr, sd.x + sd.w, sd.y + sd.h / 2.0);
            cairo_stroke(cr);

            // fill
            cairo_set_source_rgb(cr, 0.30, 0.80, 1.00);
            cairo_move_to(cr, sd.x, sd.y + sd.h / 2.0);
            cairo_line_to(cr, sd.x + t * sd.w, sd.y + sd.h / 2.0);
            cairo_stroke(cr);

            // handle
            cairo_arc(cr, sd.x + t * sd.w, sd.y + sd.h / 2.0, 5, 0, 2.0 * kPi);
            cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
            cairo_fill(cr);

            // value text
            char buf[24];
            if (i < 7 && i == 0)
                std::snprintf(buf, sizeof(buf), "%s",
                              v < 0.5 ? "Bell" : (v < 1.5 ? "LowSh" : "HiSh"));
            else if (i == 2)
                std::snprintf(buf, sizeof(buf), "%.0fHz", v);
            else if (i == 1 || i == 6)
                std::snprintf(buf, sizeof(buf), "%s", v >= 0.5 ? "on" : "off");
            else
                std::snprintf(buf, sizeof(buf), "%.2g", v);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
            cairo_move_to(cr, sd.x, sd.y + sd.h + 12);
            cairo_show_text(cr, buf);
        }

        // section titles
        cairo_set_font_size(cr, 10);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
        cairo_move_to(cr, 12, ga.tabY + ga.tabH + 8);
        cairo_show_text(cr, "Band controls (selected)");
        cairo_move_to(cr, 12 + 4 * colW, ga.tabY + ga.tabH + 8);
        cairo_show_text(cr, "Dynamics / Output");
    }

    enum DragMode { kDragNone = 0, kDragNode, kDragSlider };

    float fParams[kTotalParams];
    int fNumBands = kDefaultNumBands;
    int fSelected = 0;
    DragMode fDragging = kDragNone;
    int fDragBand = -1;
    int fDragSlider = -1;
    bool fDragRight = false;
    uint fAnimTick = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynEGUI)
};

// ---------------------------------------------------------------------------

UI* createUI() { return new DynEGUI(); }

// ---------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
