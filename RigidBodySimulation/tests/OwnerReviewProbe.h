#pragma once
// Isolated Owner rendering investigation only; absent from ordinary products.
#ifdef GENGINE_OWNER_REVIEW_PROBE
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <new>
#include <malloc.h>
#include <imgui/imgui.h>
namespace Rbs::ReviewMetrics
{
    inline thread_local bool counting{};
    inline thread_local unsigned long long allocations{}, bytes{};
    inline std::array<unsigned long long, 5> ns{}, calls{}, allocs{};
    inline unsigned long long Now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    struct Scope
    {
        int id; unsigned long long start=Now(), before=allocations;
        explicit Scope(int i):id(i){}
        ~Scope(){ns[id]+=Now()-start;++calls[id];allocs[id]+=allocations-before;}
    };
    inline void* Allocate(std::size_t n, std::size_t alignment=0)
    {
        auto* p=alignment?_aligned_malloc(n?n:1,alignment):std::malloc(n?n:1);
        if(!p)throw std::bad_alloc();
        if(counting){++allocations;bytes+=n;}
        return p;
    }
}
#ifdef GENGINE_OWNER_REVIEW_ALLOC_IMPLEMENTATION
void* operator new(std::size_t n){return Rbs::ReviewMetrics::Allocate(n);}
void* operator new[](std::size_t n){return Rbs::ReviewMetrics::Allocate(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){return Rbs::ReviewMetrics::Allocate(n,std::size_t(a));}
void* operator new[](std::size_t n,std::align_val_t a){return Rbs::ReviewMetrics::Allocate(n,std::size_t(a));}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return Rbs::ReviewMetrics::Allocate(n,std::size_t(a));}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return Rbs::ReviewMetrics::Allocate(n,std::size_t(a));}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{_aligned_free(p);}
#endif
#define RBS_REVIEW_SCOPE(id) Rbs::ReviewMetrics::Scope reviewScope##id(id)
#else
#define RBS_REVIEW_SCOPE(id)
#endif
