// SPDX-License-Identifier: CC0-1.0
#include "Engine.hpp"
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using namespace eq29;
static unsigned checks=0;
static void require(bool pass,const char* message) {
    ++checks; if (!pass) throw std::runtime_error(message);
}
static void isolate(Engine& e,unsigned type=Bell) {
    for (unsigned b=0;b<MaxBands;++b) e.set(param(b,Enabled),b==0?1:0);
    e.set(param(0,Type),float(type)); e.set(param(0,Frequency),1000);
    e.set(param(0,Q),2); e.set(param(0,Attack),1); e.set(param(0,Release),40);
}
static double tone(Engine& e,double hz,double amplitude,double rate,bool rightOnly=false) {
    constexpr unsigned block=137;
    float l[block],r[block],ol[block],orr[block];
    const float* in[]={l,r}; float* out[]={ol,orr};
    double inputPower=0,outputPower=0;
    const unsigned frames=unsigned(rate*1.5);
    for (unsigned pos=0;pos<frames;pos+=block) {
        const unsigned n=std::min(block,frames-pos);
        for (unsigned i=0;i<n;++i) {
            const float x=float(amplitude*std::sin(2*Pi*hz*(pos+i)/rate));
            l[i]=rightOnly?0:x; r[i]=x;
        }
        e.process(in,out,n);
        for (unsigned i=0;i<n;++i) {
            require(std::isfinite(ol[i]) && std::isfinite(orr[i]),"finite audio");
            if (pos+i>frames/2) { inputPower+=double(r[i])*r[i]; outputPower+=double(orr[i])*orr[i]; }
            if (!rightOnly) require(std::abs(ol[i]-orr[i])<1.e-6f,"linked stereo symmetry");
            else require(ol[i]==0,"no channel crosstalk");
        }
    }
    return 10*std::log10(outputPower/inputPower);
}
int main() {
    try {
        for (double rate:{44100.,48000.,96000.,192000.}) {
            Engine e(rate); isolate(e); e.set(param(0,Dynamics),0); e.reset();
            require(std::abs(tone(e,1000,.1,rate))<.001,"flat unity");
            for (unsigned type:{Bell,LowShelf,HighShelf}) {
                isolate(e,type); e.set(param(0,Dynamics),0); e.set(param(0,Q),.70710678f);
                e.set(param(0,Gain),9); e.reset();
                const double hz=type==Bell?1000:type==LowShelf?100:10000;
                const double expected=gainToDb(equalizer(type,1000,.70710678,9,rate).magnitude(hz,rate));
                require(std::abs(tone(e,hz,.05,rate)-expected)<.03,"EQ matches analytic response");
            }
            isolate(e); e.set(param(0,Gain),0); e.set(param(0,Dynamics),1);
            e.set(param(0,Threshold),-30); e.set(param(0,Ratio),4); e.set(param(0,Range),8); e.reset();
            require(tone(e,1000,.5,rate)<-7.8,"in-band compression");
            require(std::abs(e.values[MeterBase]+8)<.05,"compression range cap");
            e.reset(); require(std::abs(tone(e,10000,.1,rate))<.1,"off-band rejection");
            require(std::abs(e.values[MeterBase])<.1,"off-band detector stays below threshold");
            e.set(param(0,Mode),1); e.reset();
            require(tone(e,1000,.5,rate)>7.8,"upward expansion");
            e.set(param(0,Ratio),1); e.reset(); require(std::abs(tone(e,1000,.1,rate))<.01,"1:1 disables dynamics");
            e.set(param(0,Ratio),4); e.set(Bypass,1); e.reset();
            require(std::abs(tone(e,1000,.1,rate))<.001,"bypass unity");
            e.set(Bypass,0); e.reset(); tone(e,1000,.5,rate,true);
            require(e.values[MeterBase]>7.8,"right channel triggers linked detector");
        }
        for(unsigned type:{LowShelf,HighShelf}) {
            Engine shelf; isolate(shelf,type); shelf.set(param(0,Threshold),-30);
            shelf.set(param(0,Range),8); shelf.reset();
            const double on=type==LowShelf?100:10000,off=type==LowShelf?10000:100;
            tone(shelf,on,.5,48000);
            require(shelf.values[MeterBase]<-7.8,"shelf detector triggers in its own region");
            shelf.reset(); tone(shelf,off,.1,48000);
            require(std::abs(shelf.values[MeterBase])<.1,"shelf detector rejects opposite region");
        }
        auto timingEngine=[](float attack,float release) {
            Engine e; isolate(e); e.set(param(0,Threshold),-40); e.set(param(0,Range),24);
            e.set(param(0,Attack),attack); e.set(param(0,Release),release); e.reset();return e;
        };
        Engine fast=timingEngine(.1f,5),slow=timingEngine(200,1000);
        std::array<float,4800> burst{},timedL{},timedR{},silence{};
        for(unsigned i=0;i<burst.size();++i) burst[i]=float(.5*std::sin(2*Pi*1000*i/48000));
        const float* burstIn[]={burst.data(),burst.data()}; float* timedOut[]={timedL.data(),timedR.data()};
        fast.process(burstIn,timedOut,480); slow.process(burstIn,timedOut,480);
        require(fast.values[MeterBase]<slow.values[MeterBase]-3,"attack setting changes transient response");
        fast=timingEngine(1,5); slow=timingEngine(1,1000);
        fast.process(burstIn,timedOut,4800); slow.process(burstIn,timedOut,4800);
        const float* silenceIn[]={silence.data(),silence.data()};
        fast.process(silenceIn,timedOut,4800); slow.process(silenceIn,timedOut,4800);
        require(std::abs(fast.values[MeterBase])<.1 && slow.values[MeterBase]<-10,
                "release setting changes recovery time");
        // Host block partitions must not affect the control-rate scheduling or samples.
        Engine a,b; isolate(a); isolate(b); a.reset(); b.reset();
        std::vector<float> signal(4096),oa(4096),ob(4096),ra(4096),rb(4096);
        std::mt19937 gen(29); std::uniform_real_distribution<float> noise(-.2f,.2f);
        for(auto& x:signal) x=noise(gen);
        const float* in[]={signal.data(),signal.data()}; float* out[]={oa.data(),ra.data()};
        a.process(in,out,4096);
        for(unsigned pos=0;pos<4096;) {
            unsigned n=std::min(17u,4096-pos); const float* ip[]={signal.data()+pos,signal.data()+pos};
            float* op[]={ob.data()+pos,rb.data()+pos}; b.process(ip,op,n); pos+=n;
        }
        require(oa==ob,"block-size invariant processing");
        // Parameter validation and worst-case automation stress, including all eight slots.
        Engine stress;
        stress.set(param(0,Frequency),NAN); require(stress.values[param(0,Frequency)]==spec(param(0,Frequency)).def,"NaN parameter default");
        stress.set(param(0,Q),100); require(stress.values[param(0,Q)]==12,"parameter clamp");
        float l[127]{},r[127]{},ol[127]{},orr[127]{}; const float* ip[]={l,r};float* op[]={ol,orr};
        for(unsigned block=0;block<1800;++block) {
            for(unsigned band=0;band<MaxBands;++band) {
                stress.set(param(band,Enabled),1);
                stress.set(param(band,Type),float((block/40+band)%3));
                stress.set(param(band,Frequency),(block%2)?20:20000);
                stress.set(param(band,Q),(block%2)?.15f:12);
                stress.set(param(band,Gain),(block%2)?-24:24);
                stress.set(param(band,Range),24); stress.set(param(band,Mode),float(block%2));
            }
            for(unsigned i=0;i<127;++i) { l[i]=noise(gen)*.001f; r[i]=noise(gen)*.001f; }
            stress.process(ip,op,127);
            for(unsigned i=0;i<127;++i) require(std::isfinite(ol[i])&&std::isfinite(orr[i]),"automation stability");
        }
        stress.reset();
        for(auto& x:l)x=0;
        for(auto& x:r)x=0;
        stress.process(ip,op,127);
        for(unsigned i=0;i<127;++i) require(ol[i]==0 && orr[i]==0,"reset clears delay state");
        std::cout<<"DSP: "<<checks<<" checks passed (4 sample rates, filters, dynamics, stereo, bypass, partitioning, automation).\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1;}
}
