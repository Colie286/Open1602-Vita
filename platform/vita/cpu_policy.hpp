// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace o1602::platform::vita {
// Only this process's verified writes may later be lowered or restored.
// Hardware calls are injected so plugin interference and SDK failures can be
// exercised without pretending Vita3K changes a physical CPU clock.
class CpuClockLease {
public:
    enum class State { Unchanged, Applied, ExternalHigher, ReadError, SetError, Unverified };
    struct Result { State state; int before,requested,rc,after; };
    template<class Read,class Write> Result apply(int target,Read read,Write write) {
        const int before=read();
        if(before<=0)return {State::ReadError,before,target,-1,before};
        if(m_owned&&before!=m_owned){m_owned=0;m_original=0;}
        if(target==0){
            if(!m_owned)return {State::Unchanged,before,0,0,before};
            target=m_original;
        }else if(before>target&&before!=m_owned)
            return {State::ExternalHigher,before,target,0,before};
        if(before==target)return {State::Unchanged,before,target,0,before};
        const bool restoring=target==m_original&&m_owned;
        const int rc=write(target),after=read();
        if(rc<0)return {State::SetError,before,target,rc,after};
        if(after!=target){if(after!=before)m_owned=m_original=0;return {State::Unverified,before,target,rc,after};}
        if(restoring){m_owned=m_original=0;}
        else {if(!m_owned)m_original=before;m_owned=after;}
        return {State::Applied,before,target,rc,after};
    }
    int owned()const noexcept{return m_owned;}
private:
    int m_original=0,m_owned=0;
};
}
