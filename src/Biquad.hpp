// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "Parameters.hpp"
#include <complex>

namespace eq29 {
constexpr double Pi = 3.14159265358979323846;
struct Coefficients {
    double b0=1, b1=0, b2=0, a1=0, a2=0;
    static Coefficients normalized(double b0, double b1, double b2, double a0, double a1, double a2) {
        return {b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};
    }
    double magnitude(double hz, double rate) const {
        const auto z = std::polar(1.0, -2.0*Pi*hz/rate);
        return std::abs((b0 + b1*z + b2*z*z) / (1.0 + a1*z + a2*z*z));
    }
};
// RBJ cookbook biquads; shelf Q controls resonance (not a shelf-slope parameter).
inline Coefficients equalizer(unsigned type, double hz, double q, double gain, double rate) {
    hz = std::clamp(hz, 1.0, rate*.45);
    q = std::clamp(q, .15, 12.0);
    const double w = 2*Pi*hz/rate, c=std::cos(w), s=std::sin(w);
    const double a=std::pow(10.0,gain/40.0), alpha=s/(2*q), t=2*std::sqrt(a)*alpha;
    if (type == LowShelf)
        return Coefficients::normalized(a*((a+1)-(a-1)*c+t),2*a*((a-1)-(a+1)*c),a*((a+1)-(a-1)*c-t),
                                      (a+1)+(a-1)*c+t,-2*((a-1)+(a+1)*c),(a+1)+(a-1)*c-t);
    if (type == HighShelf)
        return Coefficients::normalized(a*((a+1)+(a-1)*c+t),-2*a*((a-1)+(a+1)*c),a*((a+1)+(a-1)*c-t),
                                      (a+1)-(a-1)*c+t,2*((a-1)-(a+1)*c),(a+1)-(a-1)*c-t);
    return Coefficients::normalized(1+alpha*a,-2*c,1-alpha*a,1+alpha/a,-2*c,1-alpha/a);
}
inline Coefficients detector(unsigned type, double hz, double q, double rate) {
    const double w=2*Pi*std::clamp(hz,1.0,rate*.45)/rate, c=std::cos(w);
    const double alpha=std::sin(w)/(2*(type == Bell ? q : .7071067811865476));
    if (type == LowShelf) return Coefficients::normalized((1-c)/2,1-c,(1-c)/2,1+alpha,-2*c,1-alpha);
    if (type == HighShelf) return Coefficients::normalized((1+c)/2,-(1+c),(1+c)/2,1+alpha,-2*c,1-alpha);
    // Constant 0 dB peak bandpass; detector follows the band's frequency and Q.
    return Coefficients::normalized(alpha,0,-alpha,1+alpha,-2*c,1-alpha);
}
struct Biquad {
    Coefficients c;
    double z1=0,z2=0;
    void reset() { z1=z2=0; }
    double process(double x) {
        const double y=c.b0*x+z1;
        z1=c.b1*x-c.a1*y+z2;
        z2=c.b2*x-c.a2*y;
        if (std::abs(z1)<1.e-25) z1=0;
        if (std::abs(z2)<1.e-25) z2=0;
        return y;
    }
};
}
