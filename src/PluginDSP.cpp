// SPDX-License-Identifier: CC0-1.0
#include "DistrhoPlugin.hpp"
#include "Engine.hpp"
#include "PortMapping.hpp"
#include <cstdio>

START_NAMESPACE_DISTRHO
class DynamicEQ final : public Plugin {
public:
    DynamicEQ() : Plugin(eq29::ParameterCount,0,0), engine_(getSampleRate()) {}
protected:
    const char* getLabel() const override { return "my_dynamic_eq"; }
    const char* getDescription() const override { return "Sixteen-band stereo dynamic equalizer with band-local peak detectors."; }
    const char* getMaker() const override { return "eq29"; }
    const char* getHomePage() const override { return "https://github.com/jcantos89/eq29"; }
    const char* getLicense() const override { return "https://creativecommons.org/publicdomain/zero/1.0/"; }
    uint32_t getVersion() const override { return d_version(1,1,0); }
    int64_t getUniqueId() const override { return d_cconst('e','q','2','9'); }
    void initAudioPort(bool input,uint32_t index,AudioPort& port) override {
        Plugin::initAudioPort(input,index,port);
        if(input && index>=2) {
            port.name=index==2 ? "Sidechain Left" : "Sidechain Right";
            port.symbol=index==2 ? "sidechain_l" : "sidechain_r";
            port.hints|=kAudioPortIsSidechain;
            port.groupId=kPortGroupNone;
        } else port.groupId=kPortGroupStereo;
    }
    void initParameter(uint32_t index,Parameter& p) override {
        index=eq29::toEngineParameter(index);
        const auto s=eq29::spec(index);
        char name[80],symbol[80];
        if (eq29::isBandParameter(index)) {
            std::snprintf(name,sizeof(name),"Band %u %s",eq29::parameterBand(index)+1,s.name);
            std::snprintf(symbol,sizeof(symbol),"band%u_%s",eq29::parameterBand(index)+1,s.symbol);
        } else if ((index>=eq29::MeterBase && index<eq29::Bypass) ||
                   (index>=eq29::ExtraMeterBase && index<eq29::SidechainBase)) {
            const unsigned band=index<eq29::Bypass ? index-eq29::MeterBase : eq29::LegacyBands+index-eq29::ExtraMeterBase;
            std::snprintf(name,sizeof(name),"Band %u dynamic gain",band+1);
            std::snprintf(symbol,sizeof(symbol),"band%u_dynamic_gain",band+1);
        } else if(index>=eq29::SidechainBase && index<eq29::AnalyzerMode) {
            std::snprintf(name,sizeof(name),"Band %u external sidechain",index-eq29::SidechainBase+1);
            std::snprintf(symbol,sizeof(symbol),"band%u_external_sidechain",index-eq29::SidechainBase+1);
        } else if(index>=eq29::SpectrumInBase) {
            const bool output=index>=eq29::SpectrumOutBase;
            const unsigned bin=index-(output ? eq29::SpectrumOutBase : eq29::SpectrumInBase);
            std::snprintf(name,sizeof(name),"Spectrum %s %u",output?"output":"input",bin);
            std::snprintf(symbol,sizeof(symbol),"spectrum_%s_%u",output?"output":"input",bin);
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
        if(index==eq29::AnalyzerMode) {
            p.enumValues.count=4;p.enumValues.restrictedMode=true;
            p.enumValues.values=new ParameterEnumerationValue[4];
            p.enumValues.values[0]={0,"Off"};p.enumValues.values[1]={1,"Input"};
            p.enumValues.values[2]={2,"Output"};p.enumValues.values[3]={3,"Input + Output"};
        }
        if (eq29::isBandParameter(index) && eq29::parameterField(index)==eq29::Type) {
            p.enumValues.count=3; p.enumValues.restrictedMode=true;
            p.enumValues.values=new ParameterEnumerationValue[3];
            p.enumValues.values[0]={0,"Bell"}; p.enumValues.values[1]={1,"Low Shelf"}; p.enumValues.values[2]={2,"High Shelf"};
        }
        if (eq29::isBandParameter(index) && eq29::parameterField(index)==eq29::Mode) {
            p.enumValues.count=2; p.enumValues.restrictedMode=true;
            p.enumValues.values=new ParameterEnumerationValue[2];
            p.enumValues.values[0]={0,"Compression"}; p.enumValues.values[1]={1,"Upward expansion"};
        }
    }
    float getParameterValue(uint32_t i) const override { return engine_.values[eq29::toEngineParameter(i)]; }
    void setParameterValue(uint32_t i,float v) override { engine_.set(eq29::toEngineParameter(i),v); }
    void activate() override { engine_.reset(); }
    void sampleRateChanged(double rate) override { engine_.setSampleRate(rate); }
    void run(const float** in,float** out,uint32_t frames) override { const float* sidechain[2]={in[2],in[3]}; engine_.process(in,out,frames,sidechain); }
private:
    eq29::Engine engine_;
};
Plugin* createPlugin() { return new DynamicEQ; }
END_NAMESPACE_DISTRHO
