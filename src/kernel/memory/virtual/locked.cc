#include "kernel/task.h"
namespace kernel::task {
bool AddressSpace::Residency::contains(uint64_t address)const {
    for(unsigned i=0;i<count;++i)if(address>=ranges[i].begin && address<ranges[i].end)return true;
    return false;
}
uint64_t AddressSpace::Residency::overlap(uint64_t address,uint64_t size)const {
    uint64_t bytes=0;
    for(unsigned i=0;i<count;++i) {
        const auto begin=address>ranges[i].begin?address:ranges[i].begin;
        const auto end=address+size<ranges[i].end?address+size:ranges[i].end;
        if(begin<end)bytes+=end-begin;
    }
    return bytes;
}
bool AddressSpace::Residency::set(uint64_t address,uint64_t size,bool locked) {
    if(!size || address>UINT64_MAX-size)return false;
    Residency next;
    auto emit=[&](uint64_t begin,uint64_t end) {
        if(begin>=end)return true;
        if(next.count && next.ranges[next.count-1].end==begin) {
            next.bytes+=end-begin;next.ranges[next.count-1].end=end;return true;
        }
        if(next.count==128)return false;
        next.ranges[next.count++]={begin,end};next.bytes+=end-begin;return true;
    };
    uint64_t begin=address,end=address+size;
    bool inserted=false;
    for(unsigned i=0;i<count;++i) {
        const auto old=ranges[i];
        if(locked) {
            if(old.end<begin){if(!emit(old.begin,old.end))return false;}
            else if(old.begin>end) {
                if(!inserted){if(!emit(begin,end))return false;inserted=true;}
                if(!emit(old.begin,old.end))return false;
            } else {if(old.begin<begin)begin=old.begin;if(old.end>end)end=old.end;}
        } else if(old.end<=begin || old.begin>=end) {
            if(!emit(old.begin,old.end))return false;
        } else {
            if(old.begin<begin && !emit(old.begin,begin))return false;
            if(old.end>end && !emit(end,old.end))return false;
        }
    }
    if(locked && !inserted && !emit(begin,end))return false;
    *this=next;return true;
}
}
