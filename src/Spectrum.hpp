// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "Parameters.hpp"
#include <complex>

namespace eq29 {
// Hann-windowed, 4096-sample stereo FFT, 50% overlap. Packed real/imaginary
// channels retain antiphase material. All storage/twiddles are preallocated.
class Spectrum {
public:
    static constexpr unsigned Size=4096,Hop=Size/2;
    Spectrum() {
        for(unsigned i=0;i<Size;++i) window_[i]=float(.5-.5*std::cos(2*3.141592653589793*i/Size));
        for(unsigned i=0;i<Size/2;++i) twiddle_[i]=std::polar(1.f,float(-2*3.141592653589793*i/Size));
    }
    void reset(double rate) { rate_=rate;ring_.fill({});position_=samples_=0;levels.fill(-100); }
    bool push(float left,float right) {
        ring_[position_]={left,right};position_=(position_+1)%Size;
        ++samples_;
        if(samples_<Size || samples_%Hop!=0) return false;
        samples_=Size; // Keep bounded; the first frame still requires a full window.
        for(unsigned i=0;i<Size;++i) fft_[i]=ring_[(position_+i)%Size]*window_[i];
        for(unsigned i=1,j=0;i<Size;++i) {
            unsigned bit=Size>>1;for(;j&bit;bit>>=1) j^=bit;j^=bit;
            if(i<j) std::swap(fft_[i],fft_[j]);
        }
        for(unsigned len=2;len<=Size;len<<=1) for(unsigned i=0;i<Size;i+=len)
            for(unsigned j=0;j<len/2;++j) {
                const auto u=fft_[i+j],v=fft_[i+j+len/2]*twiddle_[j*Size/len];
                fft_[i+j]=u+v;fft_[i+j+len/2]=u-v;
            }
        for(unsigned b=0;b<SpectrumBins;++b) {
            const double center=spectrumFrequency(b,rate_);
            const double low=b ? std::sqrt(center*spectrumFrequency(b-1,rate_)) : 20;
            const double high=b+1<SpectrumBins ? std::sqrt(center*spectrumFrequency(b+1,rate_)) : std::min(20000.0,rate_*.45);
            const unsigned first=std::clamp(unsigned(std::lround(low*Size/rate_)),1u,Size/2-1);
            const unsigned last=std::clamp(unsigned(std::lround(high*Size/rate_)),first,Size/2-1);
            float peak=0;
            for(unsigned k=first;k<=last;++k) {
                const auto mirror=std::conj(fft_[Size-k]);
                const auto l=(fft_[k]+mirror)*.5f;
                const auto r=(fft_[k]-mirror)*std::complex<float>(0,-.5f);
                peak=std::max(peak,std::max(std::abs(l),std::abs(r))*4.f/Size);
            }
            const float db=float(std::clamp(gainToDb(peak),-100.0,12.0));
            // Instant peak rise; 24 dB/second fall, independent of sample rate.
            levels[b]=std::max(db,levels[b]-float(24*Hop/rate_));
        }
        return true;
    }
    std::array<float,SpectrumBins> levels{};
private:
    std::array<std::complex<float>,Size> ring_{},fft_{};
    std::array<std::complex<float>,Size/2> twiddle_{};
    std::array<float,Size> window_{};
    unsigned position_=0,samples_=0;
    double rate_=48000;
};
}
