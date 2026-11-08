#pragma once
#include "AnimationRig.h"
#include "TwoBoneIK.h"
#include <string>
#include <memory>

class LegIK {
    friend class CharacterLocomotion;
public:
    LegIK(const AnimationRig&,const std::string& side);
    RigPose Sample(float seconds,bool enabled=true);
    int Hip() const{return m_Hip;}
    int Knee() const{return m_Knee;}
    int Foot() const{return m_Foot;}
    const std::string& Side() const{return m_Side;}
    glm::vec3 Target() const{return m_Target;}
    glm::vec3 Pole() const{return m_Pole;}
    const RigPose& LastPose() const{return m_Last;}
    float Error() const{return m_Other?glm::max(m_Error,m_Other->m_Error):m_Error;}
    bool Clamped() const{return m_Clamped || (m_Other && m_Other->m_Clamped);}
    bool Stance() const{return m_Stance;}
    glm::vec3 ContactTarget() const{return m_ContactTarget;}
    glm::vec3 ContactPosition() const;
    float GroundHeight() const{return m_GroundHeight;}
    glm::vec3 HeelPosition() const{return glm::vec3(m_Last.world[m_Foot]*glm::vec4(m_HeelLocal,1));}
    glm::vec3 ToePosition() const{return glm::vec3(m_Last.world[m_Foot]*glm::vec4(m_ToeLocal,1));}
    float LowestFootHeight() const;
    size_t LegCount() const{return m_Other?2:1;}
    const LegIK& Leg(size_t index) const;
    int Pelvis() const{return m_Pelvis;}
    glm::vec3 PelvisOffset() const{return m_PelvisOffset;}
    bool Walking() const{return m_Walking;}
    glm::vec3 RootOffset() const{return m_RootOffset;}
    float TravelDistance() const{return m_Walking?m_Length*.68f:0.f;}
    static constexpr float WalkDuration=8.f;
private:
    const AnimationRig& m_Rig;
    int m_Hip=-1,m_Knee=-1,m_Foot=-1;
    std::string m_Side;
    RigPose m_Base,m_Last;
    glm::vec3 m_Target{},m_Pole{},m_Forward{},m_RestBend{};
    glm::vec3 m_SoleLocal{},m_BaseContact{},m_ContactTarget{};
    glm::vec3 m_ContactLocal{},m_HeelBase{},m_ToeBase{},m_HeelLocal{},m_ToeLocal{};
    std::vector<glm::vec3> m_RigidFootLocal;
    float m_GroundHeight=0;
    bool m_Stance=true;
    float m_Length=0,m_Error=0;
    bool m_Clamped=false;
    std::unique_ptr<LegIK> m_Other;
    int m_Pelvis=-1;
    glm::vec3 m_PelvisOffset{};
    bool m_Walking=false;
    glm::vec3 m_RootOffset{};
    int m_Spine=-1,m_Chest=-1;
    int m_UpperArms[2]={-1,-1},m_LowerArms[2]={-1,-1};
    void TransformSubtree(RigPose&,int node,const glm::mat4&) const;
    void SolveContact(RigPose&,glm::vec3 contact,float bend,bool stance,bool enabled,
                      const glm::mat4& bodyHeading=glm::mat4(1),const glm::mat4& footHeading=glm::mat4(1),const glm::vec3* basePivot=nullptr);
    void AnimateUpperBody(RigPose&,const glm::mat4& heading,float swing,float activity,
                          float lean=.055f,float arm=.30f,float elbow=.12f) const;
    RigPose SampleGait(float seconds,bool enabled);
};
