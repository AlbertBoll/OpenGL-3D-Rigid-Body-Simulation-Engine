#include "SpatialPartition/KDTree.h"
#include <concepts>
#include <type_traits>
using Point = ::GEngine::Point3D<float>;
using DoublePoint = ::GEngine::Point3D<double>;
using Error = ::GEngine::PointCoordinateError;
static_assert(std::same_as<decltype(std::declval<const Point&>()[0]), std::expected<float, Error>>);
static_assert(std::same_as<decltype(std::declval<const DoublePoint&>()[0]), std::expected<float, Error>>);
static_assert(!::GEngine::FloatOrDouble<int>);
static_assert(std::is_trivially_copyable_v<Error>);
#ifndef SPATIAL_POINT_SCHEMA_ONLY
#include <array>
#include <cstdlib>
#include <new>
#include <print>
#include <limits>

namespace Audit
{
    struct Entry { void* pointer=nullptr; bool live=false; };
    std::array<Entry,1024> entries{};
    bool enabled=false,valid=true;
    unsigned allocated=0,retired=0,live=0;
    void Created(void* pointer)
    {
        if(!enabled)return;
        for(const auto& entry:entries)if(entry.live && entry.pointer==pointer)valid=false;
        for(auto& entry:entries)if(!entry.live){entry={pointer,true};++allocated;++live;return;}
        std::abort();
    }
    void Retired(void* pointer)
    {
        for(auto& entry:entries)if(entry.live && entry.pointer==pointer){entry.live=false;++retired;--live;return;}
    }
}
void* operator new(std::size_t bytes)
{
    void* pointer=std::malloc(bytes?bytes:1);
    if(!pointer)throw std::bad_alloc{}; // Test allocator only; production allocation behavior is unchanged.
    Audit::Created(pointer);return pointer;
}
void operator delete(void* pointer) noexcept { Audit::Retired(pointer);std::free(pointer); }
void operator delete(void* pointer,std::size_t) noexcept { ::operator delete(pointer); }
namespace
{
    using namespace ::GEngine;
    unsigned checks=0;
    void Check(bool valid,const char* reason)
    {++checks;if(!valid){std::println(stderr,"[FAIL] {}",reason);std::exit(1);}}
    template<class T> void Coordinates()
    {
        const Point3D<T> point({1.25f,-2.5f,3.75f});
        const float expected[]{1.25f,-2.5f,3.75f};
        const auto before=Audit::allocated;Audit::enabled=true;
        for(int axis=0;axis<3;++axis)
        {const auto value=point[axis];Check(value && *value==expected[axis],"Coordinate value/type changed");}
        for(int axis:{(std::numeric_limits<int>::min)(),-1,3,(std::numeric_limits<int>::max)()})
        {
            const auto value=point[axis];Check(!value,"Invalid coordinate did not fail");
            const auto& error=value.error();
            Check(error.code==PointCoordinateErrorCode::IndexOutOfRange && error.index==axis
                && error.operation=="Point3D::operator[]" && error.message=="Index out of range for Point3D",
                "Coordinate error lost code/index/operation/message");
        }
        const Point3D<T> nonfinite({1.f,std::numeric_limits<float>::quiet_NaN(),3.f});
        const auto payload=nonfinite[1];Check(payload && std::isnan(*payload),"Index contract changed coordinate payload semantics");
        Check(point.m_Position==Vec3f(1.25f,-2.5f,3.75f),"Const access modified point storage");
        Audit::enabled=false;Check(Audit::allocated==before,"Coordinate result/error unexpectedly allocated");
    }
    void GoldenTree()
    {
        std::vector<Point> points;
        for(int value=1;value<=7;++value)points.emplace_back(Vec3f(static_cast<float>(value)));
        std::vector<Vec3f> rectangles;rectangles.reserve(56);
        const std::array<std::array<Vec3f,4>,7> planes{{
            {{{4,1,1},{4,7,1},{4,7,7},{4,1,7}}},
            {{{1,2,1},{4,2,1},{4,2,7},{1,2,7}}},
            {{{1,1,1},{4,1,1},{4,2,1},{1,2,1}}},
            {{{1,2,3},{4,2,3},{4,7,3},{1,7,3}}},
            {{{4,6,1},{7,6,1},{7,6,7},{4,6,7}}},
            {{{4,1,5},{7,1,5},{7,6,5},{4,6,5}}},
            {{{4,6,7},{7,6,7},{7,7,7},{4,7,7}}}
        }};
        {
            KDTree tree;
            for(int cycle=0;cycle<2;++cycle)
            {
                Audit::enabled=true;tree.ConstructKDTree(points);Audit::enabled=false;
                Check(Audit::valid && Audit::live==7,"Partition temporaries leaked or tree node ownership changed");
                rectangles.clear();tree.CollectBoxes(rectangles);
                Check(rectangles.size()==56,"Seven-node rectangle payload count changed");
                std::size_t cursor=0;
                for(const auto& plane:planes)for(unsigned edge=0;edge<4;++edge)
                {
                    Check(rectangles[cursor++]==plane[edge],"Golden partition rectangle start changed");
                    Check(rectangles[cursor++]==plane[(edge+1)%4],"Golden partition rectangle end changed");
                }
                if(cycle==0)
                {
                    tree.ClearNode();Check(Audit::live==0,"Explicit clear retained nodes");
                    tree.ClearNode();Check(Audit::live==0,"Repeated clear changed retirement");
                }
            }
        }
        Check(Audit::valid && Audit::live==0 && Audit::allocated==Audit::retired,"Tree destruction did not retire every observed allocation");
        std::println("[PASS] coordinate range/payload golden-tree nodes=7 cycles=2 allocations={}/{} checks={}",
            Audit::allocated,Audit::retired,checks);
    }
}
int main(){Coordinates<float>();Coordinates<double>();GoldenTree();}
#endif
