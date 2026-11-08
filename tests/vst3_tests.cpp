// SPDX-License-Identifier: CC0-1.0
#include "PortMapping.hpp"
#include "travesty/factory.h"
#include "travesty/component.h"
#include "travesty/audio_processor.h"
#include "travesty/edit_controller.h"
#include "Vst3Host.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <iomanip>
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
# include <chrono>
# include <thread>
#endif
using namespace eq29;
static void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
static v3_result V3_API noQuery(void*,const v3_tuid,void** out) {*out=nullptr;return V3_NO_INTERFACE;}
static uint32_t V3_API ref(void*) {return 1;}
struct Stream : v3_bstream_cpp {
    std::vector<unsigned char> bytes;size_t position=0;
    Stream() {
        query_interface=noQuery;this->ref=::ref;unref=::ref;
        stream.read=[](void* self,void* buf,int32_t n,int32_t* read)->v3_result {
            auto& s=**static_cast<Stream**>(self);const size_t count=std::min(size_t(n),s.bytes.size()-s.position);
            if(count) std::memcpy(buf,s.bytes.data()+s.position,count);s.position+=count;
            if(read)*read=int32_t(count);return count>0?V3_OK:V3_FALSE;
        };
        stream.write=[](void* self,void* buf,int32_t n,int32_t* written)->v3_result {
            auto& s=**static_cast<Stream**>(self);s.bytes.resize(std::max(s.bytes.size(),s.position+size_t(n)));
            std::memcpy(s.bytes.data()+s.position,buf,size_t(n));s.position+=size_t(n);
            if(written)*written=n;return V3_OK;
        };
        stream.seek=[](void* self,int64_t pos,int32_t mode,int64_t* result)->v3_result {
            auto& s=**static_cast<Stream**>(self);
            const int64_t next=(mode==V3_SEEK_CUR?int64_t(s.position):mode==V3_SEEK_END?int64_t(s.bytes.size()):0)+pos;
            if(next<0 || size_t(next)>s.bytes.size())return V3_INVALID_ARG;
            s.position=size_t(next);if(result)*result=next;return V3_OK;
        };
        stream.tell=[](void* self,int64_t* result)->v3_result {*result=int64_t((*static_cast<Stream**>(self))->position);return V3_OK;};
    }
};
struct Queue : v3_param_value_queue_cpp {
    unsigned id=0;double value=0;
    Queue() {
        query_interface=noQuery;this->ref=::ref;unref=::ref;
        queue.get_param_id=[](void* self)->v3_param_id {return (*static_cast<Queue**>(self))->id;};
        queue.get_point_count=[](void*)->int32_t {return 1;};
        queue.get_point=[](void* self,int32_t i,int32_t* offset,double* value)->v3_result {
            if(i)return V3_INVALID_ARG;*offset=0;*value=(*static_cast<Queue**>(self))->value;return V3_OK;
        };
        queue.add_point=[](void* self,int32_t,double value,int32_t* index)->v3_result {(*static_cast<Queue**>(self))->value=value;if(index)*index=0;return V3_OK;};
    }
};
struct Changes : v3_param_changes_cpp {
    std::array<Queue,InputCount> points{};std::array<Queue*,InputCount> handles{};
    Changes(unsigned offset) {
        query_interface=noQuery;this->ref=::ref;unref=::ref;
        for(unsigned h=0;h<InputCount;++h) {
            const auto s=spec(toEngineParameter(h));points[h].id=offset+h;
            points[h].value=(s.def-s.min)/(s.max-s.min);handles[h]=&points[h];
        }
        changes.get_param_count=[](void*)->int32_t {return InputCount;};
        changes.get_param_data=[](void* self,int32_t idx)->v3_param_value_queue** {
            auto* s=*static_cast<Changes**>(self);
            return idx>=0 && unsigned(idx)<InputCount ? reinterpret_cast<v3_param_value_queue**>(&s->handles[size_t(idx)]) : nullptr;
        };
        changes.add_param_data=[](void*,const v3_param_id*,int32_t*)->v3_param_value_queue** {return nullptr;};
    }
    void set(unsigned engine,float value) {
        auto s=spec(engine);points[toHostParameter(engine)].value=(value-s.min)/(s.max-s.min);
    }
};
struct OutputChanges : v3_param_changes_cpp {
    std::array<Queue,ParameterCount+16> points{};
    std::array<Queue*,ParameterCount+16> handles{};
    unsigned count=0;
    OutputChanges() {
        query_interface=noQuery;this->ref=::ref;unref=::ref;
        changes.get_param_count=[](void* self)->int32_t{return int32_t((*static_cast<OutputChanges**>(self))->count);};
        changes.get_param_data=[](void* self,int32_t i)->v3_param_value_queue** {
            auto* s=*static_cast<OutputChanges**>(self);return i>=0 && unsigned(i)<s->count ? reinterpret_cast<v3_param_value_queue**>(&s->handles[size_t(i)]) : nullptr;
        };
        changes.add_param_data=[](void* self,const v3_param_id* id,int32_t* index)->v3_param_value_queue** {
            auto* s=*static_cast<OutputChanges**>(self);unsigned i=0;
            for(;i<s->count;++i)if(s->points[i].id==*id)break;
            if(i==s->count){if(i>=s->points.size())return nullptr;s->points[i].id=*id;s->handles[i]=&s->points[i];++s->count;}
            if(index)*index=int32_t(i);return reinterpret_cast<v3_param_value_queue**>(&s->handles[i]);
        };
    }
    double get(unsigned id) const {for(unsigned i=0;i<count;++i)if(points[i].id==id)return points[i].value;return -1;}
};
int main(int argc,char** argv) {
    try {
        require(argc>=2,"usage: eq29_vst3_tests NATIVE_VST3_BINARY [--ui]");
#ifdef _WIN32
        auto library=LoadLibraryA(argv[1]);require(library,"LoadLibrary VST3");
        auto symbol=[&](const char* name) {return reinterpret_cast<void*>(GetProcAddress(library,name));};
        auto init=reinterpret_cast<bool (*)()>(symbol("InitDll"));if(init)require(init(),"VST3 InitDll");
#else
        auto library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
        if(!library){std::cerr<<dlerror()<<'\n';throw std::runtime_error("load VST3 binary");}
        auto symbol=[&](const char* name) {return dlsym(library,name);};
# ifdef __APPLE__
        auto init=reinterpret_cast<bool (*)(void*)>(symbol("bundleEntry"));if(init)require(init(nullptr),"VST3 bundleEntry");
# else
        auto init=reinterpret_cast<bool (*)(void*)>(symbol("ModuleEntry"));if(init)require(init(library),"VST3 ModuleEntry");
# endif
#endif
        auto entry=reinterpret_cast<v3_plugin_factory** (*)()>(symbol("GetPluginFactory"));require(entry,"GetPluginFactory export");
        auto** factory=entry();require(factory,"VST3 factory instance");
        testhost::Host host;
        v3_class_info audioClass{};bool found=false;
        for(int32_t i=0;i<v3_cpp_obj(factory)->num_classes(factory);++i) {
            v3_class_info info{};require(v3_cpp_obj(factory)->get_class_info(factory,i,&info)==V3_OK,"class info");
            if(std::strcmp(info.category,"Audio Module Class")==0){audioClass=info;found=true;}
        }
        require(found,"VST3 audio processor class");v3_component** component=nullptr;
        require(v3_cpp_obj(factory)->create_instance(factory,audioClass.class_id,v3_component_iid,reinterpret_cast<void**>(&component))==V3_OK,"create VST3 component");
        require(v3_cpp_obj_initialize(component,host.context())==V3_OK,"initialize VST3 component");
        require(v3_cpp_obj(component)->get_bus_count(component,V3_AUDIO,V3_INPUT)==2,"main and external sidechain buses");
        v3_bus_info main{},side{},output{};
        require(v3_cpp_obj(component)->get_bus_info(component,V3_AUDIO,V3_INPUT,0,&main)==V3_OK && main.channel_count==2 && main.bus_type==V3_MAIN,"stereo main bus");
        require(v3_cpp_obj(component)->get_bus_info(component,V3_AUDIO,V3_INPUT,1,&side)==V3_OK && side.channel_count==2 && side.bus_type==V3_AUX,"stereo auxiliary sidechain");
        require(v3_cpp_obj(component)->get_bus_info(component,V3_AUDIO,V3_OUTPUT,0,&output)==V3_OK && output.channel_count==2,"stereo output bus");
        v3_audio_processor** processor=nullptr;
        require(v3_cpp_obj_query_interface(component,v3_audio_processor_iid,&processor)==V3_OK,"audio processor interface");
        v3_edit_controller** controller=nullptr;
        if(v3_cpp_obj_query_interface(component,v3_edit_controller_iid,&controller)!=V3_OK) {
            v3_tuid id{};require(v3_cpp_obj(component)->get_controller_class_id(component,id)==V3_OK,"controller class id");
            require(v3_cpp_obj(factory)->create_instance(factory,id,v3_edit_controller_iid,reinterpret_cast<void**>(&controller))==V3_OK,"create controller");
            require(v3_cpp_obj_initialize(controller,host.context())==V3_OK,"initialize controller");
        }
        v3_connection_point** compConnection=nullptr,**ctrlConnection=nullptr;
        if(v3_cpp_obj_query_interface(component,v3_connection_point_iid,&compConnection)==V3_OK &&
           v3_cpp_obj_query_interface(controller,v3_connection_point_iid,&ctrlConnection)==V3_OK) {
            v3_cpp_obj(compConnection)->connect(compConnection,ctrlConnection);
            v3_cpp_obj(ctrlConnection)->connect(ctrlConnection,compConnection);
        }
        unsigned offset=0;bool parameterFound=false;
        for(int i=0;i<v3_cpp_obj(controller)->get_parameter_count(controller);++i) {
            v3_param_info info{};require(v3_cpp_obj(controller)->get_parameter_info(controller,i,&info)==V3_OK,"parameter metadata");
            std::string title;for(auto ch:info.title){if(!ch)break;title+=char(ch);}
            if(title=="Band 1 Enabled"){offset=info.param_id;parameterFound=true;break;}
        }
        require(parameterFound,"VST3 input parameter identity");
        Changes change(offset);Changes* changesHandle=&change;
        for(unsigned b=0;b<MaxBands;++b)change.set(param(b,Enabled),b==15?1:0);
        change.set(param(15,Type),Bell);change.set(param(15,Frequency),1000);change.set(param(15,Gain),6);
        change.set(param(15,Dynamics),0);change.set(param(15,Q),2);change.set(param(15,Release),40);
        v3_speaker_arrangement inArr[2]={3,3},outArr[1]={3};
        require(v3_cpp_obj(processor)->set_bus_arrangements(processor,inArr,2,outArr,1)==V3_OK,"VST3 bus arrangements");
        v3_process_setup setup{V3_REALTIME,V3_SAMPLE_32,256,48000};
        require(v3_cpp_obj(processor)->setup_processing(processor,&setup)==V3_OK,"VST3 setup processing");
        for(int i=0;i<2;++i)require(v3_cpp_obj(component)->activate_bus(component,V3_AUDIO,V3_INPUT,i,1)==V3_OK,"activate input bus");
        require(v3_cpp_obj(component)->activate_bus(component,V3_AUDIO,V3_OUTPUT,0,1)==V3_OK,"activate output bus");
        require(v3_cpp_obj(component)->set_active(component,1)==V3_OK,"VST3 activate");
        require(v3_cpp_obj(processor)->set_processing(processor,1)==V3_OK,"VST3 start processing");
        std::array<float,256> mainL{},mainR{},scL{},scR{},outL{},outR{};
        float* mainChannels[]={mainL.data(),mainR.data()},*scChannels[]={scL.data(),scR.data()},*outChannels[]={outL.data(),outR.data()};
        v3_audio_bus_buffers inputs[2]{};inputs[0].num_channels=inputs[1].num_channels=2;
        inputs[0].channel_buffers_32=mainChannels;inputs[1].channel_buffers_32=scChannels;
        v3_audio_bus_buffers outputs[1]{};outputs[0].num_channels=2;outputs[0].channel_buffers_32=outChannels;
        v3_process_data data{};data.process_mode=V3_REALTIME;data.symbolic_sample_size=V3_SAMPLE_32;data.nframes=256;
        data.num_input_buses=2;data.num_output_buses=1;data.inputs=inputs;data.outputs=outputs;
        OutputChanges outputChanges;OutputChanges* outputHandle=&outputChanges;
        data.output_params=reinterpret_cast<v3_param_changes**>(&outputHandle);
        auto run=[&](bool sendChanges,double sideHz,bool silentMain=false) {
            double inPower=0,outPower=0;
            // After a real signal, IIR tails and fused multiply-add rounding need
            // not reach binary zero. -300 dBFS still excludes any signal leakage.
            constexpr float silenceFloor=1.e-15f;
            for(unsigned block=0;block<240;++block) {
                for(unsigned i=0;i<256;++i) {
                    const double t=(block*256+i)/48000.;
                    mainL[i]=mainR[i]=silentMain?0:float(.1*std::sin(2*3.141592653589793*1000*t));
                    scL[i]=scR[i]=float(.5*std::sin(2*3.141592653589793*sideHz*t));
                }
                data.input_params=sendChanges&&block==0 ? reinterpret_cast<v3_param_changes**>(&changesHandle) : nullptr;
                require(v3_cpp_obj(processor)->process(processor,&data)==V3_OK,"VST3 audio callback");
                for(unsigned i=0;i<256;++i) {
                    require(std::isfinite(outL[i]) && std::isfinite(outR[i]),"finite VST3 output");
                    if(silentMain && block>120 && (std::abs(outL[i])>silenceFloor || std::abs(outR[i])>silenceFloor)) {
                        std::ostringstream details;
                        details<<"no sidechain leakage: residual "<<std::scientific<<std::setprecision(9)
                               <<std::max(std::abs(outL[i]),std::abs(outR[i]))<<" at block "<<block;
                        throw std::runtime_error(details.str());
                    }
                    if(block>120){inPower+=mainL[i]*mainL[i];outPower+=outL[i]*outL[i];}
                }
            }
            return silentMain?0:10*std::log10(outPower/inPower);
        };
        require(std::abs(run(true,0)-6)<.05,"VST3 band 16 static EQ");
        bool spectrumSeen=false;
        for(unsigned bin=0;bin<SpectrumBins;++bin)
            if(outputChanges.get(offset+toHostParameter(SpectrumInBase+bin))>.5) spectrumSeen=true;
        require(spectrumSeen,"VST3 publishes real spectrum meter data");
        Stream state;Stream* stateHandle=&state;
        require(v3_cpp_obj(component)->get_state(component,reinterpret_cast<v3_bstream**>(&stateHandle))==V3_OK && !state.bytes.empty(),"save VST3 component state");
        change.set(param(15,Gain),0);change.set(param(15,Dynamics),1);change.set(param(15,Threshold),-30);
        change.set(param(15,Range),8);change.set(param(15,Attack),1);change.set(SidechainBase+15,1);
        require(run(true,1000)<-7.8,"VST3 external sidechain compression");
        require(std::abs(run(false,10000))<.1,"VST3 sidechain is band selective");
        run(false,1000,true);
        require(v3_cpp_obj(processor)->set_processing(processor,0)==V3_OK,"stop VST3 processing");
        require(v3_cpp_obj(component)->set_active(component,0)==V3_OK,"deactivate VST3");
        state.position=0;
        require(v3_cpp_obj(component)->set_state(component,reinterpret_cast<v3_bstream**>(&stateHandle))==V3_OK,"restore VST3 component state");
        require(v3_cpp_obj(component)->set_active(component,1)==V3_OK,"reactivate VST3");
        require(v3_cpp_obj(processor)->set_processing(processor,1)==V3_OK,"restart VST3");
        require(std::abs(run(false,0)-6)<.05,"restored VST3 state changes actual audio");
#ifdef EQ29_TEST_X11
        if(argc>2 && std::strcmp(argv[2],"--ui")==0) {
            for(unsigned i=0;i<outputChanges.count;++i)
                v3_cpp_obj(controller)->set_parameter_normalised(controller,outputChanges.points[i].id,outputChanges.points[i].value);
            auto** view=v3_cpp_obj(controller)->create_view(controller,"editor");require(view,"VST3 editor view");
            Display* display=XOpenDisplay(nullptr);require(display,"--ui requires X11");
            Window parent=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,1020,700,0,0,0);
            XMapWindow(display,parent);XSync(display,False);
            testhost::Frame frame;
            require(v3_cpp_obj(view)->set_frame(view,reinterpret_cast<v3_plugin_frame**>(&frame.handle))==V3_OK,"VST3 editor frame");
            require(v3_cpp_obj(view)->attached(view,reinterpret_cast<void*>(parent),V3_VIEW_PLATFORM_TYPE_X11)==V3_OK,"VST3 native editor attach");
            require(frame.runLoop.timer,"VST3 registered event-loop timer");
            for(unsigned i=0;i<80;++i){frame.runLoop.tick();std::this_thread::sleep_for(std::chrono::milliseconds(12));}
            Window root{},parentReturn{},*children=nullptr;unsigned count=0;
            require(XQueryTree(display,parent,&root,&parentReturn,&children,&count) && count,"VST3 embedded child");
            XWindowAttributes attr{};require(XGetWindowAttributes(display,children[0],&attr),"VST3 editor geometry");
            XImage* image=XGetImage(display,children[0],0,0,unsigned(attr.width),unsigned(attr.height),AllPlanes,ZPixmap);
            require(image,"VST3 rendered pixels");unsigned varied=0;
            for(int y=0;y<attr.height;y+=3)for(int x=0;x<attr.width;x+=3)if(XGetPixel(image,x,y)!=XGetPixel(image,0,0))++varied;
            require(varied>1000,"VST3 renders ImGui controls and graph");XDestroyImage(image);XFree(children);
            v3_cpp_obj(view)->removed(view);v3_cpp_obj_unref(view);XDestroyWindow(display,parent);XCloseDisplay(display);
            std::cout<<"VST3 GUI: native X11 editor attached and rendered via host timer.\n";
        }
#endif
        v3_cpp_obj(processor)->set_processing(processor,0);v3_cpp_obj(component)->set_active(component,0);
        if(compConnection && ctrlConnection) {
            v3_cpp_obj(compConnection)->disconnect(compConnection,ctrlConnection);
            v3_cpp_obj(ctrlConnection)->disconnect(ctrlConnection,compConnection);
        }
        if(compConnection)v3_cpp_obj_unref(compConnection);if(ctrlConnection)v3_cpp_obj_unref(ctrlConnection);
        v3_cpp_obj_terminate(controller);
        v3_cpp_obj_unref(controller);v3_cpp_obj_unref(processor);v3_cpp_obj_terminate(component);v3_cpp_obj_unref(component);v3_cpp_obj_unref(factory);
#ifdef _WIN32
        auto exitModule=reinterpret_cast<bool (*)()>(symbol("ExitDll"));if(exitModule)exitModule();FreeLibrary(library);
#else
# ifdef __APPLE__
        auto exitModule=reinterpret_cast<bool (*)()>(symbol("bundleExit"));if(exitModule)exitModule();
# else
        auto exitModule=reinterpret_cast<bool (*)()>(symbol("ModuleExit"));if(exitModule)exitModule();
# endif
        dlclose(library);
#endif
        std::cout<<"VST3: factory, main/aux buses, band 16, audio, external sidechain and state restore passed.\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
