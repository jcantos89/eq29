// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "Parameters.hpp"
namespace eq29 {
constexpr std::array<unsigned,ParameterCount> makeHostOrder() {
    std::array<unsigned,ParameterCount> order{};unsigned n=0;
    for(unsigned i=0;i<MeterBase;++i) order[n++]=i;
    order[n++]=Bypass;order[n++]=OutputGain; // Preserve the original VST2 98 inputs.
    for(unsigned i=ExpandedBandBase;i<ExtraMeterBase;++i) order[n++]=i;
    for(unsigned i=SidechainBase;i<=AnalyzerMode;++i) order[n++]=i;
    for(unsigned i=0;i<ParameterCount;++i) if(isOutput(i)) order[n++]=i;
    return order;
}
inline constexpr auto HostOrder=makeHostOrder();
constexpr unsigned toEngineParameter(unsigned host) {
#ifdef EQ29_VST2_PORT_ORDER
    return HostOrder[host];
#else
    return host;
#endif
}
constexpr unsigned toHostParameter(unsigned engine) {
#ifdef EQ29_VST2_PORT_ORDER
    for(unsigned h=0;h<ParameterCount;++h) if(HostOrder[h]==engine) return h;
#endif
    return engine;
}
constexpr unsigned VstInputCount=InputCount;
static_assert(HostOrder[96]==Bypass && HostOrder[97]==OutputGain,"Legacy VST automation must remain stable");
}
