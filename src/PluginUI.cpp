// SPDX-License-Identifier: CC0-1.0
#include "DistrhoUI.hpp"
#include "Biquad.hpp"
#include "PortMapping.hpp"
#include <array>
#include <cstdio>

START_NAMESPACE_DISTRHO
class DynamicEQUI final : public UI {
    std::array<float,eq29::ParameterCount> p_=eq29::defaults();
    int selected_=0, dragging_=-1;
    // Keep graph/editor code in engine coordinates for both plugin formats.
    void editParameter(uint32_t id,bool started) { UI::editParameter(eq29::toHostParameter(id),started); }
    void setParameterValue(uint32_t id,float value) { UI::setParameterValue(eq29::toHostParameter(id),value); }
    static ImU32 color(unsigned b, unsigned alpha=255) {
        constexpr unsigned colors[8][3]={{77,205,255},{126,231,157},{255,196,97},{208,150,255},
                                       {255,130,156},{100,226,211},{222,226,110},{160,176,255}};
        return IM_COL32(colors[b][0],colors[b][1],colors[b][2],alpha);
    }
    float get(unsigned b,eq29::Field f) const { return p_[eq29::param(b,f)]; }
    void send(unsigned id,float value) {
        p_[id]=eq29::sanitize(id,value); setParameterValue(id,p_[id]);
    }
    void discrete(unsigned id,float value) {
        editParameter(id,true); send(id,value); editParameter(id,false);
    }
    void slider(unsigned id,const char* label,const char* format="%.2f",float maximum=0) {
        const auto s=eq29::spec(id);
        float value=p_[id];
        const bool changed=ImGui::SliderFloat(label,&value,s.min,maximum>0 ? maximum : s.max,format,
                                              s.logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
        if (ImGui::IsItemActivated()) editParameter(id,true);
        if (changed) send(id,value);
        if (ImGui::IsItemDeactivated()) editParameter(id,false);
    }
    void checkbox(unsigned id,const char* label) {
        bool v=p_[id]>.5f;
        if (ImGui::Checkbox(label,&v)) discrete(id,v ? 1.f : 0.f);
    }
    void add(float hz=1000,float gain=0) {
        for (unsigned b=0;b<eq29::MaxBands;++b) if (get(b,eq29::Enabled)<.5f) {
            // Reusing a slot preserves no hidden state: all input controls get reset.
            for (unsigned f=1;f<eq29::Fields;++f) discrete(eq29::param(b,eq29::Field(f)),eq29::spec(eq29::param(b,eq29::Field(f))).def);
            discrete(eq29::param(b,eq29::Type),eq29::Bell);
            discrete(eq29::param(b,eq29::Frequency),hz);
            discrete(eq29::param(b,eq29::Gain),gain);
            discrete(eq29::param(b,eq29::Enabled),1);
            selected_=int(b); return;
        }
    }
    void graph(float width,float height) {
        const ImVec2 origin=ImGui::GetCursorScreenPos();
        const ImVec2 lo(origin.x+44,origin.y+16),hi(origin.x+width-14,origin.y+height-26);
        const float w=hi.x-lo.x,h=hi.y-lo.y;
        const double rate=p_[eq29::SampleRate];
        const float maxHz=float(std::min(20000.0,rate*.45));
        const double logSpan=std::log(maxHz/20.0);
        auto xOf=[&](double hz){return lo.x+float(std::log(hz/20)/logSpan)*w;};
        auto yOf=[&](double db){return lo.y+float((30-db)/60)*h;};
        auto freqAt=[&](float x){return float(20*std::exp(std::clamp((x-lo.x)/w,0.f,1.f)*logSpan));};
        auto gainAt=[&](float y){return std::clamp(30-60*(y-lo.y)/h,-24.f,24.f);};
        ImGui::InvisibleButton("response",ImVec2(width,height),ImGuiButtonFlags_MouseButtonLeft);
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin,ImVec2(origin.x+width,origin.y+height),IM_COL32(16,23,33,255),10);
        for (int db=-24;db<=24;db+=12) {
            const float y=yOf(db); char text[16]; std::snprintf(text,sizeof(text),"%+d",db);
            draw->AddLine(ImVec2(lo.x,y),ImVec2(hi.x,y),IM_COL32(65,78,94,db==0?220:100));
            draw->AddText(ImVec2(origin.x+6,y-6),IM_COL32(150,166,185,255),text);
        }
        for (float hz : {20.f,50.f,100.f,200.f,500.f,1000.f,2000.f,5000.f,10000.f,20000.f}) {
            if (hz>maxHz) continue;
            const float x=xOf(hz); char text[16];
            if (hz>=1000) std::snprintf(text,sizeof(text),"%gk",hz/1000); else std::snprintf(text,sizeof(text),"%g",hz);
            draw->AddLine(ImVec2(x,lo.y),ImVec2(x,hi.y),IM_COL32(65,78,94,100));
            draw->AddText(ImVec2(x-8,hi.y+7),IM_COL32(150,166,185,255),text);
        }
        std::array<eq29::Coefficients,eq29::MaxBands> base,live,limit;
        for (unsigned b=0;b<eq29::MaxBands;++b) {
            const float gain=get(b,eq29::Gain),range=get(b,eq29::Dynamics)*get(b,eq29::Range)*(get(b,eq29::Mode)>.5f?1:-1);
            base[b]=eq29::equalizer(unsigned(get(b,eq29::Type)),get(b,eq29::Frequency),get(b,eq29::Q),gain,rate);
            live[b]=eq29::equalizer(unsigned(get(b,eq29::Type)),get(b,eq29::Frequency),get(b,eq29::Q),gain+p_[eq29::MeterBase+b],rate);
            limit[b]=eq29::equalizer(unsigned(get(b,eq29::Type)),get(b,eq29::Frequency),get(b,eq29::Q),gain+range,rate);
        }
        draw->PushClipRect(lo,hi,true);
        ImVec2 lastLive{},lastBase{},lastMin{},lastMax{};
        constexpr int points=320;
        for (int i=0;i<points;++i) {
            const float x=lo.x+w*i/(points-1),hz=freqAt(x);
            double a=p_[eq29::OutputGain],v=a,mn=a,mx=a;
            for (unsigned b=0;b<eq29::MaxBands;++b) if (get(b,eq29::Enabled)>.5f) {
                const double s=eq29::gainToDb(base[b].magnitude(hz,rate));
                const double l=eq29::gainToDb(limit[b].magnitude(hz,rate));
                a+=s; v+=eq29::gainToDb(live[b].magnitude(hz,rate));
                mn+=std::min(s,l); mx+=std::max(s,l);
            }
            if (p_[eq29::Bypass]>.5f) v=0;
            const ImVec2 current(x,yOf(v)),stat(x,yOf(a)),lower(x,yOf(mn)),upper(x,yOf(mx));
            if (i) {
                draw->AddQuadFilled(lastMin,lastMax,upper,lower,IM_COL32(91,181,240,45));
                draw->AddLine(lastBase,stat,IM_COL32(143,159,180,180),1);
                draw->AddLine(lastLive,current,IM_COL32(122,237,183,255),2.5f);
            }
            lastLive=current; lastBase=stat; lastMin=lower; lastMax=upper;
        }
        draw->PopClipRect();
        const auto& io=ImGui::GetIO();
        int hovered=-1;
        for (unsigned b=0;b<eq29::MaxBands;++b) if (get(b,eq29::Enabled)>.5f) {
            const ImVec2 pos(xOf(std::min(get(b,eq29::Frequency),maxHz)),yOf(get(b,eq29::Gain)));
            const float dx=pos.x-io.MousePos.x,dy=pos.y-io.MousePos.y;
            if (dx*dx+dy*dy<144) hovered=int(b);
            draw->AddCircleFilled(pos,selected_==int(b)?9.f:7.f,color(b));
            if (selected_==int(b)) draw->AddCircle(pos,12,color(b,130),0,2);
            char number[4]; std::snprintf(number,sizeof(number),"%u",b+1);
            draw->AddText(ImVec2(pos.x-3,pos.y-6),IM_COL32(12,20,26,255),number);
        }
        if (ImGui::IsItemHovered()) {
            if (hovered>=0 && ImGui::IsMouseClicked(0)) {
                selected_=dragging_=hovered;
                for (auto f:{eq29::Frequency,eq29::Gain,eq29::Q}) editParameter(eq29::param(unsigned(dragging_),f),true);
            } else if (hovered<0 && ImGui::IsMouseDoubleClicked(0)) add(freqAt(io.MousePos.x),gainAt(io.MousePos.y));
            if (hovered>=0 && io.MouseWheel!=0) {
                selected_=hovered;
                discrete(eq29::param(unsigned(hovered),eq29::Q),get(unsigned(hovered),eq29::Q)*std::pow(1.15f,io.MouseWheel));
            }
        }
        if (dragging_>=0) {
            const unsigned b=unsigned(dragging_);
            if (ImGui::IsMouseDown(0)) {
                if (io.KeyShift) send(eq29::param(b,eq29::Q),get(b,eq29::Q)*std::exp(-io.MouseDelta.y*.015f));
                else { send(eq29::param(b,eq29::Frequency),freqAt(io.MousePos.x)); send(eq29::param(b,eq29::Gain),gainAt(io.MousePos.y)); }
            } else {
                for (auto f:{eq29::Frequency,eq29::Gain,eq29::Q}) editParameter(eq29::param(b,f),false);
                dragging_=-1;
            }
        }
    }
public:
    DynamicEQUI() : UI() { setGeometryConstraints(820,620); }
protected:
    void parameterChanged(uint32_t id,float value) override {
        id=eq29::toEngineParameter(id);
        if (id<eq29::ParameterCount) p_[id]=eq29::sanitize(id,value);
    }
    void onImGuiDisplay() override {
        ImGui::StyleColorsDark();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(float(getWidth()),float(getHeight())));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(18,14));
        ImGui::Begin("My Dynamic EQ",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        unsigned count=0;
        for (unsigned b=0;b<eq29::MaxBands;++b) if (get(b,eq29::Enabled)>.5f) ++count;
        ImGui::Text("EQ29 / DYNAMIC EQ"); ImGui::SameLine();
        ImGui::TextDisabled("     %u / 8 bands   |   Stereo linked",count);
        ImGui::SameLine(); checkbox(eq29::Bypass,"Bypass");
        ImGui::SameLine(); ImGui::SetNextItemWidth(150); slider(eq29::OutputGain,"Output","%.1f dB");
        graph(ImGui::GetContentRegionAvail().x,std::max(230.f,float(getHeight())*.48f));
        ImGui::TextDisabled("Drag: frequency / gain   |   Shift-drag or wheel: Q   |   Double-click: add band");
        for (unsigned b=0;b<eq29::MaxBands;++b) if (get(b,eq29::Enabled)>.5f) {
            if (b) ImGui::SameLine();
            char text[20]; std::snprintf(text,sizeof(text),"Band %u",b+1);
            ImGui::PushStyleColor(ImGuiCol_Button,selected_==int(b)?color(b,160):color(b,55));
            if (ImGui::Button(text)) selected_=int(b);
            ImGui::PopStyleColor();
        }
        if (count) ImGui::SameLine();
        ImGui::BeginDisabled(count==eq29::MaxBands);
        if (ImGui::Button("+ Add")) add();
        ImGui::EndDisabled();
        if (selected_<0 || get(unsigned(selected_),eq29::Enabled)<.5f) {
            selected_=-1;
            for (unsigned b=0;b<eq29::MaxBands;++b) if (get(b,eq29::Enabled)>.5f) { selected_=int(b); break; }
        }
        ImGui::Separator();
        if (selected_>=0) {
            const unsigned b=unsigned(selected_);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color(b)),"BAND %u",b+1); ImGui::SameLine();
            if (ImGui::Button("Remove")) { discrete(eq29::param(b,eq29::Enabled),0); selected_=-1; }
            ImGui::SameLine(); checkbox(eq29::param(b,eq29::Dynamics),"Dynamics enabled");
            ImGui::SameLine(); ImGui::Text("Live change: %+.2f dB",p_[eq29::MeterBase+b]);
            if (ImGui::BeginTable("controls",3,ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                int type=int(get(b,eq29::Type)); ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##type",&type,"Bell\0Low Shelf\0High Shelf\0")) discrete(eq29::param(b,eq29::Type),float(type));
                slider(eq29::param(b,eq29::Frequency),"Frequency","%.0f Hz",float(std::min(20000.0,double(p_[eq29::SampleRate])*.45)));
                slider(eq29::param(b,eq29::Gain),"Gain","%.1f dB");
                slider(eq29::param(b,eq29::Q),"Q","%.2f");
                ImGui::TableNextColumn();
                int mode=int(get(b,eq29::Mode)); ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##mode",&mode,"Compression\0Upward expansion\0")) discrete(eq29::param(b,eq29::Mode),float(mode));
                slider(eq29::param(b,eq29::Threshold),"Threshold","%.1f dB");
                slider(eq29::param(b,eq29::Ratio),"Ratio","%.2f : 1");
                slider(eq29::param(b,eq29::Range),"Range","%.1f dB");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("BAND-LOCAL PEAK DETECTOR");
                slider(eq29::param(b,eq29::Attack),"Attack","%.1f ms");
                slider(eq29::param(b,eq29::Release),"Release","%.0f ms");
                ImGui::TextWrapped("Above threshold: compress cuts this band; expand boosts it. Range caps the change.");
                ImGui::EndTable();
            }
        } else ImGui::TextUnformatted("No active bands. Click + Add or double-click the graph.");
        ImGui::TextDisabled("Green: live response   |   Gray: static EQ   |   Shading: available dynamic range");
        ImGui::End(); ImGui::PopStyleVar();
    }
};
UI* createUI() { return new DynamicEQUI; }
END_NAMESPACE_DISTRHO
