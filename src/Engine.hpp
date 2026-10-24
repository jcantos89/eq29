// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "Biquad.hpp"

namespace eq29 {
// No heap allocation, locks, I/O or host callbacks in the processing path.
class Engine {
public:
    std::array<float,ParameterCount> values = defaults();
    explicit Engine(double rate=48000) { setSampleRate(rate); }
    void setSampleRate(double rate) {
        rate_ = std::clamp(rate, 1000.0, 768000.0);
        values[SampleRate] = float(rate_);
        reset();
    }
    void set(unsigned index,float value) {
        if (index<ParameterCount && !isOutput(index)) values[index]=sanitize(index,value);
    }
    void reset() {
        tick_=0;
        fadeFrames_=std::max(1u,unsigned(.005*rate_));
        bypass_=values[Bypass]; output_=values[OutputGain];
        smoothing_=std::exp(-16.0/(.01*rate_));
        sampleSmoothing_=std::exp(-1.0/(.005*rate_));
        for (unsigned b=0;b<MaxBands;++b) {
            bands_[b]=Band{};
            bands_[b].hz=values[param(b,Frequency)];
            bands_[b].q=values[param(b,Q)];
            bands_[b].gain=values[param(b,Gain)]*values[param(b,Enabled)];
            values[MeterBase+b]=0;
        }
    }
    void process(const float* const* in, float* const* out, unsigned count) {
        for (unsigned n=0;n<count;++n) {
            if (tick_==0) update();
            tick_=(tick_+1)%16;
            const double dry[2]={std::isfinite(in[0][n]) ? in[0][n] : 0.0,
                                 std::isfinite(in[1][n]) ? in[1][n] : 0.0};
            double wet[2]={dry[0],dry[1]};
            for (unsigned b=0;b<MaxBands;++b) {
                auto& band=bands_[b];
                // All detectors see the original input, independent of other bands.
                const double peak=std::max(std::abs(band.detect[0].process(dry[0])),
                                           std::abs(band.detect[1].process(dry[1])));
                const double time=peak>band.envelope ? band.attack : band.release;
                band.envelope=time*band.envelope+(1-time)*peak;
                for (unsigned ch=0;ch<2;++ch) {
                    const double filtered=band.eq[ch].process(wet[ch]);
                    if (band.fade>0) {
                        const double mix=double(band.fade)/fadeFrames_;
                        wet[ch]=mix*band.oldEq[ch].process(wet[ch])+(1-mix)*filtered;
                    } else wet[ch]=filtered;
                }
                if (band.fade>0) --band.fade;
            }
            bypass_=sampleSmoothing_*bypass_+(1-sampleSmoothing_)*values[Bypass];
            output_=sampleSmoothing_*output_+(1-sampleSmoothing_)*values[OutputGain];
            const double makeup=dbToGain(output_);
            for (unsigned ch=0;ch<2;++ch)
                out[ch][n]=float(bypass_*dry[ch]+(1-bypass_)*wet[ch]*makeup);
        }
    }
private:
    struct Band {
        Biquad eq[2],detect[2],oldEq[2];
        unsigned fade=0;
        double envelope=0,hz=1000,q=.707,gain=0,dynamic=0,attack=0,release=0;
        unsigned previousType=99;
    };
    std::array<Band,MaxBands> bands_{};
    double rate_=48000,smoothing_=0,sampleSmoothing_=0,bypass_=0,output_=0;
    unsigned tick_=0,fadeFrames_=240;
    void update() {
        for (unsigned b=0;b<MaxBands;++b) {
            auto& s=bands_[b];
            const auto p=[&](Field f) { return double(values[param(b,f)]); };
            const auto smooth=[&](double old,double target) {return smoothing_*old+(1-smoothing_)*target;};
            s.hz=smooth(s.hz,p(Frequency)); s.q=smooth(s.q,p(Q));
            const unsigned type=unsigned(p(Type));
            if (type!=s.previousType) {
                // Crossfade old/new topologies for 5 ms rather than stepping delay state.
                if (s.previousType!=99) {
                    s.oldEq[0]=s.eq[0]; s.oldEq[1]=s.eq[1]; s.fade=fadeFrames_;
                }
                s.eq[0].reset(); s.eq[1].reset(); s.detect[0].reset(); s.detect[1].reset();
                s.envelope=0; s.previousType=type;
            }
            s.attack=std::exp(-1.0/(p(Attack)*.001*rate_));
            s.release=std::exp(-1.0/(p(Release)*.001*rate_));
            const double above=std::max(0.0,gainToDb(s.envelope)-p(Threshold));
            const double slope=p(Mode)>.5 ? p(Ratio)-1.0 : 1.0-1.0/p(Ratio);
            const double depth=std::min(p(Range),above*slope);
            const double target=p(Enabled)*p(Dynamics)*depth*(p(Mode)>.5 ? 1 : -1);
            const double dynamicSmoothing=std::exp(-16.0/(.001*rate_));
            s.dynamic=dynamicSmoothing*s.dynamic+(1-dynamicSmoothing)*target;
            s.gain=smooth(s.gain,p(Enabled)*p(Gain));
            const auto ec=equalizer(type,s.hz,s.q,s.gain+s.dynamic,rate_);
            const auto dc=detector(type,s.hz,s.q,rate_);
            for (unsigned ch=0;ch<2;++ch) { s.eq[ch].c=ec; s.detect[ch].c=dc; }
            values[MeterBase+b]=float(s.dynamic);
        }
    }
};
}
