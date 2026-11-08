#include "CharacterLocomotion.h"
#include <iostream>
#include <stdexcept>
void Require(bool test,const char* text){if(!test)throw std::runtime_error(text);}
float Difference(const RigPose& a,const RigPose& b){
    float result=0;for(size_t n=0;n<a.world.size();++n)for(int c=0;c<4;++c)for(int r=0;r<4;++r)
        result=std::max(result,std::abs(a.world[n][c][r]-b.world[n][c][r]));return result;
}
int main(int argc,char** argv){try{
    Require(argc==2,"Model argument required");auto rig=AnimationRig::Load(argv[1]);
    LegIK legs(rig,"both");
    const float importedWidth=glm::length(legs.ContactTarget()-legs.Leg(1).ContactTarget());
    CharacterLocomotion control(legs);
    const auto initial=control.Pose();
    std::cout<<"Standing knee flex L="<<control.PostureDiagnostics().kneeFlex[0]<<" R="<<control.PostureDiagnostics().kneeFlex[1]<<" auto_height="<<control.PostureDiagnostics().automaticHeight<<std::endl;
    Require(control.PostureDiagnostics().kneeFlex[0]<10 && control.PostureDiagnostics().kneeFlex[1]<10,"Default standing knees are excessively bent");
    Require(control.PostureDiagnostics().contactError<.0003f && !control.PostureDiagnostics().clamped,"Straight standing lost foot contact");
    int head=-1;for(size_t n=0;n<rig.nodes.size();++n)if(rig.nodes[n].name=="Head")head=int(n);
    Require(head>=0,"Head calibration bone missing");
    Require(std::abs(glm::length(legs.ContactTarget()-legs.Leg(1).ContactTarget())-importedWidth*.5f)<1e-5f,"Default stance did not halve imported width");
    Require(std::abs(initial.world[size_t(head)][3].x-initial.world[legs.Pelvis()][3].x)<1e-5f,"Default upper body is still tilted forward");
    const auto supportCenter=(legs.HeelPosition()+legs.ToePosition()+legs.Leg(1).HeelPosition()+legs.Leg(1).ToePosition())*.25f;
    Require(std::abs(supportCenter.x-initial.world[legs.Pelvis()][3].x)<1e-5f,"Rest pelvis projection is not centered over shoes");
    const auto idleRevision=control.Current().poseRevision;
    for(int i=0;i<120;++i)control.Update(1.f/60,{});
    Require(Difference(initial,control.Pose())<1e-5f,"Idle pose drifted");
    Require(control.Current().poseRevision==idleRevision,"Stationary pose invalidates reflection history");
    PoseSettings rest;rest.values[PoseSettings::Width]=.8f;control.SetPoseSettings(rest);
    Require(std::abs(glm::length(legs.ContactTarget()-legs.Leg(1).ContactTarget())-importedWidth*.8f)<1e-5f,"Idle width edit did not apply immediately");
    auto settling=control.Snapshot();settling.blend=.3f;control.Restore(settling);
    rest.values[PoseSettings::Width]=.65f;control.SetPoseSettings(rest);
    Require(control.Current().active<0 && control.Current().blend==0 && std::abs(glm::length(legs.ContactTarget()-legs.Leg(1).ContactTarget())-importedWidth*.65f)<1e-5f,"Arm settling blocked idle stance preview");
    rest.values[PoseSettings::Width]=.8f;control.SetPoseSettings(rest);
    const auto planted0=legs.ContactPosition(),planted1=legs.Leg(1).ContactPosition();
    const auto neutralHip=control.Pose().world[legs.Pelvis()][3];
    rest.values[PoseSettings::HipForward]=-.04f;rest.values[PoseSettings::RestTilt]=-6;control.SetPoseSettings(rest);
    Require(legs.ContactPosition()==planted0 || glm::length(legs.ContactPosition()-planted0)<1e-5f,"Rest correction moved support foot");
    Require(glm::length(legs.Leg(1).ContactPosition()-planted1)<1e-5f,"Rest correction moved opposite foot");
    Require(control.Pose().world[legs.Pelvis()][3].x<neutralHip.x-.04f,"Hip fore/aft correction absent at idle");
    Require(control.Pose().world[size_t(head)][3].x<control.Pose().world[legs.Pelvis()][3].x-.05f,"Rest tilt has no effect without walking");
    control.SetPoseSettings(PoseSettings{});control.Reset();
    Require(Difference(initial,control.Pose())<1e-6f,"Corrected rest defaults did not restore");
    glm::vec3 previousContacts[2]{};bool support[2]={false,false};
    CharacterLocomotion::FootPhase previousPhases[2]={CharacterLocomotion::FootPhase::Flat,CharacterLocomotion::FootPhase::Flat};
    float maximumError=0,maximumPelvis=0;bool left=false,right=false,turned=false;
    for(int frame=0;frame<1200;++frame){
        glm::vec3 input=frame<240?glm::vec3(1,0,0):frame<480?glm::vec3(0,0,-1):frame<720?glm::vec3(-1,0,0):glm::vec3(0);
        control.Update(1.f/60,input);
        const auto state=control.Current();
        maximumError=std::max(maximumError,legs.Error());maximumPelvis=std::max(maximumPelvis,std::abs(legs.PelvisOffset().y));
        if(legs.Clamped() || legs.Error()>=.0003f){std::cerr<<"frame="<<frame<<" error="<<legs.Error()<<" active="<<state.active<<" yaw="<<state.yaw<<std::endl;}
        Require(!legs.Clamped() && legs.Error()<.0003f,"Controlled foot target unreachable");
        Require(legs.Stance() || legs.Leg(1).Stance(),"Both controlled feet airborne");
        for(size_t i=0;i<2;++i){
            const auto& leg=legs.Leg(i);const auto point=leg.ContactPosition();
            Require(FiniteIk(point) && point.y>=legs.GroundHeight()-.0003f,"Invalid controlled foot contact");
            if(leg.Stance() && support[i] && previousPhases[i]==control.PostureDiagnostics().phase[i])Require(glm::length(point-previousContacts[i])<.0003f,"Turning dragged support foot");
            support[i]=leg.Stance();previousContacts[i]=point;previousPhases[i]=control.PostureDiagnostics().phase[i];
            Require(control.PostureDiagnostics().clearance[i]>=-.0003f,"Rolling shoe penetrated the ground");
        }
        left|=state.active==0;right|=state.active==1;turned|=std::abs(state.yaw)>2.5f;
        if(frame==140){
            const auto snapshot=control.Snapshot();const auto expected=control.Pose();
            LegIK restoredLegs(rig,"both");CharacterLocomotion restored(restoredLegs);restored.Restore(snapshot);
            Require(Difference(expected,restored.Pose())<1e-6f,"Rebuild lost current foot or body pose");
            restored.Update(1.f/60,input);control.Update(1.f/60,input);
            Require(Difference(control.Pose(),restored.Pose())<1e-6f,"Restored controller diverged");
        }
    }
    Require(left && right && turned,"Missing alternating steps or reverse turn");
    Require(control.Current().poseRevision>idleRevision,"Moving pose did not invalidate reflection history");
    Require(control.Current().speed<1e-5f && control.Current().active<0 && control.Current().blend<.001f,"Release failed to settle to idle");
    Require(control.Current().distance>2,"Character did not travel");
    const auto stopped=control.Pose();for(int i=0;i<120;++i)control.Update(1.f/60,{});
    Require(Difference(stopped,control.Pose())<1e-5f,"Stopped pose keeps changing");
    control.Reset();Require(Difference(initial,control.Pose())==0,"Reset failed");
    const auto beforePause=control.Snapshot();control.Update(0,{1,0,0});Require(glm::length(control.Current().position-beforePause.position)==0,"Paused control moved");
    for(int i=0;i<90;++i)control.Update(1.f/60,{1,0,1});const float diagonal=control.Current().distance;
    control.Reset();for(int i=0;i<90;++i)control.Update(1.f/60,{1,0,0});
    Require(std::abs(diagonal-control.Current().distance)<.001f,"Diagonal input moves faster");
    bool arms=false,chest=false;
    for(size_t n=0;n<rig.nodes.size();++n){
        const float moved=glm::length(glm::vec3(control.Pose().world[n][3]-initial.world[n][3])-control.Current().position);
        if(rig.nodes[n].name=="LowerArm.L" || rig.nodes[n].name=="LowerArm.R")arms|=moved>.02f;
        if(rig.nodes[n].name=="Chest")chest|=moved>.005f;
    }
    Require(arms && chest,"Body/arm animation absent");
    control.Reset();for(int i=0;i<180;++i)control.Update(1.f/60,{1,0,0});
    const auto sixty=control.Snapshot();Require(sixty.distance>.9f,"Movement too slow");
    for(int rate:{30,120}){
        control.Reset();for(int i=0;i<3*rate;++i)control.Update(1.f/rate,{1,0,0});
        Require(glm::length(control.Current().position-sixty.position)<.002f,"Frame rate changed movement speed");
        Require(control.Current().steps>=sixty.steps-1 && control.Current().steps<=sixty.steps+1,"Frame rate changed foot stepping");
    }
    control.Reset();for(int i=0;i<50;++i)control.Update(1.f/60,{1,0,0});
    const auto frozen=control.Snapshot();const auto originalPose=control.Pose();
    PoseSettings tuned;tuned.values[PoseSettings::Drop]=.08f;tuned.values[PoseSettings::Support]=.03f;
    tuned.values[PoseSettings::Lean]=14;tuned.values[PoseSettings::Knee]=.9f;tuned.values[PoseSettings::Width]=.8f;
    tuned.values[PoseSettings::Arm]=30;tuned.values[PoseSettings::Elbow]=20;
    control.SetPoseSettings(tuned);
    Require(Difference(originalPose,control.Pose())>.02f,"Paused posture change has no effect");
    Require(control.Current().position==frozen.position && control.Current().speed==frozen.speed,"Posture editing advanced movement");
    for(size_t i=0;i<2;++i){
        Require(control.Current().feet[i].contact==frozen.feet[i].contact && control.Current().feet[i].goal==frozen.feet[i].goal && control.Current().feet[i].elapsed==frozen.feet[i].elapsed,"Posture editing moved a planted/swing target");
    }
    Require(control.PostureDiagnostics().contactError<.0003f && !control.PostureDiagnostics().clamped,"Posture change broke contact");
    LegIK customLegs(rig,"both");CharacterLocomotion customRestore(customLegs);customRestore.SetPoseSettings(tuned);customRestore.Restore(control.Snapshot());
    Require(Difference(control.Pose(),customRestore.Pose())<1e-6f,"Rebuild lost custom posture");
    for(int i=0;i<360;++i){
        control.Update(1.f/60,i<180?glm::vec3(1,0,0):glm::vec3(0,0,-1));
        Require(control.PostureDiagnostics().contactError<.0003f && !control.PostureDiagnostics().clamped,"Tuned walking posture lost contact");
    }
    const auto beforeInvalid=control.Pose();auto invalid=tuned;invalid.values[PoseSettings::Width]=100;
    bool invalidRejected=false;try{control.SetPoseSettings(invalid);}catch(...){invalidRejected=true;}
    Require(invalidRejected && Difference(beforeInvalid,control.Pose())==0,"Invalid setting changed live pose");
    const auto fittingPosition=control.Current().position;const float fittingYaw=control.Current().yaw;
    control.FitStandingPose();
    Require(control.Current().position==fittingPosition && control.Current().yaw==fittingYaw && control.Current().active<0 && control.Current().speed==0,"Stand preview lost root or retained walking");
    Require(control.PostureDiagnostics().phase[0]==CharacterLocomotion::FootPhase::Flat && control.PostureDiagnostics().phase[1]==CharacterLocomotion::FootPhase::Flat && control.PostureDiagnostics().contactError<.0003f,"Stand preview failed contact");
    // Isolate one step so all three roll phases and their boundaries are sampled.
    PoseSettings rolling;rolling.values[PoseSettings::Heel]=25;rolling.values[PoseSettings::Toe]=35;
    rolling.values[PoseSettings::RollTime]=.14f;control.SetPoseSettings(rolling);control.Reset();
    auto rollState=control.Snapshot();rollState.position={.1f,0,0};rollState.blend=1;rollState.active=0;
    rollState.feet[0].start=rollState.feet[0].contact;
    rollState.feet[0].goal=rollState.feet[0].start+glm::vec3(.2f,0,0);
    rollState.feet[0].goalYaw=.3f;
    bool toePhase=false,airPhase=false,heelPhase=false;
    for(int i=0;i<=660;++i){
        rollState.feet[0].elapsed=i*.001f;control.Restore(rollState);
        const auto& d=control.PostureDiagnostics();
        Require(d.contactError<.0003f && !d.clamped,"Rolling pivot target missed");
        Require(d.clearance[0]>=-.0003f && d.clearance[1]>=-.0003f,"Extreme roll angle penetrated ground");
        toePhase|=d.phase[0]==CharacterLocomotion::FootPhase::ToeOff;
        airPhase|=d.phase[0]==CharacterLocomotion::FootPhase::Swing;
        heelPhase|=d.phase[0]==CharacterLocomotion::FootPhase::HeelStrike;
        if(d.phase[0]==CharacterLocomotion::FootPhase::ToeOff){
            Require(d.pitch[0]<=.001f && std::abs(legs.ToePosition().y-legs.GroundHeight())<.0003f,"Toe support pivot lifted");
        }
        if(d.phase[0]==CharacterLocomotion::FootPhase::HeelStrike){
            Require(d.pitch[0]>=-.001f && std::abs(legs.HeelPosition().y-legs.GroundHeight())<.0003f,"Heel support pivot lifted");
        }
    }
    Require(toePhase && airPhase && heelPhase,"Missing foot roll phases");
    for(float boundary:{.14f,.52f,.66f}){
        rollState.feet[0].elapsed=boundary-.00001f;control.Restore(rollState);const auto before=control.Pose();
        rollState.feet[0].elapsed=boundary+.00001f;control.Restore(rollState);
        Require(Difference(before,control.Pose())<.0003f,"Foot roll phase transition jumped");
    }
    rollState.feet[0].elapsed=0;control.Restore(rollState);const auto toeStart=control.Pose();
    auto flatStart=rollState;flatStart.active=-1;control.Restore(flatStart);
    Require(Difference(toeStart,control.Pose())<.00001f,"Switching sole to toe pivot jumped");
    rolling.values[PoseSettings::RollTime]=0;control.SetPoseSettings(rolling);control.Restore(rollState);
    Require(control.PostureDiagnostics().pitch[0]==0,"Zero roll time did not disable pitching");
    std::cout<<"Controller: 1200 baseline + 360 tuned frames, support lock, turns, idle, 30/60/120 FPS, paused editing and custom rebuild OK; max_error="<<maximumError<<" max_pelvis="<<maximumPelvis<<std::endl;
    std::cout<<"Foot roll: 661 extreme-angle samples, toe/heel pivot lock, no rigid-shoe penetration and continuous phase switches OK"<<std::endl;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
