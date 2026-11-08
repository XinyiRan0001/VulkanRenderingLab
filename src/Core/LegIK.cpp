#include "LegIK.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
glm::vec3 Position(const RigPose& p,int n){return glm::vec3(p.world.at(size_t(n))[3]);}
glm::quat Between(glm::vec3 from,glm::vec3 to,glm::vec3 pole){
    from=glm::normalize(from);to=glm::normalize(to);
    const float dot=glm::clamp(glm::dot(from,to),-1.f,1.f);
    if(dot< -1+1e-6f){
        auto axis=glm::cross(from,pole);
        if(glm::dot(axis,axis)<1e-8f)axis=glm::cross(from,std::abs(from.x)<.8f?glm::vec3(1,0,0):glm::vec3(0,1,0));
        return glm::angleAxis(3.14159265359f,glm::normalize(axis));
    }
    const auto axis=glm::cross(from,to);
    return glm::normalize(glm::quat(1+dot,axis.x,axis.y,axis.z));
}
glm::mat4 Pivot(glm::vec3 p,glm::quat q){
    return glm::translate(glm::mat4(1),p)*glm::mat4_cast(q)*glm::translate(glm::mat4(1),-p);
}
bool Similarity(const glm::mat4& m){
    glm::mat3 a(m);const float x=glm::length(a[0]),y=glm::length(a[1]),z=glm::length(a[2]);
    if(x<1e-6 || std::abs(x-y)>x*1e-3 || std::abs(x-z)>x*1e-3 || glm::determinant(a)<=0)return false;
    return std::abs(glm::dot(a[0],a[1]))<x*x*1e-3 && std::abs(glm::dot(a[0],a[2]))<x*x*1e-3 &&
           std::abs(glm::dot(a[1],a[2]))<x*x*1e-3;
}
std::string Lower(std::string s){for(char& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
}
LegIK::LegIK(const AnimationRig& rig,const std::string& side):m_Rig(rig),m_Side(side){
    if(side!="left" && side!="right" && side!="both" && side!="walk")throw std::runtime_error("Leg IK side must be left|right|both|walk");
    m_Walking=side=="walk";
    const std::string suffix=side=="right"?".r":".l";
    const auto find=[&](const std::string& name){
        int found=-1;
        for(size_t i=0;i<rig.nodes.size();++i)if(Lower(rig.nodes[i].name)==name){
            if(found>=0)throw std::runtime_error("Ambiguous leg bone: "+name);
            found=int(i);
        }
        if(found<0)throw std::runtime_error("Leg IK requires bone "+name+" (current character naming)");
        return found;
    };
    m_Hip=find("upperleg"+suffix);m_Knee=find("lowerleg"+suffix);m_Foot=find("foot"+suffix);
    if(rig.nodes[m_Knee].parent!=m_Hip || rig.nodes[m_Foot].parent!=m_Knee)
        throw std::runtime_error("Leg IK requires a direct upper-leg/lower-leg/foot chain");
    m_Base=rig.BindPose();
    for(size_t i=0;i<rig.clips.size();++i)if(Lower(rig.clips[i].name)=="idle"){m_Base=rig.Sample(i,0,false);break;}
    for(int node:{m_Hip,m_Knee,m_Foot})
        if(!Similarity(m_Base.world[node]))throw std::runtime_error("Leg IK requires positive uniform chain scale without shear");
    const auto h=Position(m_Base,m_Hip),k=Position(m_Base,m_Knee),f=Position(m_Base,m_Foot);
    m_Length=glm::length(k-h)+glm::length(f-k);
    if(glm::length(k-h)<1e-6f || glm::length(f-k)<1e-6f || glm::length(f-h)<1e-6f)
        throw std::runtime_error("Leg IK base chain has coincident joints");
    const auto axis=glm::normalize(f-h);
    auto bend=(k-h)-axis*glm::dot(k-h,axis);
    const int toes=find("toes"+suffix);
    m_Forward=Position(m_Base,toes)-f;m_Forward.y=0;
    if(glm::length(m_Forward)<1e-5)throw std::runtime_error("Leg IK cannot infer horizontal foot forward axis");
    m_Forward=glm::normalize(m_Forward);
    if(glm::length(bend)<m_Length*.005f)bend=m_Forward;
    m_RestBend=glm::normalize(bend);
    m_Pole=h+m_RestBend*m_Length*2.f;
    // Calibrate a sole proxy from vertices rigidly attached to this foot subtree.
    // Blended ankle/calf vertices are deliberately excluded from the contact fit.
    std::vector<glm::vec3> soleVertices;
    float minimumY=std::numeric_limits<float>::max();
    for(const auto& primitive:rig.primitives){
        const int skinIndex=rig.nodes[primitive.node].skin;
        if(skinIndex<0)continue;
        const auto& skin=rig.skins[skinIndex];
        for(size_t vi=0;vi<primitive.vertices.size();++vi){
            const auto& vertex=primitive.vertices[vi];
            float footWeight=0;glm::mat4 transform(0);
            for(int j=0;j<4;++j){
                int node=skin.joints.at(vertex.joints[j]);
                for(int ancestor=node;ancestor>=0;ancestor=rig.nodes[ancestor].parent)
                    if(ancestor==m_Foot){footWeight+=vertex.weights[j];break;}
                transform+=m_Base.world[primitive.node]*m_Base.palettes[primitive.node][vertex.joints[j]]*vertex.weights[j];
            }
            if(footWeight<.999f)continue;
            auto position=vertex.position;
            const auto& morphWeights=m_Base.morphWeights[primitive.node];
            for(size_t mi=0;mi<primitive.morphs.size();++mi)
                if(mi<morphWeights.size())position+=primitive.morphs[mi].positions[vi]*morphWeights[mi];
            const auto point=glm::vec3(transform*glm::vec4(position,1));
            soleVertices.push_back(point);minimumY=std::min(minimumY,point.y);
        }
    }
    glm::vec3 contact(0);size_t count=0;
    for(const auto point:soleVertices)if(point.y<=minimumY+m_Length*.003f){contact+=point;++count;}
    if(!count)throw std::runtime_error("Leg IK needs rigid foot vertices to calibrate the sole");
    contact/=float(count);contact.y=minimumY;
    m_GroundHeight=minimumY;m_BaseContact=contact;m_ContactTarget=contact;
    m_SoleLocal=glm::vec3(glm::inverse(m_Base.world[m_Foot])*glm::vec4(contact,1));
    m_ContactLocal=m_SoleLocal;
    float rear=std::numeric_limits<float>::max(),front=-rear;
    const auto inverseFoot=glm::inverse(m_Base.world[m_Foot]);
    for(const auto point:soleVertices){
        const float along=glm::dot(point-contact,m_Forward);
        rear=std::min(rear,along);front=std::max(front,along);
        m_RigidFootLocal.push_back(glm::vec3(inverseFoot*glm::vec4(point,1)));
    }
    // Conservative sole-plane support edges enclose all rigid shoe vertices.
    // Positive heel pitch and negative toe pitch cannot rotate them below ground.
    m_HeelBase=contact+m_Forward*rear;m_ToeBase=contact+m_Forward*front;
    m_HeelLocal=glm::vec3(inverseFoot*glm::vec4(m_HeelBase,1));
    m_ToeLocal=glm::vec3(inverseFoot*glm::vec4(m_ToeBase,1));
    m_Target=f;m_Last=m_Base;
    std::cout<<"[Leg IK Chain] "<<side<<" hip="<<rig.nodes[m_Hip].name<<" knee="<<rig.nodes[m_Knee].name
             <<" foot="<<rig.nodes[m_Foot].name<<" upper="<<glm::length(k-h)<<" lower="<<glm::length(f-k)<<std::endl;
    std::cout<<"[Leg IK Contact] ground="<<m_GroundHeight<<" sole_vertices="<<count<<" calibration=rigid-foot"<<std::endl;
    if(side=="both" || m_Walking){
        m_Other=std::make_unique<LegIK>(rig,"right");
        m_Pelvis=rig.nodes[m_Hip].parent;
        if(m_Pelvis<0 || rig.nodes[m_Other->m_Hip].parent!=m_Pelvis)
            throw std::runtime_error("Bilateral gait needs a common pelvis parent");
        if(std::abs(m_GroundHeight-m_Other->m_GroundHeight)>m_Length*.02f)
            throw std::runtime_error("Bilateral gait requires approximately level base soles");
        m_GroundHeight=std::min(m_GroundHeight,m_Other->m_GroundHeight);
        m_Other->m_GroundHeight=m_GroundHeight;
        m_Spine=find("spine");m_Chest=find("chest");
        m_UpperArms[0]=find("upperarm.l");m_UpperArms[1]=find("upperarm.r");
        m_LowerArms[0]=find("lowerarm.l");m_LowerArms[1]=find("lowerarm.r");
    }
}
const LegIK& LegIK::Leg(size_t index) const{
    if(index==0)return *this;
    if(index==1 && m_Other)return *m_Other;
    throw std::out_of_range("Leg IK leg index");
}
glm::vec3 LegIK::ContactPosition() const{
    return glm::vec3(m_Last.world[m_Foot]*glm::vec4(m_ContactLocal,1));
}
float LegIK::LowestFootHeight() const{
    float height=std::numeric_limits<float>::max();
    for(const auto point:m_RigidFootLocal)height=std::min(height,(m_Last.world[m_Foot]*glm::vec4(point,1)).y);
    return height;
}
void LegIK::TransformSubtree(RigPose& pose,int root,const glm::mat4& delta) const{
    for(size_t i=0;i<m_Rig.nodes.size();++i){
        for(int node=int(i);node>=0;node=m_Rig.nodes[size_t(node)].parent)
            if(node==root){pose.world[i]=delta*pose.world[i];break;}
    }
}
RigPose LegIK::Sample(float seconds,bool enabled){
    if(!std::isfinite(seconds) || seconds<0)throw std::runtime_error("Leg IK time must be finite and nonnegative");
    if(m_Other)return SampleGait(seconds,enabled);
    auto pose=m_Base;
    const float time=std::fmod(seconds,4.f);
    m_Stance=time<1.f || (time>=2.f && time<3.f);
    const float swing=time<2.f?time-1.f:time-3.f;
    const float s=glm::clamp(swing,0.f,1.f);
    const float smooth=s*s*s*(s*(s*6.f-15.f)+10.f);
    const float u=time<1.f?0.f:time<2.f?smooth:time<3.f?1.f:1.f-smooth;
    const float lift=m_Stance?0.f:16.f*s*s*(1.f-s)*(1.f-s);
    const auto contact=m_BaseContact+m_Forward*(m_Length*.16f*u)+glm::vec3(0,m_Length*.12f*lift,0);
    SolveContact(pose,contact,.7f*u+.15f*lift,m_Stance,enabled);
    if(enabled)m_Rig.RefreshPalettes(pose);
    m_Last=pose;
    return pose;
}
void LegIK::SolveContact(RigPose& pose,glm::vec3 contact,float bend,bool stance,bool enabled,
                         const glm::mat4& bodyHeading,const glm::mat4& footHeading,const glm::vec3* basePivot){
    m_ContactTarget=contact;m_Stance=stance;
    const auto pivot=basePivot?*basePivot:m_BaseContact;
    m_ContactLocal=basePivot?glm::vec3(glm::inverse(m_Base.world[m_Foot])*glm::vec4(pivot,1)):m_SoleLocal;
    m_Target=m_ContactTarget-glm::mat3(footHeading)*(pivot-Position(m_Base,m_Foot));
    const auto root=Position(pose,m_Hip);
    // Preserve the authored bend at rest, then favor the toe direction as the
    // foot lifts. This avoids a sideways knee without snapping the resting pose.
    m_Pole=root+glm::normalize(glm::mat3(bodyHeading)*glm::mix(m_RestBend,m_Forward,bend))*m_Length*2.f;
    const auto solution=SolveTwoBoneIK(root,Position(pose,m_Knee),Position(pose,m_Foot),m_Target,m_Pole);
    m_Clamped=solution.clamped;
    if(enabled){
        const auto upper=Between(Position(pose,m_Knee)-root,solution.knee-root,m_Pole-root);
        TransformSubtree(pose,m_Hip,Pivot(root,upper));
        const auto knee=Position(pose,m_Knee);
        const auto lower=Between(Position(pose,m_Foot)-knee,solution.end-knee,m_Pole-root);
        TransformSubtree(pose,m_Knee,Pivot(knee,lower));
        // Preserve the base foot orientation: translate its descendants without toe roll.
        auto foot=footHeading*m_Base.world[m_Foot];foot[3]=pose.world[m_Foot][3];
        TransformSubtree(pose,m_Foot,foot*glm::inverse(pose.world[m_Foot]));
        const float upperError=std::abs(glm::length(Position(pose,m_Knee)-root)-solution.upperLength);
        const float lowerError=std::abs(glm::length(Position(pose,m_Foot)-Position(pose,m_Knee))-solution.lowerLength);
        if(glm::length(Position(pose,m_Foot)-solution.end)>m_Length*2e-4f || upperError>m_Length*2e-4f || lowerError>m_Length*2e-4f)
            throw std::runtime_error("Leg IK FK/length verification failed");
    }
    m_Error=glm::length(Position(pose,m_Foot)-m_Target);m_Last=pose;
}
void LegIK::AnimateUpperBody(RigPose& pose,const glm::mat4& heading,float swing,float activity,float lean,float arm,float elbow) const{
    if(m_Spine<0)return;
    const auto forward=glm::normalize(glm::mat3(heading)*(m_Forward+m_Other->m_Forward));
    const auto sagittal=glm::normalize(glm::cross(glm::vec3(0,1,0),forward));
    const auto rotate=[&](int node,float angle,glm::vec3 axis){
        TransformSubtree(pose,node,Pivot(Position(pose,node),glm::angleAxis(angle,axis)));
    };
    rotate(m_Spine,lean*activity,sagittal);
    rotate(m_Chest,-.045f*swing*activity,glm::vec3(0,1,0));
    for(int i=0;i<2;++i){
        rotate(m_UpperArms[i],(i==0?1.f:-1.f)*arm*swing*activity,sagittal);
        rotate(m_LowerArms[i],-elbow*(1.f+(2.f/3.f)*std::abs(swing))*activity,sagittal);
    }
}
RigPose LegIK::SampleGait(float seconds,bool enabled){
    // Absolute-time evaluation makes seeking/rebuilds independent of frame rate.
    // Evaluate the easing polynomial in double precision to avoid tiny
    // float cancellation reversals near the final stationary pose.
    const double time=glm::clamp(double(seconds)/WalkDuration,0.,1.);
    const float eased=float(time*time*time*(time*(time*6.-15.)+10.));
    const float cycles=m_Walking?4.f*eased:std::fmod(seconds,4.f)/4.f;
    const float cycleIndex=std::floor(cycles),phase=cycles-cycleIndex;
    const auto forward=glm::normalize(m_Forward+m_Other->m_Forward);
    m_RootOffset=m_Walking?forward*(TravelDistance()*eased):glm::vec3(0);
    auto swing=[](float phase){
        if(phase<=.1f || phase>=.45f)return 0.f;
        const float s=(phase-.1f)/.35f;
        return 16.f*s*s*(1.f-s)*(1.f-s);
    };
    const float leftLift=swing(phase),rightLift=swing(std::fmod(phase+.5f,1.f));
    auto lateral=m_Other->m_BaseContact-m_BaseContact;lateral.y=0;
    if(glm::length(lateral)<m_Length*.01f)throw std::runtime_error("Bilateral gait has no stance width");
    lateral=glm::normalize(lateral);
    // Shift toward the supporting leg and lower the hips to leave reach margin.
    // This is a bounded kinematic heuristic, not a center-of-mass simulation.
    m_PelvisOffset=lateral*(m_Length*.035f*(leftLift-rightLift))-
                   glm::vec3(0,m_Length*((m_Walking?.025f:.012f)+.012f*(leftLift+rightLift)),0);
    auto pose=m_Base;
    const auto rootTransform=glm::translate(glm::mat4(1),m_RootOffset);
    for(auto& world:pose.world)world=rootTransform*world;
    if(enabled)TransformSubtree(pose,m_Pelvis,glm::translate(glm::mat4(1),m_PelvisOffset));
    else m_PelvisOffset=glm::vec3(0);
    if(enabled){
        const float activity=m_Walking?float(16.*time*time*(1.-time)*(1.-time)):1.f;
        AnimateUpperBody(pose,glm::mat4(1),std::sin(6.2831853f*phase),activity);
    }
    for(size_t i=0;i<2;++i){
        auto& leg=i==0?*this:*m_Other;
        const float lift=i==0?leftLift:rightLift;
        auto contact=leg.m_BaseContact+leg.m_Forward*(leg.m_Length*.06f*lift);
        if(m_Walking){
            const float start=i==0?.1f:.6f,end=i==0?.45f:.95f;
            const float s=glm::clamp((phase-start)/(end-start),0.f,1.f);
            const float step=s*s*s*(s*(s*6.f-15.f)+10.f);
            // A planted anchor advances only during that leg's swing window.
            contact=leg.m_BaseContact+forward*((cycleIndex+step)*TravelDistance()/4.f);
        }
        contact.y=m_GroundHeight+leg.m_Length*.10f*lift;
        leg.SolveContact(pose,contact,.55f+.3f*lift,lift==0,enabled);
    }
    if(enabled)m_Rig.RefreshPalettes(pose);
    m_Last=pose;m_Other->m_Last=pose;
    return pose;
}
