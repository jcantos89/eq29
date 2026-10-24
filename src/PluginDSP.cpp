// SPDX-License-Identifier: CC0-1.0
#include "DistrhoPlugin.hpp"
#include "Engine.hpp"
#include <cstdio>

START_NAMESPACE_DISTRHO
class DynamicEQ final : public Plugin {
public:
    DynamicEQ() : Plugin(eq29::ParameterCount,0,0), engine_(getSampleRate()) {}
protected:
    const char* getLabel() const override { return "my_dynamic_eq"; }
    const char* getDescription() const override { return "Eight-band stereo dynamic equalizer with band-local peak detectors."; }
    const char* getMaker() const override { return "eq29"; }
    const char* getHomePage() const override { return "https://github.com/jcantos89/eq29"; }
    const char* getLicense() const override { return "https://creativecommons.org/publicdomain/zero/1.0/"; }
    uint32_t getVersion() const override { return d_version(1,0,0); }
    int64_t getUniqueId() const override { return d_cconst('e','q','2','9'); }
    void initAudioPort(bool input,uint32_t index,AudioPort& port) override {
        Plugin::initAudioPort(input,index,port); port.groupId=kPortGroupStereo;
    }
    void initParameter(uint32_t index,Parameter& p) override {
        const auto s=eq29::spec(index);
        char name[80],symbol[80];
        if (index<eq29::MeterBase) {
            std::snprintf(name,sizeof(name),"Band %u %s",index/eq29::Fields+1,s.name);
            std::snprintf(symbol,sizeof(symbol),"band%u_%s",index/eq29::Fields+1,s.symbol);
        } else if (index<eq29::Bypass) {
            std::snprintf(name,sizeof(name),"Band %u dynamic gain",index-eq29::MeterBase+1);
            std::snprintf(symbol,sizeof(symbol),"band%u_dynamic_gain",index-eq29::MeterBase+1);
        } else {
            std::snprintf(name,sizeof(name),"%s",s.name);
            std::snprintf(symbol,sizeof(symbol),"%s",s.symbol);
        }
        p.name=name; p.symbol=symbol; p.unit=s.unit;
        p.ranges.min=s.min; p.ranges.max=s.max; p.ranges.def=s.def;
        p.hints=eq29::isOutput(index) ? kParameterIsOutput : kParameterIsAutomatable;
        if (s.integer) p.hints|=kParameterIsInteger;
        if (s.integer && s.max==1) p.hints|=kParameterIsBoolean;
        if (s.logarithmic) p.hints|=kParameterIsLogarithmic;
        if (index<eq29::MeterBase && index%eq29::Fields==eq29::Type) {
            p.enumValues.count=3; p.enumValues.restrictedMode=true;
            p.enumValues.values=new ParameterEnumerationValue[3];
            p.enumValues.values[0]={0,"Bell"}; p.enumValues.values[1]={1,"Low Shelf"}; p.enumValues.values[2]={2,"High Shelf"};
        }
        if (index<eq29::MeterBase && index%eq29::Fields==eq29::Mode) {
            p.enumValues.count=2; p.enumValues.restrictedMode=true;
            p.enumValues.values=new ParameterEnumerationValue[2];
            p.enumValues.values[0]={0,"Compression"}; p.enumValues.values[1]={1,"Upward expansion"};
        }
    }
    float getParameterValue(uint32_t i) const override { return engine_.values[i]; }
    void setParameterValue(uint32_t i,float v) override { engine_.set(i,v); }
    void activate() override { engine_.reset(); }
    void sampleRateChanged(double rate) override { engine_.setSampleRate(rate); }
    void run(const float** in,float** out,uint32_t frames) override { engine_.process(in,out,frames); }
private:
    eq29::Engine engine_;
};
Plugin* createPlugin() { return new DynamicEQ; }
END_NAMESPACE_DISTRHO
