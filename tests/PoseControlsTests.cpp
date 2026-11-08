#include "PoseControls.h"
#include <iostream>
#include <limits>
void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main(int argc,char** argv){try{
    Require(argc==2,"Config scratch path required");PoseControls ui;
    for(int row=0;row<PoseSettings::Count;++row){
        const float y=PoseControls::RowTop+row*PoseControls::RowHeight+24;
        ui.Press(162,y);Require(ui.dragging==row,"Wrong posture slider captured");
        ui.Drag(-100);Require(ui.settings.values[row]==PoseSettings::sliders[row].minimum,"Slider minimum not clamped");
        ui.Drag(1000);Require(std::abs(ui.settings.values[row]-PoseSettings::sliders[row].maximum)<1e-5f,"Slider maximum not clamped");
        ui.dragging=-1;
    }
    Require(ui.Press(30,54)==PoseControls::Pause,"Pause hit test");
    Require(ui.Press(140,54)==PoseControls::Defaults,"Defaults hit test");
    Require(ui.Press(250,54)==PoseControls::Compare,"Comparison hit test");
    Require(ui.Press(30,PoseControls::ActionTop+12)==PoseControls::Save && ui.Press(140,PoseControls::ActionTop+12)==PoseControls::Load && ui.Press(250,PoseControls::ActionTop+12)==PoseControls::Reach,"Config/reach hit test");
    Require(ui.Press(160,PoseControls::ActionTop+44)==PoseControls::Stand,"Stand preview hit test");
    ui.visible=false;Require(!ui.Contains(20,20) && ui.Press(30,54)==PoseControls::None && !ui.Drag(80),"Hidden panel captures input");
    ui.custom=false;Require(ui.Effective().values==PoseSettings{}.values,"Baseline modified stored defaults");
    ui.settings.autoReach=false;ui.settings.Save(argv[1]);const auto loaded=PoseSettings::Load(argv[1]);
    Require(loaded.values==ui.settings.values && !loaded.autoReach,"Config roundtrip changed values");
    const auto reject=[&](const char* text){std::ofstream file(argv[1]);file<<text;file.close();bool failed=false;try{PoseSettings::Load(argv[1]);}catch(...){failed=true;}Require(failed,"Invalid config accepted");};
    reject("POSTURE_CONFIG 2\n");reject("POSTURE_CONFIG 1\npelvis_drop 0.04\npelvis_drop 0.05\n");
    reject("POSTURE_CONFIG 1\nunknown 1\n");reject("POSTURE_CONFIG 1\npelvis_drop nan\n");
    {std::ofstream legacy(argv[1]);legacy<<"POSTURE_CONFIG 1\n";
     for(int i=0;i<PoseSettings::Heel;++i)legacy<<PoseSettings::sliders[i].key<<" "<<PoseSettings::sliders[i].initial<<"\n";
     legacy<<"auto_reach 1\n";}
    const auto legacy=PoseSettings::Load(argv[1]);
    Require(legacy.values[PoseSettings::Heel]==12 && legacy.values[PoseSettings::Toe]==18 && legacy.values[PoseSettings::RollTime]==.06f,"Legacy config lost new rolling defaults");
    {std::ofstream v2(argv[1]);v2<<"POSTURE_CONFIG 2\n";
     for(int i=0;i<PoseSettings::HipForward;++i)v2<<PoseSettings::sliders[i].key<<" "<<(i==PoseSettings::Width?1.f:PoseSettings::sliders[i].initial)<<"\n";
     v2<<"auto_reach 1\n";}
    const auto version2=PoseSettings::Load(argv[1]);
    Require(version2.values[PoseSettings::Width]==1 && version2.values[PoseSettings::HipForward]==0 && version2.values[PoseSettings::RestTilt]==0,"Version 2 changed explicit width or lost rest defaults");
    PoseSettings invalid;invalid.values[PoseSettings::Drop]=std::numeric_limits<float>::infinity();
    bool failed=false;try{invalid.Validate();}catch(...){failed=true;}Require(failed,"Nonfinite posture accepted");
    for(auto size:{std::pair<int,int>{960,640},{481,321},{801,451}}){
        const float scale=PoseControls::Scale(size.first,size.second);
        Require(PoseControls::Width*scale+24<=size.first+.001f && PoseControls::Height*scale+24<=size.second+.001f,"Posture panel exceeds viewport");
    }
    std::cout<<"Posture controls: slider capture/ranges, hidden input, buttons, baseline, scaled layout and strict config roundtrip OK"<<std::endl;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}
