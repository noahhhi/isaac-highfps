#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>

namespace highfps {
// Per-render occurrence numbers. Generation tags recycle the table without
// allocating/freeing a node for every callback on every display frame.
class RenderOrdinalIndex {
    struct Slot {
        std::uintptr_t entity=0;
        std::uint32_t callback=0,pass=0,next=0,generation=0;
    };
    static constexpr std::size_t kSlots=65536;
    std::vector<Slot> slots_{kSlots};
    std::uint32_t generation_=1;
    std::size_t size_=0;
public:
    static constexpr std::size_t kCapacity=kSlots/2;
    void Clear() {
        size_=0;
        if(++generation_==0) {
            for(auto& slot:slots_)slot.generation=0;
            generation_=1;
        }
    }
    bool Next(std::uintptr_t entity,std::uint32_t callback,std::uint32_t pass,
              std::uint32_t& ordinal) {
        std::uint64_t hash=std::uint64_t(entity) ^ (std::uint64_t(callback)<<32) ^ pass;
        hash^=hash>>30;hash*=0xbf58476d1ce4e5b9ULL;
        hash^=hash>>27;hash*=0x94d049bb133111ebULL;hash^=hash>>31;
        for(std::size_t i=std::size_t(hash)&(kSlots-1), probes=0;probes<kSlots;
            i=(i+1)&(kSlots-1),++probes) {
            auto& slot=slots_[i];
            if(slot.generation!=generation_) {
                if(size_==kCapacity)return false;
                slot={entity,callback,pass,1,generation_};++size_;ordinal=0;return true;
            }
            if(slot.entity==entity && slot.callback==callback && slot.pass==pass) {
                if(slot.next==(std::numeric_limits<std::uint32_t>::max)())return false;
                ordinal=slot.next++;return true;
            }
        }
        return false;
    }
    std::size_t Size()const{return size_;}
};
}
