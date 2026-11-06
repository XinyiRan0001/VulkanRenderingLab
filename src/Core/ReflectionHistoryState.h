#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

// Whole-frame invalidation for the static-only temporal stage. State advances
// only after submission; preparing an unsubmitted frame cannot create history.
class ReflectionHistoryState {
public:
    using Snapshot=std::array<float,96>;
    struct Frame {bool enabled=false,reset=true;uint32_t previous=0,phase=0;};
    void SetEnabled(bool enabled){
        if(m_Enabled!=enabled){m_Enabled=enabled;m_Valid=false;m_Frames=0;m_Phase=0;}
    }
    bool Enabled() const {return m_Enabled;}
    uint32_t Frames() const {return m_Frames;}
    Frame Prepare(const Snapshot& snapshot){
        m_Prepared=snapshot;
        bool changed=!m_Valid;
        for(std::size_t i=0;i<snapshot.size();++i)
            if(!std::isfinite(snapshot[i]) || !std::isfinite(m_Previous[i]) || std::abs(snapshot[i]-m_Previous[i])>1e-6f)changed=true;
        m_Current={m_Enabled,!m_Enabled || changed,
            m_Enabled && !changed?std::min(m_Frames,63u):0u,m_Enabled?m_Phase:0u};
        return m_Current;
    }
    const Frame& Current() const {return m_Current;}
    void Submit(){
        m_Valid=m_Current.enabled;
        m_Frames=m_Valid?std::min(m_Current.previous+1,64u):0;
        m_Phase=m_Valid?m_Phase+1:0;
        m_Previous=m_Prepared;
    }
private:
    bool m_Enabled=false,m_Valid=false;
    uint32_t m_Frames=0,m_Phase=0;
    Snapshot m_Previous{},m_Prepared{};
    Frame m_Current{};
};
