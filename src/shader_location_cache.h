#pragma once
// Main-thread-only scalar GL location cache. No GL calls, pointer ownership or
// per-query allocations. The host validates/copies driver name pointers and
// clears on each Manager render and every relevant GL lifetime mutation.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#if defined(_MSC_VER)
#define HIGHFPS_LOCATION_FORCEINLINE __forceinline
#elif defined(__GNUC__)
#define HIGHFPS_LOCATION_FORCEINLINE inline __attribute__((always_inline))
#else
#define HIGHFPS_LOCATION_FORCEINLINE inline
#endif

namespace highfps::shader_locations {
enum class Kind : std::uint8_t { Attribute=0, Uniform=1 };
class Cache {
    friend struct CacheTestAccess;
public:
    static constexpr std::size_t Capacity=4096;
    static constexpr std::size_t NameBytes=128; // Includes NUL; never truncate.
    static constexpr std::size_t IndexSlots=8192;
    static constexpr std::size_t FrontSlots=256;
    struct Stats {
        std::uint64_t finds=0,hits=0,misses=0,stores=0,updates=0;
        std::uint64_t rejectedInvalid=0,rejectedNegative=0,rejectedFull=0;
        std::uint64_t probes=0,clears=0,frontHits=0;
    };
private:
    struct Entry {
        std::uint64_t hash=0;
        std::uintptr_t context=0;
        std::uint32_t program=0;
        int location=0;
        std::uint8_t length=0;
        Kind kind=Kind::Attribute;
        std::array<char,NameBytes> name{};
    };
    struct Slot {std::uint32_t generation=0,index=0;};
    struct Storage {std::array<Entry,Capacity> entries{};std::array<Slot,IndexSlots> index{};std::array<Slot,FrontSlots> front{};};
    static_assert((IndexSlots&(IndexSlots-1))==0&&IndexSlots>=2*Capacity,"bounded half-full index");
    static_assert(sizeof(Storage)<=1024u*1024u,"location cache storage must fit one MiB");
    std::unique_ptr<Storage> storage_; // One allocation, at construction only.
    std::uint32_t generation_=1;
    std::size_t size_=0;
    Stats stats_{};
    struct Name {const char* text=nullptr;std::uint8_t length=0;};
    static bool Inspect(std::uintptr_t context,std::uint32_t program,Kind kind,const char*text,Name&name){
        if(!context||!program||!text||(kind!=Kind::Attribute&&kind!=Kind::Uniform))return false;
        for(std::size_t n=0;n<NameBytes;++n)if(!text[n]){
            if(!n)return false;name={text,std::uint8_t(n)};return true;
        }
        return false; // Longer or unterminated names stay on the driver path.
    }
    static std::uint32_t Rotate(std::uint32_t x,unsigned n){return (x<<n)|(x>>(32-n));}
    static std::uint32_t MixWord(std::uint32_t h,std::uint32_t word){
        word*=0xcc9e2d51u;word=Rotate(word,15);word*=0x1b873593u;
        return Rotate(h^word,13)*5u+0xe6546b64u;
    }
    static std::uint64_t Hash(std::uintptr_t context,std::uint32_t program,Kind kind,Name name){
        // Murmur3-style 32-bit word mixing avoids per-byte 64-bit multiplies on
        // x86. Hashes only select candidates; Matches always compares every key
        // field and the exact copied name, including on a full hash collision.
        std::uint32_t h=MixWord(0x9747b28cu,std::uint32_t(context));
        if constexpr(sizeof(context)>4)h=MixWord(h,std::uint32_t(std::uint64_t(context)>>32));
        h=MixWord(h,program);h=MixWord(h,std::uint32_t(kind));
        unsigned i=0;
        for(;i+4<=name.length;i+=4){std::uint32_t word;std::memcpy(&word,name.text+i,4);h=MixWord(h,word);}
        std::uint32_t tail=0;unsigned shift=0;
        for(;i<name.length;++i,shift+=8)tail|=std::uint32_t(static_cast<unsigned char>(name.text[i]))<<shift;
        if(shift){tail*=0xcc9e2d51u;tail=Rotate(tail,15);tail*=0x1b873593u;h^=tail;}
        h^=std::uint32_t(name.length+sizeof(context)+8);h^=h>>16;h*=0x85ebca6bu;
        h^=h>>13;h*=0xc2b2ae35u;return h^(h>>16);
    }
    static bool InspectValidated(std::uintptr_t context,std::uint32_t program,Kind kind,const char*text,std::size_t length,Name&name){
        if(!context||!program||!text||!length||length>=NameBytes||
           (kind!=Kind::Attribute&&kind!=Kind::Uniform)||text[length]!=0)return false;
        name={text,std::uint8_t(length)};return true;
    }
    // One direct candidate, never a front-table scan. Collisions fall through to
    // the authoritative hash index; all identities are compared before a hit.
    static HIGHFPS_LOCATION_FORCEINLINE unsigned HotClass(Kind kind,Name name){
        // Shapes of names declared in the shipped coloroffset, champion, dogma
        // and gold shaders. Classification is not identity: SameKey must still
        // compare every byte. Other shader parameters retain the general index.
        const unsigned length=name.length;
        if(length<5||length>16)return 16;
        const char first=name.text[0],last=name.text[length-1];
        if(kind==Kind::Uniform){
            if(length==8&&first=='T'&&last=='0')return 9; // Texture0
            if(length==9&&first=='T'&&last=='m')return 10; // Transform
            return 16;
        }
        switch(length){
        case 5:if(first=='C'&&last=='r')return 1;break; // Color
        case 8:if(first=='P'&&last=='n')return 0; // Position
               if(first=='T'&&last=='d')return 2;break; // TexCoord
        case 9:if(first=='C'&&last=='e')return 7;break; // ClipPlane
        case 10:if(first=='C'&&last=='n')return 3;break; // ColorizeIn
        case 11:if(first=='T'&&last=='e')return 5;break; // TextureSize
        case 13:if(first=='C'&&last=='n')return 4;break; // ColorOffsetIn
        case 15:if(first=='C'&&last=='n')return 8;break; // ChampionColorIn
        case 16:if(first=='P'&&last=='t')return 6;break; // PixelationAmount
        }
        return 16;
    }
    static std::size_t FrontIndex(std::uintptr_t context,std::uint32_t program,unsigned nameClass){
        std::uint32_t programGroup=program^std::uint32_t(context>>4)^std::uint32_t(context>>16);
        if constexpr(sizeof(context)>4)programGroup^=std::uint32_t(std::uint64_t(context)>>32);
        return ((programGroup&15u)<<4)|nameClass;
    }
    static bool SameKey(const Entry&e,std::uintptr_t context,std::uint32_t program,Kind kind,Name name){
        return e.context==context&&e.program==program&&e.kind==kind&&e.length==name.length&&
            std::memcmp(e.name.data(),name.text,name.length)==0;
    }
    static bool Matches(const Entry&e,std::uint64_t hash,std::uintptr_t context,std::uint32_t program,Kind kind,Name name){
        return e.hash==hash&&SameKey(e,context,program,kind,name);
    }
    std::size_t Locate(std::uintptr_t context,std::uint32_t program,Kind kind,Name name,std::uint64_t hash,bool&found){
        std::size_t slot=std::size_t(hash)&(IndexSlots-1);found=false;
        // No tombstones/deletions within a generation, and at most half the
        // index can be occupied. The bound also makes corruption fail closed.
        for(std::size_t n=0;n<IndexSlots;++n){
            ++stats_.probes;const auto&s=storage_->index[slot];
            if(s.generation!=generation_)return slot;
            if(s.index>=size_)return IndexSlots;
            if(Matches(storage_->entries[s.index],hash,context,program,kind,name)){found=true;return slot;}
            slot=(slot+1)&(IndexSlots-1);
        }
        return IndexSlots;
    }
public:
    Cache():storage_(std::make_unique<Storage>()){}
    Cache(const Cache&)=delete;Cache&operator=(const Cache&)=delete;
    Cache(Cache&&)=delete;Cache&operator=(Cache&&)=delete;
    static constexpr std::size_t StorageBytes(){return sizeof(Storage);}
    std::size_t Size()const{return size_;}
    Stats GetStats()const{return stats_;}
    void ResetStats(){stats_={};}
    void BeginFrame(){Clear();}
    void Clear(){
        size_=0;++stats_.clears;
        if(++generation_==0){storage_->index.fill(Slot{});storage_->front.fill(Slot{});generation_=1;}
    }
private:
    // Keep the front decision and fallback in the public lookup body: otherwise
    // x86 outlines two helpers and adds call overhead even for bypassed names.
    HIGHFPS_LOCATION_FORCEINLINE bool FindHashed(std::uintptr_t context,std::uint32_t program,Kind kind,Name name,int&result,Slot*front){
        const auto hash=Hash(context,program,kind,name);bool found=false;const auto slot=Locate(context,program,kind,name,hash,found);
        if(!found||slot==IndexSlots){++stats_.misses;return false;}
        const auto index=storage_->index[slot].index;if(front)*front={generation_,index};
        result=storage_->entries[index].location;++stats_.hits;return true;
    }
    HIGHFPS_LOCATION_FORCEINLINE bool FindName(std::uintptr_t context,std::uint32_t program,Kind kind,Name name,int&result){
        const unsigned nameClass=HotClass(kind,name);
        if(nameClass==16)return FindHashed(context,program,kind,name,result,nullptr);
        auto&front=storage_->front[FrontIndex(context,program,nameClass)];
        if(front.generation==generation_&&front.index<size_){
            const auto&e=storage_->entries[front.index];
            if(SameKey(e,context,program,kind,name)){result=e.location;++stats_.hits;++stats_.frontHits;return true;}
        }
        return FindHashed(context,program,kind,name,result,&front);
    }
    bool StoreName(std::uintptr_t context,std::uint32_t program,Kind kind,Name name,int location){
        const auto hash=Hash(context,program,kind,name);bool found=false;const auto slot=Locate(context,program,kind,name,hash,found);
        if(slot==IndexSlots){++stats_.rejectedInvalid;return false;}
        if(found){storage_->entries[storage_->index[slot].index].location=location;++stats_.updates;return true;}
        if(size_==Capacity){++stats_.rejectedFull;return false;}
        auto&e=storage_->entries[size_];e.hash=hash;e.context=context;e.program=program;e.kind=kind;e.location=location;e.length=name.length;
        std::memcpy(e.name.data(),name.text,std::size_t(name.length)+1);
        storage_->index[slot]={generation_,std::uint32_t(size_)};++size_;return true;
    }
public:
    // Valid readable strings only. Runtime handles SEH/bounded pointer copying.
    // Failure never writes the caller's result. Legacy API retains bounded scan.
    bool Find(std::uintptr_t context,std::uint32_t program,Kind kind,const char*text,int&result){
        ++stats_.finds;Name name;
        if(!Inspect(context,program,kind,text,name)){++stats_.rejectedInvalid;++stats_.misses;return false;}
        return FindName(context,program,kind,name,result);
    }
    bool Store(std::uintptr_t context,std::uint32_t program,Kind kind,const char*text,int location){
        ++stats_.stores;if(location<0){++stats_.rejectedNegative;return false;}
        Name name;if(!Inspect(context,program,kind,text,name)){++stats_.rejectedInvalid;return false;}
        return StoreName(context,program,kind,name,location);
    }
    // Host has already validated the FIRST NUL at text[length], with all bytes
    // [0,length] readable. Do not pass a length spanning an embedded NUL. This
    // avoids scanning a second time after the runtime's guarded name copy.
    bool FindValidated(std::uintptr_t context,std::uint32_t program,Kind kind,const char*text,std::size_t length,int&result){
        ++stats_.finds;Name name;
        if(!InspectValidated(context,program,kind,text,length,name)){++stats_.rejectedInvalid;++stats_.misses;return false;}
        return FindName(context,program,kind,name,result);
    }
    bool StoreValidated(std::uintptr_t context,std::uint32_t program,Kind kind,const char*text,std::size_t length,int location){
        ++stats_.stores;if(location<0){++stats_.rejectedNegative;return false;}
        Name name;if(!InspectValidated(context,program,kind,text,length,name)){++stats_.rejectedInvalid;return false;}
        return StoreName(context,program,kind,name,location);
    }
};
}

#undef HIGHFPS_LOCATION_FORCEINLINE
