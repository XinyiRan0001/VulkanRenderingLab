#include "CharacterLocomotion.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <limits>

namespace {
constexpr float Pi=3.14159265359f;
float Angle(float a){return std::remainder(a,2.f*Pi);}
float Smooth(float s){return s*s*s*(s*(s*6.f-15.f)+10.f);}
glm::mat4 Heading(float yaw){return glm::rotate(glm::mat4(1),yaw,glm::vec3(0,1,0));}
}
CharacterLocomotion::CharacterLocomotion(LegIK& legs):m_Legs(legs){
    if(legs.LegCount()!=2)throw std::runtime_error("Character control requires two leg chains");
    m_BaseForward=glm::normalize(legs.m_Forward+legs.m_Other->m_Forward);
    const auto hip=glm::vec3(legs.m_Base.world[legs.m_Pelvis][3]);
    const auto support=(legs.m_HeelBase+legs.m_ToeBase+legs.m_Other->m_HeelBase+legs.m_Other->m_ToeBase)*.25f;
    m_BaseForeAft=glm::dot(support-hip,m_BaseForward);
    int top=legs.m_Chest;
    for(const auto& skin:legs.m_Rig.skins)for(int joint:skin.joints)
        if(legs.m_Rig.nodes[joint].name=="Head")top=joint;
    m_ReferenceTop=top;
    const auto axis=glm::vec3(legs.m_Base.world[top][3])-hip;
    if(axis.y>1e-5f)m_BaseTilt=glm::clamp(-std::atan2(glm::dot(axis,m_BaseForward),axis.y),-glm::radians(20.f),glm::radians(20.f));
    std::cout<<"[Rest Calibration] fore_aft="<<m_BaseForeAft<<" tilt_deg="<<glm::degrees(m_BaseTilt)
        <<" source_width="<<glm::length(legs.m_BaseContact-legs.m_Other->m_BaseContact)<<std::endl;
    Reset();
}
void CharacterLocomotion::SetPoseSettings(const PoseSettings& settings){
    settings.Validate();const bool widthChanged=settings.values[PoseSettings::Width]!=m_Settings.values[PoseSettings::Width];
    m_Settings=settings;
    if(widthChanged && m_State.active<0 && m_State.speed<.001f)FitStandingPose();
    else Evaluate(true);
}
void CharacterLocomotion::FitStandingPose(){
    m_State.speed=0;m_State.blend=0;m_State.active=-1;m_State.next=0;
    for(size_t i=0;i<2;++i){
        auto& foot=m_State.feet[i];foot=Foot{};foot.contact=Ideal(i,m_State.position,m_State.yaw);
        foot.start=foot.goal=foot.contact;foot.yaw=foot.startYaw=foot.goalYaw=m_State.yaw;
    }
    Evaluate(true);
}
glm::mat4 CharacterLocomotion::Frame(glm::vec3 position,float yaw) const{
    auto pivot=glm::vec3(m_Legs.m_Base.world[m_Legs.m_Pelvis][3]);pivot.y=0;
    pivot+=m_BaseForward*m_BaseForeAft;
    return glm::translate(glm::mat4(1),position+pivot)*Heading(yaw)*glm::translate(glm::mat4(1),-pivot);
}
glm::vec3 CharacterLocomotion::Ideal(size_t side,glm::vec3 position,float yaw) const{
    const auto& leg=m_Legs.Leg(side);
    const auto center=(m_Legs.m_BaseContact+m_Legs.m_Other->m_BaseContact)*.5f;
    auto lateral=m_Legs.m_Other->m_BaseContact-m_Legs.m_BaseContact;lateral.y=0;lateral=glm::normalize(lateral);
    const auto contact=leg.m_BaseContact+lateral*((m_Settings.values[PoseSettings::Width]-1.f)*glm::dot(leg.m_BaseContact-center,lateral));
    auto result=glm::vec3(Frame(position,yaw)*glm::vec4(contact,1));
    result.y=m_Legs.GroundHeight();return result;
}
void CharacterLocomotion::Reset(){
    const auto revision=m_State.poseRevision;
    m_State=State{};m_State.poseRevision=(revision+1)&0xffffffu;m_ArmSwing=0;
    for(size_t i=0;i<2;++i)m_State.feet[i].contact=Ideal(i,{},0);
    Evaluate(true);
}
void CharacterLocomotion::Restore(const State& state){
    if(!FiniteIk(state.position) || !std::isfinite(state.yaw) || !std::isfinite(state.speed) ||
       !std::isfinite(state.blend) || !std::isfinite(state.distance) || state.active< -1 || state.active>1 || state.next<0 || state.next>1)
        throw std::runtime_error("Invalid locomotion snapshot");
    for(const auto& foot:state.feet)
        if(!FiniteIk(foot.contact)||!FiniteIk(foot.start)||!FiniteIk(foot.goal)||!std::isfinite(foot.yaw)||
           !std::isfinite(foot.startYaw)||!std::isfinite(foot.goalYaw)||!std::isfinite(foot.elapsed))
            throw std::runtime_error("Invalid locomotion foot snapshot");
    m_State=state;Evaluate(true);
}
void CharacterLocomotion::Update(float seconds,glm::vec3 direction,bool ik){
    if(!std::isfinite(seconds) || seconds<0 || !FiniteIk(direction))throw std::runtime_error("Invalid locomotion input");
    direction.y=0;
    if(glm::length(direction)>1.f)direction=glm::normalize(direction);
    // Bound stalls and integrate at a fixed maximum step; key repeat is irrelevant.
    const float total=std::min(seconds,.1f);
    if(total>1e-6f){
        const int substeps=int(std::ceil(total*120.f));
        for(int i=0;i<substeps;++i)Tick(total/substeps,direction);
    }
    Evaluate(ik);
}
void CharacterLocomotion::Tick(float dt,glm::vec3 direction){
    const float input=glm::length(direction),oldYaw=m_State.yaw;
    if(input>.001f){
        const float desired=std::atan2(-direction.z,direction.x);
        m_State.yaw=Angle(m_State.yaw+glm::clamp(Angle(desired-m_State.yaw),-1.2f*dt,1.2f*dt));
    }
    const float target=MaxSpeed*std::min(input,1.f);
    m_State.speed+=glm::clamp(target-m_State.speed,-.9f*dt,.7f*dt);
    const auto forward=glm::vec3(std::cos(m_State.yaw),0,-std::sin(m_State.yaw));
    const auto previous=m_State.position;
    m_State.position+=forward*(m_State.speed*dt);
    const auto attempted=m_State.position;
    m_State.position.x=glm::clamp(m_State.position.x,-2.f,2.f);
    m_State.position.z=glm::clamp(m_State.position.z,-2.f,2.f);
    const float distance=glm::length(m_State.position-previous);
    m_State.distance+=distance;
    if(m_State.position!=attempted && distance<dt*.001f && m_State.speed>.01f)m_State.speed=0;
    if(m_State.active>=0){
        auto& foot=m_State.feet[size_t(m_State.active)];
        foot.elapsed=std::min(foot.elapsed+dt,TotalStepTime());
        const float s=glm::clamp((foot.elapsed-m_Settings.values[PoseSettings::RollTime])/StepDuration,0.f,1.f);
        foot.contact=glm::mix(foot.start,foot.goal,Smooth(s));
        foot.contact.y=m_Legs.GroundHeight()+m_Legs.m_Length*.075f*16.f*s*s*(1-s)*(1-s);
        foot.yaw=Angle(foot.startYaw+Angle(foot.goalYaw-foot.startYaw)*Smooth(s));
        if(foot.elapsed>=TotalStepTime()){
            foot.contact=foot.goal;foot.yaw=foot.goalYaw;
            m_State.next=1-m_State.active;m_State.active=-1;++m_State.steps;
        }
    }
    if(m_State.active<0){
        for(int choice=0;choice<2;++choice){
            const int side=choice==0?m_State.next:1-m_State.next;
            auto& foot=m_State.feet[size_t(side)];
            const auto ideal=Ideal(size_t(side),m_State.position,m_State.yaw);
            if(glm::length(ideal-foot.contact)<.035f && std::abs(Angle(m_State.yaw-foot.yaw))<.10f)continue;
            foot.start=foot.contact;foot.startYaw=foot.yaw;foot.elapsed=0;
            const float angular=Angle(m_State.yaw-oldYaw)/dt;
            foot.goalYaw=Angle(m_State.yaw+angular*.18f);
            foot.goal=Ideal(size_t(side),m_State.position+forward*(m_State.speed*.28f),foot.goalYaw);
            m_State.active=side;break;
        }
    }
    const float activity=glm::clamp(m_State.speed/MaxSpeed+(m_State.active>=0?.2f:0.f),0.f,1.f);
    m_State.blend+=(activity-m_State.blend)*(1.f-std::exp(-dt*8.f));
}
CharacterLocomotion::FootPose CharacterLocomotion::SampleFoot(size_t side) const{
    const auto& foot=m_State.feet[side];const auto& leg=m_Legs.Leg(side);
    FootPose result;result.pivot=leg.m_BaseContact;result.contact=foot.contact;result.rotation=Heading(foot.yaw);
    if(m_State.active!=int(side))return result;
    const float duration=m_Settings.values[PoseSettings::RollTime];
    const float toe=duration>0?glm::radians(m_Settings.values[PoseSettings::Toe]):0;
    const float heel=duration>0?glm::radians(m_Settings.values[PoseSettings::Heel]):0;
    const auto axis=glm::normalize(glm::cross(leg.m_Forward,glm::vec3(0,1,0)));
    const auto rotation=[&](float yaw,float pitch){return Heading(yaw)*glm::rotate(glm::mat4(1),pitch,axis);};
    if(duration>0 && foot.elapsed<duration){
        result.phase=FootPhase::ToeOff;result.pivot=leg.m_ToeBase;
        result.pitch=-toe*Smooth(glm::clamp(foot.elapsed/duration,0.f,1.f));
        result.rotation=rotation(foot.startYaw,result.pitch);
        result.contact=foot.start+glm::mat3(Heading(foot.startYaw))*(leg.m_ToeBase-leg.m_BaseContact);
    }else if(foot.elapsed<duration+StepDuration){
        result.phase=FootPhase::Swing;
        const float s=glm::clamp((foot.elapsed-duration)/StepDuration,0.f,1.f),weight=Smooth(s);
        const auto startRotation=rotation(foot.startYaw,-toe),endRotation=rotation(foot.goalYaw,heel);
        const auto start=foot.start+glm::mat3(Heading(foot.startYaw))*(leg.m_ToeBase-leg.m_BaseContact)+glm::mat3(startRotation)*(leg.m_BaseContact-leg.m_ToeBase);
        const auto end=foot.goal+glm::mat3(Heading(foot.goalYaw))*(leg.m_HeelBase-leg.m_BaseContact)+glm::mat3(endRotation)*(leg.m_BaseContact-leg.m_HeelBase);
        result.contact=glm::mix(start,end,weight)+glm::vec3(0,leg.m_Length*.075f*16.f*s*s*(1-s)*(1-s),0);
        result.pitch=glm::mix(-toe,heel,weight);
        result.rotation=rotation(Angle(foot.startYaw+Angle(foot.goalYaw-foot.startYaw)*weight),result.pitch);
    }else if(duration>0){
        result.phase=FootPhase::HeelStrike;result.pivot=leg.m_HeelBase;
        const float s=glm::clamp((foot.elapsed-duration-StepDuration)/duration,0.f,1.f);
        result.pitch=heel*(1-Smooth(s));result.rotation=rotation(foot.goalYaw,result.pitch);
        result.contact=foot.goal+glm::mat3(Heading(foot.goalYaw))*(leg.m_HeelBase-leg.m_BaseContact);
    }else{
        result.contact=foot.goal;result.rotation=Heading(foot.goalYaw);
    }
    return result;
}
void CharacterLocomotion::Evaluate(bool ik){
    const auto previousWorld=m_Legs.m_Last.world;
    auto pose=m_Legs.m_Base;
    const auto frame=Frame(m_State.position,m_State.yaw),heading=Heading(m_State.yaw);
    for(auto& world:pose.world)world=frame*world;
    float lift=0;m_ArmSwing=0;
    if(m_State.active>=0){
        const float s=glm::clamp(m_State.feet[size_t(m_State.active)].elapsed/TotalStepTime(),0.f,1.f);
        lift=16.f*s*s*(1-s)*(1-s);
        m_ArmSwing=std::sin(Pi*s)*(m_State.active==0?1.f:-1.f);
    }
    const auto lateral=glm::normalize(glm::mat3(heading)*(m_Legs.m_Other->m_BaseContact-m_Legs.m_BaseContact));
    m_Diagnostics=Diagnostics{};
    auto pelvis=lateral*(m_Legs.m_Length*m_Settings.values[PoseSettings::Support]*lift*(m_State.active==0?1.f:-1.f));
    pelvis+=glm::mat3(heading)*m_BaseForward*(m_BaseForeAft+m_Legs.m_Length*m_Settings.values[PoseSettings::HipForward]);
    pelvis.y=-m_Legs.m_Length*(m_Settings.values[PoseSettings::Drop]+.007f*lift);
    const float requestedY=pelvis.y;
    const std::array<FootPose,2> feet={SampleFoot(0),SampleFoot(1)};
    float allowedY=std::numeric_limits<float>::max();
    // Lower hips when needed to keep both fixed support anchors reachable.
    for(size_t i=0;i<2 && m_Settings.autoReach;++i){
        const auto& leg=m_Legs.Leg(i);
        const auto ankle=feet[i].contact-glm::mat3(feet[i].rotation)*(feet[i].pivot-glm::vec3(leg.m_Base.world[leg.m_Foot][3]));
        const auto hip=glm::vec3(pose.world[leg.m_Hip][3])+glm::vec3(pelvis.x,0,pelvis.z);
        const auto delta=ankle-hip;
        // Near-straight at rest; leave slightly more room during locomotion.
        // Avoid exact full extension, where the knee bend plane is singular.
        const float radius=leg.m_Length*glm::mix(.998f,.992f,glm::clamp(m_State.blend,0.f,1.f));
        const float height=std::sqrt(std::max(.01f,radius*radius-delta.x*delta.x-delta.z*delta.z));
        allowedY=std::min(allowedY,ankle.y+height-hip.y);
    }
    if(m_Settings.autoReach){
        // Narrowing an imported bent-leg pose can require raising, not only
        // lowering, the hips. Blend toward a neutral near-straight idle height.
        const float idleWeight=1.f-glm::clamp(m_State.blend,0.f,1.f);
        pelvis.y=std::min(requestedY+allowedY*idleWeight,allowedY);
    }
    pelvis.y=std::max(pelvis.y,-m_Legs.m_Length*.18f);
    m_Diagnostics.automaticDrop=ik?std::max(0.f,requestedY-pelvis.y):0.f;
    m_Diagnostics.automaticHeight=ik?pelvis.y-requestedY:0.f;
    m_Legs.m_RootOffset=m_State.position;m_Legs.m_PelvisOffset=ik?pelvis:glm::vec3(0);
    if(ik){
        m_Legs.TransformSubtree(pose,m_Legs.m_Pelvis,glm::translate(glm::mat4(1),pelvis));
        const glm::vec3 pivot(pose.world[m_Legs.m_Pelvis][3]);
        const auto sagittal=glm::normalize(glm::cross(glm::vec3(0,1,0),glm::mat3(heading)*m_BaseForward));
        const auto rotation=glm::rotate(glm::mat4(1),m_BaseTilt+glm::radians(m_Settings.values[PoseSettings::RestTilt]),sagittal);
        m_Legs.TransformSubtree(pose,m_Legs.m_Spine,glm::translate(glm::mat4(1),pivot)*rotation*glm::translate(glm::mat4(1),-pivot));
        m_Legs.AnimateUpperBody(pose,heading,m_ArmSwing,m_State.blend,
            glm::radians(m_Settings.values[PoseSettings::Lean]),glm::radians(m_Settings.values[PoseSettings::Arm]),glm::radians(m_Settings.values[PoseSettings::Elbow]));
    }
    for(size_t i=0;i<2;++i){
        auto& leg=i==0?m_Legs:*m_Legs.m_Other;
        leg.SolveContact(pose,feet[i].contact,m_Settings.values[PoseSettings::Knee],feet[i].phase!=FootPhase::Swing,ik,heading,feet[i].rotation,&feet[i].pivot);
        m_Diagnostics.reach[i]=glm::length(leg.m_Target-glm::vec3(pose.world[leg.m_Hip][3]))/leg.m_Length;
        m_Diagnostics.phase[i]=feet[i].phase;m_Diagnostics.pitch[i]=glm::degrees(feet[i].pitch);
        const glm::vec3 hipPoint(pose.world[leg.m_Hip][3]),kneePoint(pose.world[leg.m_Knee][3]),anklePoint(pose.world[leg.m_Foot][3]);
        const float cosine=glm::clamp(glm::dot(glm::normalize(hipPoint-kneePoint),glm::normalize(anklePoint-kneePoint)),-1.f,1.f);
        m_Diagnostics.kneeFlex[i]=180.f-glm::degrees(std::acos(cosine));
    }
    m_Legs.m_Rig.RefreshPalettes(pose);
    for(size_t i=0;i<pose.world.size();++i)if(pose.world[i]!=previousWorld[i]){
        m_State.poseRevision=(m_State.poseRevision+1)&0xffffffu;break;
    }
    m_Legs.m_Last=pose;m_Legs.m_Other->m_Last=pose;
    const auto forward=glm::mat3(heading)*m_BaseForward;
    const auto center=(m_Legs.HeelPosition()+m_Legs.ToePosition()+m_Legs.m_Other->HeelPosition()+m_Legs.m_Other->ToePosition())*.25f;
    const glm::vec3 hip(pose.world[m_Legs.m_Pelvis][3]);
    const auto axis=glm::vec3(pose.world[m_ReferenceTop][3])-hip;
    m_Diagnostics.bodyForeAft=glm::dot(hip-center,forward);
    m_Diagnostics.bodyTilt=glm::degrees(std::atan2(glm::dot(axis,forward),axis.y));
    m_Diagnostics.stanceWidth=std::abs(glm::dot(m_Legs.ContactPosition()-m_Legs.m_Other->ContactPosition(),lateral));
    for(size_t i=0;i<2;++i)m_Diagnostics.clearance[i]=m_Legs.Leg(i).LowestFootHeight()-m_Legs.GroundHeight();
    m_Diagnostics.contactError=std::max(glm::length(m_Legs.ContactPosition()-m_Legs.ContactTarget()),glm::length(m_Legs.m_Other->ContactPosition()-m_Legs.m_Other->ContactTarget()));
    m_Diagnostics.clamped=m_Legs.Clamped();
}
