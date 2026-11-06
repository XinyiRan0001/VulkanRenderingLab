#include "Core/ReflectionHistoryState.h"
#include <iostream>
#include <limits>
#include <stdexcept>
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    try{
        ReflectionHistoryState state;ReflectionHistoryState::Snapshot key{};
        auto f=state.Prepare(key);Require(!f.enabled && f.previous==0 && f.phase==0,"Disabled stage must not reuse history");state.Submit();
        state.SetEnabled(true);f=state.Prepare(key);Require(f.enabled && f.reset && f.previous==0 && f.phase==0,"Enable must start fresh");
        state.Prepare(key);Require(state.Frames()==0,"Prepare must not count an unsubmitted frame");state.Submit();
        f=state.Prepare(key);Require(!f.reset && f.previous==1 && f.phase==1,"Static frame must reuse submitted history");state.Submit();
        key[0]=.1f;f=state.Prepare(key);Require(f.reset && f.previous==0 && f.phase==2,"Camera change must clear history but vary samples");state.Submit();
        key[84]=.52f;f=state.Prepare(key);Require(f.reset && f.previous==0,"Pose change must clear history");state.Submit();
        key[88]=64;f=state.Prepare(key);Require(f.reset && f.previous==0,"Sampling change must clear history");state.Submit();
        for(int i=0;i<100;++i){state.Prepare(key);state.Submit();}
        Require(state.Frames()==64 && state.Prepare(key).previous==63,"History must cap with normalized 1/64 updates");
        auto invalid=key;invalid[0]=std::numeric_limits<float>::quiet_NaN();
        Require(state.Prepare(invalid).previous==0,"Non-finite state must reset");state.Submit();
        Require(state.Prepare(key).previous==0,"Non-finite predecessor must not be reused");
        state.SetEnabled(false);f=state.Prepare(key);Require(!f.enabled && f.previous==0 && f.phase==0,"Disable must select a current-frame result");state.Submit();
        state.SetEnabled(true);f=state.Prepare(key);Require(f.reset && f.previous==0 && f.phase==0,"Re-enable must not resurrect old history");
        std::cout<<"Reflection history: static reuse, motion/pose/parameter reset, submission, cap and toggle OK"<<std::endl;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
