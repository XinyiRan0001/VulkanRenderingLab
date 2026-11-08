#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <stdexcept>
#include <string>

struct PoseSettings {
    enum Field { Drop,Support,Lean,Knee,Width,Arm,Elbow,Heel,Toe,RollTime,HipForward,RestTilt,Count };
    struct Slider {const char* key;const char* label;float minimum,maximum,initial;bool degrees;bool seconds=false;};
    inline static constexpr std::array<Slider,Count> sliders={{
        {"pelvis_drop","PELVIS DROP",0,.10f,0,false},
        {"support_shift","SUPPORT SHIFT",0,.05f,.018f,false},
        {"body_lean","BODY LEAN",-8,18,0,true},
        {"knee_forward","KNEE FORWARD",0,1,.9f,false},
        {"stance_width","STANCE WIDTH",.3f,1.15f,.5f,false},
        {"arm_swing","ARM SWING",0,40,17.188734f,true},
        {"elbow_bend","ELBOW BEND",0,35,6.875494f,true},
        {"heel_angle","HEEL STRIKE",0,25,12,true},
        {"toe_angle","TOE OFF",0,35,18,true},
        {"roll_time","ROLL TIME",0,.14f,.06f,false,true},
        {"hip_fore_aft","HIP FORE AFT",-.12f,.12f,0,false},
        {"rest_tilt","REST TILT",-15,15,0,true}
    }};
    std::array<float,Count> values{};
    bool autoReach=true;
    PoseSettings(){for(size_t i=0;i<Count;++i)values[i]=sliders[i].initial;}
    void Validate() const{
        for(size_t i=0;i<Count;++i)
            if(!std::isfinite(values[i]) || values[i]<sliders[i].minimum || values[i]>sliders[i].maximum)
                throw std::runtime_error(std::string("Invalid posture parameter: ")+sliders[i].key);
    }
    void Save(const std::string& path) const{
        Validate();std::ofstream file(path);file.imbue(std::locale::classic());
        if(!file)throw std::runtime_error("Cannot write posture config");
        file<<"POSTURE_CONFIG 3\n"<<std::setprecision(9);
        for(size_t i=0;i<Count;++i)file<<sliders[i].key<<" "<<values[i]<<"\n";
        file<<"auto_reach "<<autoReach<<"\n";
        if(!file)throw std::runtime_error("Cannot finish posture config");
    }
    static PoseSettings Load(const std::string& path){
        std::ifstream file(path);file.imbue(std::locale::classic());std::string key;int version=0;
        if(!(file>>key>>version) || key!="POSTURE_CONFIG" || version<1 || version>3)throw std::runtime_error("Invalid posture config version");
        const size_t required=version==1?Heel:version==2?HipForward:Count;
        PoseSettings result;std::array<bool,Count+1> seen{};
        while(file>>key){
            size_t field=Count;
            for(size_t i=0;i<Count;++i)if(key==sliders[i].key){field=i;break;}
            if(field==Count && key!="auto_reach")throw std::runtime_error("Unknown posture config field");
            if(field>=required && field<Count)throw std::runtime_error("Unexpected posture config field for version");
            if(seen[field])throw std::runtime_error("Duplicate posture config field");
            seen[field]=true;
            if(field==Count){int flag=-1;if(!(file>>flag) || (flag!=0 && flag!=1))throw std::runtime_error("Invalid reach flag");result.autoReach=flag!=0;}
            else if(!(file>>result.values[field]))throw std::runtime_error("Invalid posture config value");
        }
        for(size_t i=0;i<seen.size();++i)if(!seen[i] && (i<required || i==Count))throw std::runtime_error("Incomplete posture config");
        result.Validate();return result;
    }
};

struct PoseControls {
    enum Action {None,Pause,Defaults,Compare,Save,Load,Reach,Stand};
    static constexpr float Width=324,RowTop=82,RowHeight=36;
    static constexpr float ActionTop=RowTop+PoseSettings::Count*RowHeight+6,Height=ActionTop+210;
    PoseSettings settings;
    bool visible=true,custom=true;
    int dragging=-1;
    std::string status="WIDTH APPLIES NEXT STEP",configPath="build/character-pose.cfg";
    static float Scale(int w,int h){return std::max(.1f,std::min({1.f,(w-24)/Width,(h-24)/Height}));}
    bool Contains(float x,float y) const{return visible && x>=0 && x<=Width && y>=0 && y<=Height;}
    PoseSettings Effective() const{return custom?settings:PoseSettings{};}
    bool Drag(float x){
        if(!visible || dragging<0)return false;
        const auto& slider=PoseSettings::sliders[size_t(dragging)];
        settings.values[size_t(dragging)]=slider.minimum+(slider.maximum-slider.minimum)*std::clamp((x-12)/300.f,0.f,1.f);
        return true;
    }
    Action Press(float x,float y){
        if(!Contains(x,y))return None;
        if(y>=42 && y<=68){
            if(x>=12 && x<=108)return Pause;
            if(x>=116 && x<=212)return Defaults;
            if(x>=220 && x<=312)return Compare;
        }
        if(y>=ActionTop && y<=ActionTop+26){
            if(x>=12 && x<=108)return Save;
            if(x>=116 && x<=212)return Load;
            if(x>=220 && x<=312)return Reach;
        }
        if(y>=ActionTop+32 && y<=ActionTop+58 && x>=12 && x<=312)return Stand;
        for(int i=0;i<PoseSettings::Count;++i){
            const float bar=RowTop+i*RowHeight+20;
            if(y>=bar-4 && y<=bar+12 && x>=8 && x<=316){dragging=i;Drag(x);break;}
        }
        return None;
    }
};
