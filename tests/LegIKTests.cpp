#include "LegIK.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
float Difference(const glm::mat4& a,const glm::mat4& b){
    float result=0;for(int c=0;c<4;++c)for(int r=0;r<4;++r)result=std::max(result,std::abs(a[c][r]-b[c][r]));return result;
}
void CheckLengths(glm::vec3 root,const TwoBoneSolution& s){
    Require(std::abs(glm::length(s.knee-root)-s.upperLength)<1e-4f,"Upper segment stretched");
    Require(std::abs(glm::length(s.end-s.knee)-s.lowerLength)<1e-4f,"Lower segment stretched");
    Require(FiniteIk(s.knee)&&FiniteIk(s.end),"Nonfinite IK solution");
}
int main(int argc,char** argv){
    try{
        const glm::vec3 root(0),knee(0,-1,0),end(0,-2,0),pole(0,0,2),target(.5f,-1.4f,.2f);
        auto solution=SolveTwoBoneIK(root,knee,end,target,pole);CheckLengths(root,solution);
        Require(!solution.clamped && glm::length(solution.end-target)<1e-5f,"Reachable target missed");
        Require(solution.knee.z>0,"Knee must follow pole side");
        solution=SolveTwoBoneIK(root,knee,end,{0,-10,0},{0,-5,0});CheckLengths(root,solution);
        Require(solution.clamped && glm::length(solution.end-root)<2,"Far target not clamped");
        CheckLengths(root,SolveTwoBoneIK(root,knee,end,root,pole));
        CheckLengths(root,SolveTwoBoneIK(root,knee,root,root,pole));
        auto folded=SolveTwoBoneIK(root,{0,-.5f,0},end,root,pole);CheckLengths(root,folded);
        Require(folded.clamped && glm::length(folded.end)>1,"Near target not clamped to annulus");
        bool rejected=false;try{SolveTwoBoneIK(root,root,end,target,pole);}catch(...){rejected=true;}
        Require(rejected,"Zero-length chain accepted");
        Require(argc==2,"Character model path required");
        auto rig=AnimationRig::Load(argv[1]);
        for(const std::string side:{"left","right"}){
            LegIK ik(rig,side);const auto base=ik.LastPose();
            float maximum=0;
            for(int i=0;i<=160;++i){
                auto pose=ik.Sample(float(i)*4/160);
                maximum=std::max(maximum,ik.Error());
                Require(!ik.Clamped(),"Normal demo target unexpectedly unreachable");
                Require(ik.Error()<1e-4f,"Real character target tracking error");
                Require(glm::length(ik.ContactPosition()-ik.ContactTarget())<1e-4f,"Sole missed contact target");
                Require(ik.ContactPosition().y>=ik.GroundHeight()-1e-4f,"Sole penetrated calibrated floor");
                if(ik.Stance())Require(std::abs(ik.ContactPosition().y-ik.GroundHeight())<1e-4f,"Support foot lifted");
                for(size_t n=0;n<rig.nodes.size();++n){
                    bool child=false;
                    for(int p=int(n);p>=0;p=rig.nodes[p].parent)if(p==ik.Hip()){child=true;break;}
                    if(!child)Require(Difference(base.world[n],pose.world[n])<1e-6f,"IK changed another body subtree");
                }
                for(int c=0;c<3;++c)for(int r=0;r<3;++r)
                    Require(std::abs(base.world[ik.Foot()][c][r]-pose.world[ik.Foot()][c][r])<1e-5f,"Foot orientation changed");
                for(size_t node=0;node<rig.nodes.size();++node)if(rig.nodes[node].skin>=0){
                    const auto& skin=rig.skins[rig.nodes[node].skin];
                    for(size_t j=0;j<skin.joints.size();++j){
                        const auto expected=pose.world[skin.joints[j]]*skin.inverseBind[j];
                        Require(Difference(pose.world[node]*pose.palettes[node][j],expected)<1e-5f,"Skin palette lost joint world transform");
                    }
                }
            }
            // Stance anchors must remain fixed, independent of sampling order.
            ik.Sample(.1f);const auto rear=ik.ContactPosition();
            ik.Sample(.9f);Require(ik.Stance() && glm::length(ik.ContactPosition()-rear)<1e-5f,"Rear support foot slid");
            ik.Sample(2.1f);const auto front=ik.ContactPosition();
            ik.Sample(2.9f);Require(ik.Stance() && glm::length(ik.ContactPosition()-front)<1e-5f,"Front support foot slid");
            Require(glm::length(front-rear)>.1f,"Step has no horizontal travel");
            for(float boundary:{0.f,1.f,2.f,3.f,4.f}){
                const float before=boundary==0?3.999f:boundary-.001f;
                ik.Sample(before);const auto p=ik.ContactPosition();
                ik.Sample(boundary+.001f);Require(glm::length(ik.ContactPosition()-p)<1e-4f,"Contact jumped at phase boundary");
            }
            ik.Sample(1.5f);Require(!ik.Stance() && ik.ContactPosition().y-ik.GroundHeight()>.1f,"Swing has no clearance");
            // Central differences approach zero at lift-off and landing (no velocity pop).
            for(float boundary:{1.f,2.f,3.f,4.f}){
                ik.Sample(boundary-.001f);const auto before=ik.ContactTarget();
                ik.Sample(boundary+.001f);Require(glm::length(ik.ContactTarget()-before)/.002f<.02f,"Phase boundary velocity discontinuity");
            }
            auto disabled=ik.Sample(2,false);
            for(size_t n=0;n<rig.nodes.size();++n)Require(Difference(disabled.world[n],base.world[n])==0,"Disabled IK changed base");
            std::cout<<"[Leg IK Test] side="<<side<<" poses=161 max_target_error="<<maximum<<" other_subtrees=OK palettes=OK"<<std::endl;
        }
        LegIK gait(rig,"both");const auto original=gait.LastPose();
        Require(gait.LegCount()==2 && gait.Pelvis()>=0,"Missing bilateral rig");
        gait.Sample(0);
        const glm::vec3 anchors[]={gait.Leg(0).ContactTarget(),gait.Leg(1).ContactTarget()};
        const auto lateral=glm::normalize(anchors[1]-anchors[0]);
        int leftSwings=0,rightSwings=0,doubleSupports=0;
        for(int i=0;i<=400;++i){
            const float time=float(i)*4/400;
            const auto pose=gait.Sample(time);
            Require(!gait.Clamped() && gait.Error()<1e-4f,"Bilateral gait lost target reach");
            Require(gait.Leg(0).Stance() || gait.Leg(1).Stance(),"Both feet airborne");
            Require(glm::length(gait.PelvisOffset())<.1f,"Excessive pelvis displacement");
            Require(glm::length(glm::vec3(pose.world[gait.Pelvis()][3]-original.world[gait.Pelvis()][3])-gait.PelvisOffset())<1e-5f,"Pelvis curve not applied");
            for(size_t j=0;j<2;++j){
                const auto& leg=gait.Leg(j);
                const auto contact=leg.ContactPosition();
                Require(glm::length(contact-leg.ContactTarget())<1e-4f,"Bilateral sole target missed");
                Require(contact.y>=gait.GroundHeight()-1e-4f,"Bilateral floor penetration");
                if(leg.Stance()){
                    Require(glm::length(contact-anchors[j])<1e-4f,"Pelvis movement dragged support foot");
                }else{
                    if(j==0)++leftSwings;else ++rightSwings;
                    const float towardSupport=glm::dot(gait.PelvisOffset(),lateral)*(j==0?1.f:-1.f);
                    Require(towardSupport>=-1e-6f,"Pelvis shifted toward swing foot");
                }
                for(const auto pair:{std::pair<int,int>{leg.Hip(),leg.Knee()},{leg.Knee(),leg.Foot()}}){
                    const float initial=glm::length(glm::vec3(original.world[pair.first][3]-original.world[pair.second][3]));
                    const float current=glm::length(glm::vec3(pose.world[pair.first][3]-pose.world[pair.second][3]));
                    Require(std::abs(initial-current)<1e-4f,"Bilateral bone stretched");
                }
            }
            if(gait.Stance() && gait.Leg(1).Stance())++doubleSupports;
            for(size_t node=0;node<rig.nodes.size();++node)if(rig.nodes[node].skin>=0){
                const auto& skin=rig.skins[rig.nodes[node].skin];
                for(size_t j=0;j<skin.joints.size();++j)
                    Require(Difference(pose.world[node]*pose.palettes[node][j],pose.world[skin.joints[j]]*skin.inverseBind[j])<1e-5f,"Bilateral skin palette inconsistent");
            }
        }
        Require(leftSwings>100 && rightSwings>100 && doubleSupports>50,"Missing gait phases");
        for(size_t side=0;side<2;++side){
            gait.Sample(side==0?1.1f:3.1f);
            Require(gait.Leg(side).ContactPosition().y-gait.GroundHeight()>.1f,"Gait swing has no visible clearance");
            Require(gait.Leg(1-side).Stance(),"Opposite leg is not supporting at swing peak");
        }
        for(float boundary:{0.f,.4f,1.8f,2.f,2.4f,3.8f,4.f}){
            gait.Sample(boundary==0?3.9999f:boundary-.0001f);const auto before=gait.LastPose();
            gait.Sample(boundary+.0001f);
            for(size_t n=0;n<rig.nodes.size();++n)
                Require(Difference(before.world[n],gait.LastPose().world[n])<.0003f,"Gait boundary pose discontinuity");
        }
        const auto first=gait.Sample(1.1f);gait.Sample(3.2f);const auto repeated=gait.Sample(1.1f);
        for(size_t n=0;n<rig.nodes.size();++n)Require(Difference(first.world[n],repeated.world[n])==0,"Gait depends on sampling history");
        const auto off=gait.Sample(1.1f,false);
        for(size_t n=0;n<rig.nodes.size();++n)Require(Difference(off.world[n],original.world[n])==0,"Disabled gait modified pose");
        Require(glm::length(gait.PelvisOffset())==0,"Disabled gait retained pelvis offset");
        std::cout<<"[Gait Test] poses=401 planted_contacts=OK pelvis=OK continuity=OK palettes=OK seek=OK disabled=OK"<<std::endl;
        LegIK walk(rig,"walk");const auto walkBase=walk.LastPose();
        glm::vec3 previousContacts[2]{};bool previousStance[2]={false,false};float previousDistance=-1;
        float maximumWalkError=0;
        for(int sample=0;sample<=800;++sample){
            const float time=LegIK::WalkDuration*sample/800.f;
            const auto pose=walk.Sample(time);
            maximumWalkError=std::max(maximumWalkError,walk.Error());
            Require(!walk.Clamped() && walk.Error()<1e-4f,"Walking target unreachable");
            Require(walk.Stance() || walk.Leg(1).Stance(),"Walking has no support foot");
            const float distance=glm::length(walk.RootOffset());
            Require(distance>=previousDistance && distance<=walk.TravelDistance()+1e-5f,"Root path reversed or overshot");
            previousDistance=distance;
            for(size_t side=0;side<2;++side){
                const auto& leg=walk.Leg(side);
                const auto contact=leg.ContactPosition();
                Require(glm::length(contact-leg.ContactTarget())<1e-4f,"Walking sole target missed");
                Require(contact.y>=walk.GroundHeight()-1e-4f,"Walking foot penetrated floor");
                if(leg.Stance()){
                    Require(std::abs(contact.y-walk.GroundHeight())<1e-4f,"Walking support foot left ground");
                    if(previousStance[side])Require(glm::length(contact-previousContacts[side])<1e-4f,"Walking support foot slid");
                }
                previousContacts[side]=contact;previousStance[side]=leg.Stance();
                for(const auto pair:{std::pair<int,int>{leg.Hip(),leg.Knee()},{leg.Knee(),leg.Foot()}}){
                    const float initial=glm::length(glm::vec3(walkBase.world[pair.first][3]-walkBase.world[pair.second][3]));
                    const float current=glm::length(glm::vec3(pose.world[pair.first][3]-pose.world[pair.second][3]));
                    Require(std::abs(initial-current)<1e-4f,"Walking bone stretched");
                }
            }
            const glm::vec3 actualPelvisDelta(pose.world[walk.Pelvis()][3]-walkBase.world[walk.Pelvis()][3]);
            Require(glm::length(actualPelvisDelta-walk.RootOffset()-walk.PelvisOffset())<1e-5f,"Root and pelvis translation inconsistent");
            for(size_t node=0;node<rig.nodes.size();++node)if(rig.nodes[node].skin>=0){
                const auto& skin=rig.skins[rig.nodes[node].skin];
                for(size_t j=0;j<skin.joints.size();++j)
                    Require(Difference(pose.world[node]*pose.palettes[node][j],pose.world[skin.joints[j]]*skin.inverseBind[j])<1e-5f,"Walking palette inconsistent");
            }
        }
        Require(std::abs(glm::length(walk.RootOffset())-walk.TravelDistance())<1e-5f && walk.Stance() && walk.Leg(1).Stance(),"Walk did not finish on both feet");
        const auto stopped=walk.LastPose();walk.Sample(100);
        for(size_t n=0;n<rig.nodes.size();++n)Require(Difference(stopped.world[n],walk.LastPose().world[n])==0,"Walk wrapped at end");
        const auto walkFirst=walk.Sample(3.2f);walk.Sample(6.4f);const auto walkRepeat=walk.Sample(3.2f);
        for(size_t n=0;n<rig.nodes.size();++n)Require(Difference(walkFirst.world[n],walkRepeat.world[n])==0,"Walking seek retained stale anchors");
        const auto walkingOff=walk.Sample(3.2f,false);
        for(size_t n=0;n<rig.nodes.size();++n){
            auto expected=walkBase.world[n];expected[3]+=glm::vec4(walk.RootOffset(),0);
            Require(Difference(walkingOff.world[n],expected)<1e-6f,"FK comparison lost root motion");
        }
        walk.Sample(.001f);Require(glm::length(walk.RootOffset())/.001f<.001f,"Walking starts abruptly");
        walk.Sample(LegIK::WalkDuration-.001f);Require((walk.TravelDistance()-glm::length(walk.RootOffset()))/.001f<.001f,"Walking stops abruptly");
        std::cout<<"[Walk Test] poses=801 max_target_error="<<maximumWalkError<<" support_lock=OK root=OK stop=OK seek=OK palettes=OK"<<std::endl;
        std::cout<<"Two-bone IK: target, pole, annulus, singularities, authored rig and foot orientation OK"<<std::endl;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
