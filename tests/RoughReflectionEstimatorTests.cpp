#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <cstdint>
namespace {
constexpr double Pi=3.14159265358979323846;
void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
double Lambda(double c,double a){return .5*(std::sqrt(1+a*a*(1-c*c)/(c*c))-1);}
double Fresnel(double c,double f0){return f0+(1-f0)*std::pow(1-c,5);}
double D(double nh,double a){const double x=nh*nh*(a*a-1)+1;return a*a/(Pi*x*x);}
double G(double nv,double nl,double a){return 1/(1+Lambda(nv,a)+Lambda(nl,a));}
double Brdf(glm::dvec3 v,glm::dvec3 l,double a,double f0){
    if(v.z<=0 || l.z<=0)return 0;
    const auto h=glm::normalize(v+l);
    return D(h.z,a)*G(v.z,l.z,a)*Fresnel(glm::dot(v,h),f0)/(4*v.z*l.z);
}
double Radical(uint32_t x){
    double result=0,base=.5;
    while(x){result+=(x&1)*base;base*=.5;x>>=1;}return result;
}
double Importance(double nv,double rough,double f0,int count,bool shapedLight=false){
    const glm::dvec3 v(std::sqrt(1-nv*nv),0,nv);
    const double a=rough*rough;double sum=0;
    for(int i=0;i<count;++i){
        const double u=(i+.5)/count,phi=2*Pi*(Radical(i)+.13);
        const double nh=std::sqrt((1-u)/(1+(a*a-1)*u)),s=std::sqrt(1-nh*nh);
        const glm::dvec3 h(s*std::cos(phi),s*std::sin(phi),nh);
        const double vh=glm::dot(v,h);const auto l=2*vh*h-v;
        if(vh<=0 || l.z<=0)continue;
        const double weight=Fresnel(vh,f0)*G(nv,l.z,a)*vh/(nv*nh);
        const double pdf=D(nh,a)*nh/(4*vh);
        const double explicitWeight=Brdf(v,l,a,f0)*l.z/pdf;
        Require(std::abs(explicitWeight-weight)<1e-8*std::max(1.,weight),"BRDF*cos/PDF cancellation mismatch");
        sum+=weight*(shapedLight?.2+.8*l.x*l.x:1.);
    }
    return sum/count; // Invalid directions retain zero contribution.
}
// Independent solid-angle midpoint quadrature: no sampled half-vectors or PDFs.
double Quadrature(double nv,double rough,double f0,bool shapedLight=false){
    const glm::dvec3 v(std::sqrt(1-nv*nv),0,nv);
    double sum=0;constexpr int elevations=384,azimuths=768;
    for(int y=0;y<elevations;++y){
        const double z=(y+.5)/elevations,s=std::sqrt(1-z*z);
        for(int x=0;x<azimuths;++x){
            const double phi=2*Pi*(x+.5)/azimuths;
            const glm::dvec3 l(s*std::cos(phi),s*std::sin(phi),z);
            sum+=Brdf(v,l,rough*rough,f0)*z*(shapedLight?.2+.8*l.x*l.x:1.);
        }
    }
    return sum*2*Pi/(elevations*azimuths);
}
}
int main(){
    try{
        Require(std::abs(Fresnel(1,.75)-.75)<1e-12 && Fresnel(0,.75)==1,"Fresnel endpoint error");
        Require(std::abs(Importance(1,.04,.75,32768)-.75)<.002,"Smooth limit must approach F0, not F0 squared");
        for(double rough:{.35,.55,.9})for(double nv:{1.,.5,.1}){
            const double white=Importance(nv,rough,1,65536);
            Require(white>=0 && white<=1.015,"Single-scatter white furnace gained energy");
            for(bool shaped:{false,true}){
                const double mc=Importance(nv,rough,.75,65536,shaped);
                const double reference=Quadrature(nv,rough,.75,shaped);
                Require(std::abs(mc-reference)<.009,"Importance estimator differs from hemisphere quadrature");
            }
        }
        // Broad white-Fresnel lobe at normal view: discarding invalid samples and
        // renormalizing accepted ones would spuriously increase this integral.
        const double broad=Importance(1,.9,1,65536);
        Require(broad<.65 && broad>.35,"Rejected samples must stay in the denominator");
        std::cout<<"GGX estimator: explicit BRDF/PDF, smooth F0, white furnace, independent quadrature OK"<<std::endl;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
