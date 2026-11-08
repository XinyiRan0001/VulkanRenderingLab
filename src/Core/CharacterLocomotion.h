#pragma once
#include "LegIK.h"
#include "PoseControls.h"
#include <array>
#include <cstdint>

// CPU-only state can survive swapchain rebuilds independently of GPU resources.
class CharacterLocomotion {
public:
    struct Foot {
        glm::vec3 contact{},start{},goal{};
        float yaw=0,startYaw=0,goalYaw=0,elapsed=0;
    };
    struct State {
        glm::vec3 position{};
        float yaw=0,speed=0,blend=0,distance=0;
        std::array<Foot,2> feet{};
        int active=-1,next=0;
        unsigned steps=0;
        uint32_t poseRevision=0;
    };
    explicit CharacterLocomotion(LegIK& legs);
    void Update(float seconds,glm::vec3 direction,bool ik=true);
    void Reset();
    State Snapshot() const{return m_State;}
    void Restore(const State& state);
    const RigPose& Pose() const{return m_Legs.LastPose();}
    const State& Current() const{return m_State;}
    void SetPoseSettings(const PoseSettings& settings);
    void FitStandingPose();
    const PoseSettings& Settings() const{return m_Settings;}
    enum class FootPhase {Flat,ToeOff,Swing,HeelStrike};
    static const char* PhaseName(FootPhase phase){
        switch(phase){case FootPhase::ToeOff:return "TOE";case FootPhase::Swing:return "AIR";case FootPhase::HeelStrike:return "HEEL";default:return "FLAT";}
    }
    struct Diagnostics {
        float contactError=0,reach[2]={0,0},automaticDrop=0,pitch[2]={0,0},clearance[2]={0,0};
        float stanceWidth=0,bodyForeAft=0,bodyTilt=0;
        float kneeFlex[2]={0,0};
        float automaticHeight=0;
        FootPhase phase[2]={FootPhase::Flat,FootPhase::Flat};bool clamped=false;
    };
    const Diagnostics& PostureDiagnostics() const{return m_Diagnostics;}
private:
    LegIK& m_Legs;
    State m_State;
    PoseSettings m_Settings;
    Diagnostics m_Diagnostics;
    float m_ArmSwing=0;
    glm::vec3 m_BaseForward{1,0,0};
    float m_BaseForeAft=0,m_BaseTilt=0;
    int m_ReferenceTop=-1;
    static constexpr float StepDuration=.38f,MaxSpeed=.35f;
    struct FootPose {glm::vec3 contact{},pivot{};glm::mat4 rotation{1};FootPhase phase=FootPhase::Flat;float pitch=0;};
    FootPose SampleFoot(size_t side) const;
    float TotalStepTime() const{return StepDuration+2*m_Settings.values[PoseSettings::RollTime];}
    void Tick(float seconds,glm::vec3 direction);
    void Evaluate(bool ik);
    glm::mat4 Frame(glm::vec3 position,float yaw) const;
    glm::vec3 Ideal(size_t side,glm::vec3 position,float yaw) const;
};
