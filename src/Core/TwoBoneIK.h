#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

struct TwoBoneSolution {
    glm::vec3 knee{},end{};
    float upperLength=0,lowerLength=0;
    bool clamped=false;
};
inline bool FiniteIk(glm::vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
// Analytic hinge position with an explicit pole. Targets outside the reachable
// annulus are clamped; the pole controls which side of the root/target axis bends.
inline TwoBoneSolution SolveTwoBoneIK(glm::vec3 root,glm::vec3 knee,glm::vec3 end,
                                     glm::vec3 target,glm::vec3 pole){
    if(!FiniteIk(root)||!FiniteIk(knee)||!FiniteIk(end)||!FiniteIk(target)||!FiniteIk(pole))
        throw std::runtime_error("Two-bone IK needs finite positions");
    const float a=glm::length(knee-root),b=glm::length(end-knee);
    if(a<1e-6f || b<1e-6f)throw std::runtime_error("Two-bone IK has a zero-length bone");
    const float epsilon=(a+b)*1e-5f,near=std::abs(a-b)+epsilon,far=a+b-epsilon;
    glm::vec3 offset=target-root;
    const float requested=glm::length(offset);
    glm::vec3 axis=requested>epsilon?offset/requested:glm::normalize(end-root);
    if(!FiniteIk(axis) || glm::length(end-root)<epsilon)axis=glm::normalize(knee-root);
    const float distance=glm::clamp(requested,near,far);
    glm::vec3 bend=(pole-root)-axis*glm::dot(pole-root,axis);
    if(glm::dot(bend,bend)<epsilon*epsilon)
        bend=(knee-root)-axis*glm::dot(knee-root,axis);
    if(glm::dot(bend,bend)<epsilon*epsilon){
        const glm::vec3 helper=std::abs(axis.x)<.8f?glm::vec3(1,0,0):glm::vec3(0,1,0);
        bend=glm::cross(axis,helper);
    }
    bend=glm::normalize(bend);
    const float along=(a*a-b*b+distance*distance)/(2*distance);
    const float height=std::sqrt(std::max(0.f,a*a-along*along));
    return {root+axis*along+bend*height,root+axis*distance,a,b,
            requested<near || requested>far};
}
