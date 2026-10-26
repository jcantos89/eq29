// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "Parameters.hpp"

namespace eq29 {
// Engine indices and released LV2 ports remain unchanged. DPF requires VST2
// inputs to precede outputs, and hides the trailing meters from VST automation.
constexpr unsigned toEngineParameter(unsigned host) {
#ifdef EQ29_VST2_PORT_ORDER
    if (host==MeterBase) return Bypass;
    if (host==MeterBase+1) return OutputGain;
    if (host>=MeterBase+2 && host<SampleRate) return host-2;
#endif
    return host;
}
constexpr unsigned toHostParameter(unsigned engine) {
#ifdef EQ29_VST2_PORT_ORDER
    if (engine==Bypass) return MeterBase;
    if (engine==OutputGain) return MeterBase+1;
    if (engine>=MeterBase && engine<Bypass) return engine+2;
#endif
    return engine;
}
constexpr unsigned VstInputCount=MeterBase+2;
}
