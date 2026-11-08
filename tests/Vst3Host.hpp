// SPDX-License-Identifier: CC0-1.0
// Minimal host message/attribute support for exercising DPF's native VST3 editor.
#pragma once
#include "travesty/host.h"
#include "travesty/view.h"
#include <map>
#include <string>
#include <vector>
#include <cstring>

namespace testhost {
inline v3_result V3_API noQuery(void*,const v3_tuid,void** out) {*out=nullptr;return V3_NO_INTERFACE;}
inline uint32_t V3_API ref(void*) {return 1;}
struct Attributes : v3_attribute_list_cpp {
    Attributes* handle=this;
    std::map<std::string,int64_t> ints;
    std::map<std::string,double> floats;
    std::map<std::string,std::vector<int16_t>> strings;
    std::map<std::string,std::vector<unsigned char>> binaries;
    Attributes() {
        query_interface=noQuery;this->ref=unref=::testhost::ref;
        attrlist.set_int=[](void* s,const char* k,int64_t v)->v3_result {(*static_cast<Attributes**>(s))->ints[k]=v;return V3_OK;};
        attrlist.get_int=[](void* s,const char* k,int64_t* v)->v3_result {auto& m=(*static_cast<Attributes**>(s))->ints;auto i=m.find(k);if(i==m.end())return V3_FALSE;*v=i->second;return V3_OK;};
        attrlist.set_float=[](void* s,const char* k,double v)->v3_result {(*static_cast<Attributes**>(s))->floats[k]=v;return V3_OK;};
        attrlist.get_float=[](void* s,const char* k,double* v)->v3_result {auto& m=(*static_cast<Attributes**>(s))->floats;auto i=m.find(k);if(i==m.end())return V3_FALSE;*v=i->second;return V3_OK;};
        attrlist.set_string=[](void* s,const char* k,const int16_t* v)->v3_result {auto& a=(*static_cast<Attributes**>(s))->strings[k];a.clear();do{a.push_back(*v);}while(*v++);return V3_OK;};
        attrlist.get_string=[](void* s,const char* k,int16_t* v,uint32_t n)->v3_result {auto& m=(*static_cast<Attributes**>(s))->strings;auto i=m.find(k);if(i==m.end()||n<i->second.size()*2)return V3_FALSE;std::memcpy(v,i->second.data(),i->second.size()*2);return V3_OK;};
        attrlist.set_binary=[](void* s,const char* k,const void* v,uint32_t n)->v3_result {auto* b=static_cast<const unsigned char*>(v);(*static_cast<Attributes**>(s))->binaries[k]=std::vector<unsigned char>(b,b+n);return V3_OK;};
        attrlist.get_binary=[](void* s,const char* k,const void** v,uint32_t* n)->v3_result {auto& m=(*static_cast<Attributes**>(s))->binaries;auto i=m.find(k);if(i==m.end())return V3_FALSE;*v=i->second.data();*n=uint32_t(i->second.size());return V3_OK;};
    }
};
struct Message : v3_message_cpp {
    Message* handle=this;Attributes attributes;std::string id;unsigned refs=1;
    Message() {
        query_interface=noQuery;
        this->ref=[](void* s)->uint32_t{return ++(*static_cast<Message**>(s))->refs;};
        unref=[](void* s)->uint32_t{auto* m=*static_cast<Message**>(s);unsigned n=--m->refs;if(!n)delete m;return n;};
        msg.get_message_id=[](void* s)->const char*{return (*static_cast<Message**>(s))->id.c_str();};
        msg.set_message_id=[](void* s,const char* v){(*static_cast<Message**>(s))->id=v;};
        msg.get_attributes=[](void* s)->v3_attribute_list**{return reinterpret_cast<v3_attribute_list**>(&(*static_cast<Message**>(s))->attributes.handle);};
    }
};
struct Host : v3_host_application_cpp {
    Host* handle=this;
    Host() {
        query_interface=[](void* s,const v3_tuid iid,void** out)->v3_result {
            if(!std::memcmp(iid,v3_host_application_iid,16)||!std::memcmp(iid,v3_funknown_iid,16)){*out=s;return V3_OK;}
            *out=nullptr;return V3_NO_INTERFACE;
        };
        this->ref=unref=::testhost::ref;
        app.get_name=[](void*,v3_str_128 out)->v3_result{const char* name="EQ29 validation host";for(unsigned i=0;i<=std::strlen(name);++i)out[i]=name[i];return V3_OK;};
        app.create_instance=[](void*,v3_tuid,v3_tuid iid,void** out)->v3_result {
            if(std::memcmp(iid,v3_message_iid,16)){*out=nullptr;return V3_NO_INTERFACE;}
            auto* m=new Message;*out=&m->handle;return V3_OK;
        };
    }
    v3_funknown** context() {return reinterpret_cast<v3_funknown**>(&handle);}
};
struct Loop : v3_run_loop_cpp {
    Loop* handle=this;v3_timer_handler** timer=nullptr;
    Loop() {
        query_interface=noQuery;this->ref=unref=::testhost::ref;
        loop.register_event_handler=[](void*,v3_event_handler**,int)->v3_result{return V3_OK;};
        loop.unregister_event_handler=[](void*,v3_event_handler**)->v3_result{return V3_OK;};
        loop.register_timer=[](void* s,v3_timer_handler** h,uint64_t)->v3_result{(*static_cast<Loop**>(s))->timer=h;return V3_OK;};
        loop.unregister_timer=[](void* s,v3_timer_handler**)->v3_result{(*static_cast<Loop**>(s))->timer=nullptr;return V3_OK;};
    }
    void tick(){if(timer)v3_cpp_obj(timer)->on_timer(timer);}
};
struct Frame : v3_plugin_frame_cpp {
    Frame* handle=this;Loop runLoop;
    Frame() {
        query_interface=[](void* s,const v3_tuid iid,void** out)->v3_result {
            auto* f=*static_cast<Frame**>(s);
            if(!std::memcmp(iid,v3_run_loop_iid,16)){*out=&f->runLoop.handle;return V3_OK;}
            *out=nullptr;return V3_NO_INTERFACE;
        };
        this->ref=unref=::testhost::ref;
        frame.resize_view=[](void*,v3_plugin_view** view,v3_view_rect* rect)->v3_result{return v3_cpp_obj(view)->on_size(view,rect);};
    }
};
}
