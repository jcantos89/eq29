// SPDX-License-Identifier: CC0-1.0
// A small native VST2 host: load, automation, parameter-state reload and audio.
#include "PortMapping.hpp"
#include <cstdint>
#ifndef _WIN32
# define __cdecl
#endif
#include "xaymar-vst2/vst.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
# define NOMINMAX
# include <windows.h>
#else
# include <dlfcn.h>
#endif
#ifdef EQ29_TEST_X11
# include <X11/Xlib.h>
# include <X11/Xutil.h>
#endif
using namespace eq29;
static void require(bool pass,const char* why) { if(!pass) throw std::runtime_error(why); }
static unsigned writes=0,beginEdits=0,endEdits=0;
static intptr_t VST_FUNCTION_INTERFACE host(vst_effect*,VST_HOST_OPCODE op,int32_t,int64_t,void*,float) {
    switch(op) {
    case VST_HOST_OPCODE_01: return 2400; // audioMasterVersion
    case VST_HOST_OPCODE_10: return 48000;
    case VST_HOST_OPCODE_11: return 256;
    case VST_HOST_OPCODE_0F: return 1; // accept editor resize
    case VST_HOST_OPCODE_00: ++writes; return 0;
    case VST_HOST_OPCODE_2B: ++beginEdits; return 0;
    case VST_HOST_OPCODE_2C: ++endEdits; return 0;
    default: return 0;
    }
}
static intptr_t dispatch(vst_effect* fx,VST_EFFECT_OPCODE op,int index=0,intptr_t value=0,void* ptr=nullptr,float real=0) {
    return fx->control(fx,op,index,value,ptr,real);
}
static void set(vst_effect* fx,unsigned index,float value) {
    const auto s=spec(index);
    fx->set_parameter(fx,toHostParameter(index),(value-s.min)/(s.max-s.min));
}
static float get(vst_effect* fx,unsigned index) {
    const auto s=spec(index);
    return s.min+(s.max-s.min)*fx->get_parameter(fx,toHostParameter(index));
}
static void close(vst_effect* fx) {
    dispatch(fx,VST_EFFECT_OPCODE_SUSPEND,0,0);
    dispatch(fx,VST_EFFECT_OPCODE_DESTROY);
}
static double tone(vst_effect* fx) {
    std::array<float,256> left{},right{},outL{},outR{};
    const float* input[]={left.data(),right.data()};float* output[]={outL.data(),outR.data()};
    double inPower=0,outPower=0;
    for(unsigned block=0;block<200;++block) {
        for(unsigned i=0;i<256;++i) left[i]=right[i]=float(.25*std::sin(2*3.141592653589793*1000*(block*256+i)/48000));
        fx->process_float(fx,input,output,256);
        for(unsigned i=0;i<256;++i) {
            require(std::isfinite(outL[i])&&std::isfinite(outR[i]),"finite VST2 output");
            require(std::abs(outL[i]-outR[i])<1.e-6f,"VST2 stereo link");
            if(block>100) {inPower+=left[i]*left[i];outPower+=outL[i]*outL[i];}
        }
    }
    return 10*std::log10(outPower/inPower);
}
#ifdef EQ29_TEST_X11
static void gui(vst_effect* fx) {
    Display* display=XOpenDisplay(nullptr);require(display,"--ui requires X11 (use xvfb-run)");
    Window parent=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,1020,700,0,0,0);
    XMapWindow(display,parent);XSync(display,False);
    vst_rect* rect=nullptr;
    require(dispatch(fx,VST_EFFECT_OPCODE_WINDOW_GETRECT,0,0,&rect)==1 && rect && rect->right>0,"VST2 editor size");
    require(dispatch(fx,VST_EFFECT_OPCODE_WINDOW_CREATE,0,0,reinterpret_cast<void*>(parent))==1,"VST2 editor embedding");
    auto pump=[&](unsigned frames=6) {
        XSync(display,False);
        for(unsigned f=0;f<frames;++f) {
            dispatch(fx,VST_EFFECT_OPCODE_13);
            std::this_thread::sleep_for(std::chrono::milliseconds(12));
        }
    };
    pump(80);
    Window root{},parentReturn{},*children=nullptr;unsigned childCount=0;
    require(XQueryTree(display,parent,&root,&parentReturn,&children,&childCount) && childCount>0,"VST2 native editor child");
    Window child=children[0];XFree(children);
    XWindowAttributes attr{};require(XGetWindowAttributes(display,child,&attr),"VST2 editor geometry");
    XImage* image=XGetImage(display,child,0,0,unsigned(attr.width),unsigned(attr.height),AllPlanes,ZPixmap);
    require(image,"VST2 editor pixels");unsigned varied=0;
    for(int y=0;y<attr.height;y+=3) for(int x=0;x<attr.width;x+=3)
        if(XGetPixel(image,x,y)!=XGetPixel(image,0,0)) ++varied;
    XDestroyImage(image);require(varied>1000,"VST2 ImGui graph and controls rendered");
    auto move=[&](int x,int y) {XWarpPointer(display,None,child,0,0,0,0,x,y);pump();};
    auto button=[&](bool down,int x,int y) {
        XEvent e{};e.xbutton.type=down?ButtonPress:ButtonRelease;e.xbutton.display=display;
        e.xbutton.window=child;e.xbutton.root=DefaultRootWindow(display);e.xbutton.x=x;e.xbutton.y=y;
        e.xbutton.button=Button1;e.xbutton.same_screen=True;e.xbutton.state=down?0:Button1Mask;
        XSendEvent(display,child,False,down?ButtonPressMask:ButtonReleaseMask,&e);pump();
    };
    auto click=[&](int x,int y) {move(x,y);button(true,x,y);button(false,x,y);};
    // Four defaults: click + Add, then remove the newly selected fifth slot.
    click(263,403);
    require(get(fx,param(4,Enabled))>.5f,"VST2 GUI add reaches host and DSP");
    click(96,430);
    require(get(fx,param(4,Enabled))<.5f,"VST2 GUI remove reaches host and DSP");
    move(277,200);button(true,277,200);move(371,171);button(false,371,171);
    require(get(fx,param(0,Frequency))>180 && get(fx,param(0,Frequency))<220,"VST2 node frequency automation");
    require(get(fx,param(0,Gain))>4 && get(fx,param(0,Gain))<8,"VST2 node gain automation");
    // Bypass lies after the eight band's parameters: exercise its mapping too.
    click(344,23);require(get(fx,Bypass)>.5f,"VST2 GUI bypass mapping");
    require(writes>10 && beginEdits>0 && beginEdits==endEdits,"balanced VST2 automation gestures");
    dispatch(fx,VST_EFFECT_OPCODE_WINDOW_DESTROY);XDestroyWindow(display,parent);XCloseDisplay(display);
    std::cout<<"VST2 GUI: embedded/rendered, add/remove, node dragging and bypass automation passed.\n";
}
#endif
int main(int argc,char** argv) {
    try {
        require(argc>=2,"usage: eq29_vst2_tests PLUGIN_BINARY [--ui]");
#ifdef _WIN32
        auto library=LoadLibraryA(argv[1]);require(library,"LoadLibrary VST2 binary");
        auto entry=reinterpret_cast<vst_effect* (*)(vst_host_callback)>(GetProcAddress(library,"VSTPluginMain"));
#else
        auto library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
        if(!library) { std::cerr<<dlerror()<<'\n';throw std::runtime_error("dlopen VST2 binary"); }
        auto entry=reinterpret_cast<vst_effect* (*)(vst_host_callback)>(dlsym(library,"VSTPluginMain"));
#endif
        require(entry,"VSTPluginMain export");
        auto create=[&]() {
            auto* fx=entry(host);require(fx && fx->magic_number==0x56737450,"VST2 descriptor");
            require(fx->num_inputs==2 && fx->num_outputs==2 && fx->num_params==int(VstInputCount),"stereo and all 98 input parameters exposed");
            require(fx->process_float && (fx->flags&1),"VST2 DSP and custom editor present");
            require(dispatch(fx,VST_EFFECT_OPCODE_CREATE)==1,"VST2 initialize");
            dispatch(fx,VST_EFFECT_OPCODE_SET_SAMPLE_RATE,0,0,nullptr,48000);
            dispatch(fx,VST_EFFECT_OPCODE_SET_BLOCK_SIZE,0,256);
            dispatch(fx,VST_EFFECT_OPCODE_SUSPEND,0,1);
            return fx;
        };
        for(unsigned i=0;i<ParameterCount;++i) require(toEngineParameter(toHostParameter(i))==i,"parameter map bijection");
        auto* fx=create();
        for(unsigned i=0;i<VstInputCount;++i) {
            require(dispatch(fx,VST_EFFECT_OPCODE_PARAM_ISAUTOMATABLE,int(i))==1,"every exposed VST2 parameter is automatable");
            require(!isOutput(toEngineParameter(i)),"VST2 host exposes no output controls");
        }
        char name[64]{};dispatch(fx,VST_EFFECT_OPCODE_PARAM_GETNAME,int(toHostParameter(Bypass)),0,name);
        require(std::strcmp(name,"Bypass")==0,"global bypass parameter ordering");
        for(unsigned b=0;b<MaxBands;++b) set(fx,param(b,Enabled),b==0||b==7?1:0);
        set(fx,param(0,Type),Bell);set(fx,param(0,Frequency),1000);set(fx,param(0,Gain),6);set(fx,param(0,Dynamics),0);
        set(fx,param(7,Type),HighShelf);set(fx,param(7,Frequency),6000);set(fx,param(7,Dynamics),0);
        set(fx,param(7,Threshold),-31);set(fx,param(7,Ratio),7);set(fx,param(7,Attack),17);
        set(fx,param(7,Release),432);set(fx,param(7,Range),9);set(fx,param(7,Mode),1);
        // VST2 hosts persist normalized parameter arrays when no opaque chunks are needed.
        std::vector<float> state(VstInputCount);
        for(unsigned i=0;i<VstInputCount;++i) state[i]=fx->get_parameter(fx,i);
        close(fx);fx=create();
        for(unsigned i=0;i<VstInputCount;++i) fx->set_parameter(fx,i,state[i]);
        for(unsigned i=0;i<VstInputCount;++i) require(std::abs(fx->get_parameter(fx,i)-state[i])<1.e-6f,"VST2 state reload into fresh instance");
        require(std::abs(tone(fx)-6)<.05,"VST2 restored bell EQ audio");
        set(fx,param(0,Gain),0);set(fx,param(0,Dynamics),1);set(fx,param(0,Threshold),-40);
        set(fx,param(0,Range),8);set(fx,param(0,Attack),1);
        require(tone(fx)<-7.8,"VST2 dynamic compression");
        set(fx,param(0,Mode),1);require(tone(fx)>7.8,"VST2 upward expansion");
        set(fx,param(0,Dynamics),0);set(fx,OutputGain,3);
        require(std::abs(tone(fx)-3)<.05,"VST2 output gain mapping");
        set(fx,Bypass,1);require(std::abs(tone(fx))<.01,"VST2 bypass mapping");
        close(fx);
        if(argc>2 && std::strcmp(argv[2],"--ui")==0) {
#ifdef EQ29_TEST_X11
            fx=create();gui(fx);close(fx);
#else
            throw std::runtime_error("--ui is currently implemented only for X11");
#endif
        }
#ifdef _WIN32
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        std::cout<<"VST2: native load, 98 inputs, state reload, static/dynamic audio, output gain and bypass passed.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
