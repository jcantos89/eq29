// SPDX-License-Identifier: CC0-1.0
#include "Parameters.hpp"
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <fstream>
#include <dlfcn.h>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static constexpr const char* PluginURI="https://github.com/jcantos89/eq29#my_dynamic_eq";
static void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
struct URIs {
    std::map<std::string,uint32_t> ids;
    std::map<uint32_t,std::string> names;
    static uint32_t map(void* h,const char* s) {
        auto& self=*static_cast<URIs*>(h);
        auto it=self.ids.find(s); if(it!=self.ids.end()) return it->second;
        uint32_t id=uint32_t(self.ids.size()+1); self.ids[s]=id; self.names[id]=s; return id;
    }
    static const char* unmap(void* h,uint32_t id) {
        auto& self=*static_cast<URIs*>(h);auto it=self.names.find(id);
        return it==self.names.end()?nullptr:it->second.c_str();
    }
};
struct Ports {
    std::map<std::string,unsigned> indices;
    std::vector<float> control;
    std::vector<bool> input;
    uint32_t floatType=0;
    unsigned restored=0,writes=0;
    float& at(const std::string& symbol) { return control.at(indices.at(symbol)); }
    static const void* get(const char* symbol,void* h,uint32_t* size,uint32_t* type) {
        auto& p=*static_cast<Ports*>(h); auto it=p.indices.find(symbol);
        if(it==p.indices.end()||!p.input[it->second]) return nullptr;
        *size=sizeof(float); *type=p.floatType; return &p.control[it->second];
    }
    static void set(const char* symbol,void* h,const void* value,uint32_t size,uint32_t type) {
        auto& p=*static_cast<Ports*>(h);
        require(size==sizeof(float)&&type==p.floatType,"state value type");
        p.at(symbol)=*static_cast<const float*>(value); ++p.restored;
    }
    static void write(LV2UI_Controller h,uint32_t port,uint32_t size,uint32_t format,const void* value) {
        auto& p=*static_cast<Ports*>(h);
        if (format==0 && size==sizeof(float) && port<p.control.size()) {
            p.control[port]=*static_cast<const float*>(value); ++p.writes;
        }
    }
};
int main(int argc,char** argv) {
    try {
        require(argc>=2,"usage: eq29_lv2_tests ABSOLUTE_BUNDLE_PATH [--ui]");
        const bool uiTest=argc>2 && std::string(argv[2])=="--ui";
        URIs uris; LV2_URID_Map map{&uris,URIs::map};LV2_URID_Unmap unmap{&uris,URIs::unmap};
        const int32_t blockSize=256; const float sampleRate=48000;
        LV2_Options_Option options[]={
            {LV2_OPTIONS_INSTANCE,0,map.map(map.handle,LV2_BUF_SIZE__maxBlockLength),sizeof(blockSize),map.map(map.handle,LV2_ATOM__Int),&blockSize},
            {LV2_OPTIONS_INSTANCE,0,map.map(map.handle,LV2_PARAMETERS__sampleRate),sizeof(sampleRate),map.map(map.handle,LV2_ATOM__Float),&sampleRate},
            {}};
        LV2_Feature mapFeature{LV2_URID__map,&map},optionsFeature{LV2_OPTIONS__options,options};
        const LV2_Feature* features[]={&mapFeature,&optionsFeature,nullptr};
        auto* world=lilv_world_new();
        auto* bundle=lilv_new_file_uri(world,nullptr,argv[1]); lilv_world_load_bundle(world,bundle);
        auto* uri=lilv_new_uri(world,PluginURI);
        const auto* plugin=lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world),uri);
        require(plugin && lilv_plugin_verify(plugin),"LV2 discovery and metadata validation");
        auto* audio=lilv_new_uri(world,LV2_CORE__AudioPort);
        auto* control=lilv_new_uri(world,LV2_CORE__ControlPort);
        auto* input=lilv_new_uri(world,LV2_CORE__InputPort);
        const unsigned count=lilv_plugin_get_num_ports(plugin);
        Ports ports; ports.control.resize(count); ports.input.resize(count);
        ports.floatType=map.map(map.handle,LV2_ATOM__Float);
        std::vector<unsigned> inputs,outputs;
        unsigned controls=0;
        std::vector<float> minimum(count),maximum(count),def(count);
        lilv_plugin_get_port_ranges_float(plugin,minimum.data(),maximum.data(),def.data());
        for(unsigned i=0;i<count;++i) {
            const auto* port=lilv_plugin_get_port_by_index(plugin,i);
            const std::string symbol=lilv_node_as_string(lilv_port_get_symbol(plugin,port));
            ports.indices[symbol]=i;
            if(lilv_port_is_a(plugin,port,audio)) {
                (lilv_port_is_a(plugin,port,input)?inputs:outputs).push_back(i);
            } else {
                require(lilv_port_is_a(plugin,port,control),"unexpected port kind");
                ports.input[i]=lilv_port_is_a(plugin,port,input);
                require(std::isfinite(minimum[i])&&std::isfinite(maximum[i])&&minimum[i]<=maximum[i],"control metadata bounds");
                if(ports.input[i]) require(std::isfinite(def[i])&&minimum[i]<=def[i]&&def[i]<=maximum[i],"input control metadata default");
                ports.control[i]=std::isfinite(def[i])?def[i]:0; ++controls;
            }
        }
        require(inputs.size()==2&&outputs.size()==2&&controls==eq29::ParameterCount,"stereo audio and complete control ports");
        auto* instance=lilv_plugin_instantiate(plugin,48000,features); require(instance,"LV2 instantiate");
        std::array<std::array<float,256>,4> audioBuffers{};
        for(unsigned i=0;i<count;++i) if(lilv_port_is_a(plugin,lilv_plugin_get_port_by_index(plugin,i),control))
            lilv_instance_connect_port(instance,i,&ports.control[i]);
        for(unsigned i=0;i<2;++i) {
            lilv_instance_connect_port(instance,inputs[i],audioBuffers[i].data());
            lilv_instance_connect_port(instance,outputs[i],audioBuffers[i+2].data());
        }
        // Configure a nontrivial state with a removed default band and a newly enabled slot.
        ports.at("band1_type")=0; ports.at("band1_frequency")=1000; ports.at("band1_gain")=6;
        ports.at("band1_dynamics")=0; ports.at("band2_enabled")=0; ports.at("band3_enabled")=0;
        ports.at("band4_enabled")=0; ports.at("band8_enabled")=1; ports.at("band8_type")=2;
        ports.at("band8_gain")=-3; ports.at("band8_frequency")=6000; ports.at("band8_threshold")=-31;
        ports.at("band8_ratio")=7; ports.at("band8_attack")=17; ports.at("band8_release")=432;
        ports.at("band8_range")=9; ports.at("band8_mode")=1; ports.at("band8_dynamics")=0;
        lilv_instance_activate(instance);
        auto* state=lilv_state_new_from_instance(plugin,instance,&map,nullptr,nullptr,nullptr,nullptr,Ports::get,&ports,0,features);
        require(state,"capture LV2 state");
        char* text=lilv_state_to_string(world,&map,&unmap,state,"urn:eq29:test-state",nullptr);
        require(text && std::strstr(text,"band8_enabled"),"serialize control-port state to Turtle");
        auto* restored=lilv_state_new_from_string(world,&map,text); require(restored,"deserialize state");
        const auto saved=ports.control;
        for(unsigned i=0;i<count;++i) if(ports.input[i]) ports.control[i]=def[i];
        lilv_state_restore(restored,instance,Ports::set,&ports,0,features);
        unsigned inputCount=0;
        for(unsigned i=0;i<count;++i) if(ports.input[i]) { ++inputCount; require(ports.control[i]==saved[i],"parameter round trip"); }
        require(ports.restored==inputCount,"restore every input control");
        auto runTone=[&](unsigned blocks) {
            double powerIn=0,powerOut=0;
            for(unsigned block=0;block<blocks;++block) {
                for(unsigned i=0;i<256;++i) audioBuffers[0][i]=audioBuffers[1][i]=float(.1*std::sin(2*3.141592653589793*1000*(block*256+i)/48000));
                lilv_instance_run(instance,256);
                for(unsigned i=0;i<256;++i) {
                    require(std::isfinite(audioBuffers[2][i])&&std::isfinite(audioBuffers[3][i]),"LV2 finite output");
                    if(block>blocks/2) { powerIn+=audioBuffers[0][i]*audioBuffers[0][i];powerOut+=audioBuffers[2][i]*audioBuffers[2][i]; }
                }
            }
            return 10*std::log10(powerOut/powerIn);
        };
        const double db=runTone(200); require(db>5.5&&db<6.1,"LV2 audio EQ response after restore");
        ports.at("band1_gain")=0; ports.at("band1_dynamics")=1;ports.at("band1_threshold")=-40;
        ports.at("band1_range")=8;ports.at("band1_attack")=1;
        require(runTone(200)<-7.7,"LV2 dynamic compression");
        require(ports.at("band1_dynamic_gain")<-7.7,"LV2 live output meter");
        require(ports.at("sample_rate")==48000,"sample-rate output");
        ports.at("bypass")=1; require(std::abs(runTone(100))<.01,"LV2 bypass");
        auto* uis=lilv_plugin_get_uis(plugin); require(lilv_uis_size(uis)==1,"one registered custom UI");
        const auto* ui=lilv_uis_get(uis,lilv_uis_begin(uis));
        char* binary=lilv_node_get_path(lilv_ui_get_binary_uri(ui),nullptr);
        void* library=dlopen(binary,RTLD_NOW|RTLD_LOCAL);
        if(!library) {std::cerr<<dlerror()<<'\n';throw std::runtime_error("load GUI shared library");}
        auto descriptor=reinterpret_cast<LV2UI_DescriptorFunction>(dlsym(library,"lv2ui_descriptor"));
        require(descriptor && descriptor(0) && !descriptor(1),"GUI descriptor");
        if(uiTest) {
            Display* display=XOpenDisplay(nullptr); require(display,"X11 display required for --ui (use xvfb-run)");
            Window parent=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,1020,700,0,0,0);
            XMapWindow(display,parent); XSync(display,False);
            LV2_Feature parentFeature{LV2_UI__parent,reinterpret_cast<void*>(parent)};
            LV2_Feature idleFeature{LV2_UI__idleInterface,nullptr};
            const LV2_Feature* uiFeatures[]={&mapFeature,&optionsFeature,&parentFeature,&idleFeature,nullptr};
            const auto* desc=descriptor(0);LV2UI_Widget widget=nullptr;
            auto handle=desc->instantiate(desc,PluginURI,argv[1],Ports::write,&ports,&widget,uiFeatures);
            require(handle && widget,"instantiate and embed ImGui GUI");
            const auto* idle=static_cast<const LV2UI_Idle_Interface*>(desc->extension_data(LV2_UI__idleInterface));
            require(idle,"GUI idle interface");
            for(unsigned i=0;i<count;++i) if(lilv_port_is_a(plugin,lilv_plugin_get_port_by_index(plugin,i),control))
                desc->port_event(handle,i,sizeof(float),0,&ports.control[i]);
            for(unsigned frame=0;frame<80;++frame) {
                while(XPending(display)) {XEvent event;XNextEvent(display,&event);}
                require(idle->idle(handle)==0,"GUI render/event loop");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            // Exercise the actual GUI-to-host control bridge using X11 mouse events.
            const Window child=reinterpret_cast<Window>(widget);
            auto pump=[&](unsigned frames=5) {
                XSync(display,False);
                for(unsigned f=0;f<frames;++f) {
                    require(idle->idle(handle)==0,"GUI interaction loop");
                    std::this_thread::sleep_for(std::chrono::milliseconds(12));
                }
            };
            auto move=[&](int x,int y) {
                XWarpPointer(display,None,child,0,0,0,0,x,y); pump();
            };
            auto button=[&](bool down,int x,int y) {
                XEvent event{};
                event.xbutton.type=down?ButtonPress:ButtonRelease;
                event.xbutton.display=display; event.xbutton.window=child;
                event.xbutton.root=DefaultRootWindow(display);
                event.xbutton.x=x; event.xbutton.y=y;
                event.xbutton.button=Button1; event.xbutton.same_screen=True;
                event.xbutton.state=down?0:Button1Mask;
                XSendEvent(display,child,False,down?ButtonPressMask:ButtonReleaseMask,&event);
                pump();
            };
            auto click=[&](int x,int y) {move(x,y);button(true,x,y);button(false,x,y);};
            // At 1020x700, the response ends at y=374; band buttons are at y=403.
            // The saved state above leaves only slots 1 and 8 enabled.
            click(150,403);
            require(ports.at("band2_enabled")==1,"GUI add band writes enabled control");
            require(ports.at("band2_type")==0,"GUI new band is a bell");
            // Remove is on the selected-band toolbar, independent of active count.
            click(96,430);
            require(ports.at("band2_enabled")==0,"GUI remove band writes enabled control");
            // Drag band 1's node from 1 kHz / 0 dB towards 2 kHz / +6 dB.
            move(586,200);button(true,586,200);move(679,171);button(false,679,171);
            require(ports.at("band1_frequency")>1800 && ports.at("band1_frequency")<2200,
                    "GUI node frequency drag reaches host");
            require(ports.at("band1_gain")>4 && ports.at("band1_gain")<8,
                    "GUI node gain drag reaches host");
            require(ports.writes>10,"GUI generated real parameter writes");
            XWindowAttributes attributes{};
            require(XGetWindowAttributes(display,reinterpret_cast<Window>(widget),&attributes) && attributes.width>0,"mapped GUI child window");
            XImage* image=XGetImage(display,reinterpret_cast<Window>(widget),0,0,
                                    unsigned(attributes.width),unsigned(attributes.height),AllPlanes,ZPixmap);
            require(image,"read rendered GUI pixels");
            unsigned varied=0;
            const auto first=XGetPixel(image,0,0);
            for(int y=0;y<attributes.height;y+=3) for(int x=0;x<attributes.width;x+=3)
                if(XGetPixel(image,x,y)!=first) ++varied;
            require(varied>1000,"GUI rendered controls and graph, not an empty window");
            if(argc>3) {
                std::ofstream screenshot(argv[3],std::ios::binary);
                require(bool(screenshot),"open screenshot file");
                screenshot<<"P6\n"<<attributes.width<<' '<<attributes.height<<"\n255\n";
                auto component=[](unsigned long pixel,unsigned long mask) {
                    unsigned shift=0; while(mask && !(mask&1)) {mask>>=1;++shift;}
                    return static_cast<char>(mask?((pixel>>shift)&mask)*255/mask:0);
                };
                for(int y=0;y<attributes.height;++y) for(int x=0;x<attributes.width;++x) {
                    const auto pixel=XGetPixel(image,x,y);
                    screenshot.put(component(pixel,image->red_mask));
                    screenshot.put(component(pixel,image->green_mask));
                    screenshot.put(component(pixel,image->blue_mask));
                }
            }
            XDestroyImage(image);
            desc->cleanup(handle);XDestroyWindow(display,parent);XCloseDisplay(display);
            std::cout<<"GUI: embedded X11 ImGui editor instantiated and rendered 80 frames.\n";
        }
        dlclose(library);lilv_free(binary);lilv_uis_free(uis);
        lilv_instance_deactivate(instance);lilv_instance_free(instance);
        lilv_state_free(state);lilv_state_free(restored);lilv_free(text);
        lilv_node_free(audio);lilv_node_free(control);lilv_node_free(input);lilv_node_free(uri);lilv_node_free(bundle);lilv_world_free(world);
        std::cout<<"LV2: metadata, "<<inputCount<<" input controls restored, audio EQ, dynamics, metering, bypass and GUI binary passed.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
