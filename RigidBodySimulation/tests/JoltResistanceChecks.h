#pragma once
// Included only by the existing backend diagnostic route in isolated checks.
#include <bit>
#include <chrono>
#include <print>
#include <fstream>
#include <string>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/ContactConstraintManager.h>
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
#include "../../GEngine/src/Physics/InertiaValidation.h"
#include "DistanceJointChecks.h"
#include <Jolt/Physics/QualificationObservation.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <compare>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#endif
#if !defined(GENGINE_G8_RESISTANCE_PRISTINE)
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Constraints/ConstraintPart/ContactResistanceConstraintPart.h>
#if defined(GENGINE_G8_CPU_ATTRIBUTION_CHECKS)
#include <Jolt/Physics/StateRecorderImpl.h>
#endif
namespace GEngine::G8ResistanceObservation
{
    void Begin() noexcept;
    void End() noexcept;
    std::uint64_t HeapCalls() noexcept;
    std::uint64_t HeapBytes() noexcept;
    std::uint64_t TempPeakBytes() noexcept;
    std::uint64_t TempCalls() noexcept;
    std::uint64_t UpdateNanoseconds() noexcept;
    std::uint64_t UpdateCalls() noexcept;
    void SetObserver(JPH::ContactResistanceObserver*) noexcept;
}
#endif

namespace Rbs::BackendChecks
{
    namespace ResistanceChecks
    {
        inline constexpr std::uint64_t TickLimit =
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            120;
#elif defined(GENGINE_G8_RESISTANCE_PRISTINE)
            256;
#else
            1600;
#endif
        inline bool Check(bool value, const char* message)
        {
            if (!value) std::println(stderr, "RESISTANCE_CHECK_FAIL {} ticks={}", message, CompletedCheckTicks);
            return value;
        }
        inline std::vector<std::string> ExpectedZero;
        inline std::size_t ExpectedZeroIndex{};
        inline bool LoadExpectedZero()
        {
#if !defined(GENGINE_G8_RESISTANCE_PRISTINE) && defined(GENGINE_CONFIG_RELEASE)
            char* rawPath=nullptr;std::size_t pathSize=0;
            const auto pathError=_dupenv_s(&rawPath,&pathSize,"GENGINE_G8_BASELINE_TRACE");
            const std::unique_ptr<char,decltype(&std::free)> path(rawPath,&std::free);
            if(!Check(pathError==0&&path&&pathSize>1,"matched pristine trace path"))return false;
            std::ifstream input(path.get());if(!Check(bool(input),"open pristine trace"))return false;
            for(std::string line;std::getline(input,line);)
                if(line.starts_with("RESISTANCE_ZERO "))ExpectedZero.push_back(std::move(line));
            return Check(ExpectedZero.size()==21999,"complete pristine trace membership");
#else
            return true;
#endif
        }
        inline bool Advance(PhysicsSystem& system, const char* name, unsigned tick)
        {
            if (!Check(CompletedCheckTicks < TickLimit, "completed-tick budget")) return false;
            const auto before = system.GetCompletedSteps();
            const auto start = std::chrono::steady_clock::now();
            Step(system);
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            std::println("RESISTANCE_CPU case={} tick={} ns={}", name, tick, ns);
            return Check(system.GetCompletedSteps() == before+1 && !system.GetBackendFailure() && !system.HasJointFailure(), "one successful authoritative step");
        }
        inline std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
        inline bool Trace(const char* name, unsigned tick, PhysicsWorld& world, std::span<const PhysicsBodyHandle> bodies)
        {
            for (std::size_t i=0;i<bodies.size();++i)
            {
                const auto value=world.ReadBody(bodies[i]);
                if (!Check(bool(value),"zero-law authoritative readback")) return false;
                const auto& s=*value;
                const auto line=std::format("RESISTANCE_ZERO {} {} {} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {}",
                    name,tick,i,Bits(s.position.x),Bits(s.position.y),Bits(s.position.z),
                    Bits(s.orientation.w),Bits(s.orientation.x),Bits(s.orientation.y),Bits(s.orientation.z),
                    Bits(s.linearVelocity.x),Bits(s.linearVelocity.y),Bits(s.linearVelocity.z),
                    Bits(s.angularVelocity.x),Bits(s.angularVelocity.y),Bits(s.angularVelocity.z),
                    Bits(s.inverseMass),int(s.sleeping));
                std::println("{}",line);
                if(!ExpectedZero.empty() && !Check(ExpectedZeroIndex<ExpectedZero.size() && line==ExpectedZero[ExpectedZeroIndex++],"exact pristine/patched zero-law physical state"))return false;
            }
            return true;
        }
        inline bool Add(PhysicsWorld& world, std::unique_ptr<PhysicalShape>& shape, const PhysicsBodyDesc& desc, std::vector<PhysicsBodyHandle>& bodies)
        {
            const auto h=world.CreateBody(desc,shape);
            if(!Check(bool(h),"zero-law fixture body creation"))return false;
            bodies.push_back(*h);return true;
        }
        inline bool ZeroLaw()
        {
            // A bounded physics-only control uses the unchanged authored lattice
            // positions/radius/material. It is not a new visual qualification.
            PhysicsSystem system;auto* world=new PhysicsWorld;
            if(!Check(bool(world->ConfigureBackend(PhysicsBackend::Jolt)),"actual Jolt backend")){delete world;return false;}
            system.SetPhysicsWorld(world);world->SetGravity({0,-12,0});
            std::vector<PhysicsBodyHandle> bodies;bodies.reserve(181);
            auto floor=CreateCollisionShape(BoxShapeDesc{{50,.5f,50},{0,0,0}});
            if(!Check(bool(floor),"zero-law floor shape"))return false;
            PhysicsBodyDesc ground;ground.type=BodyType::Static;
            if(!Add(*world,*floor,ground,bodies))return false;
            for(int z=1;z<6;++z)for(int x=0;x<6;++x)for(int y=0;y<6;++y)
            {
                auto shape=CreateCollisionShape(SphereShapeDesc{1});
                if(!Check(bool(shape),"zero-law sphere shape"))return false;
                PhysicsBodyDesc body;body.position={float(x-1)*2,10.f+float(z-1)*2,float(y-1)*2};
                if(!Add(*world,*shape,body,bodies))return false;
            }
            if(!Check(bodies.size()==181,"180 independent lattice bodies") || !Trace("lattice",0,*world,bodies))return false;
            for(unsigned tick=1;tick<=120;++tick)
                if(!Advance(system,"lattice",tick)||!Trace("lattice",tick,*world,bodies))return false;
            // A zero floor coefficient disables each authored resistance term
            // even when the other body has positive lengths.
            PhysicsSystem one;auto* mixed=new PhysicsWorld;
            if(!Check(bool(mixed->ConfigureBackend(PhysicsBackend::Jolt)),"mixed-zero Jolt backend")){delete mixed;return false;}
            one.SetPhysicsWorld(mixed);mixed->SetGravity({0,-12,0});bodies.clear();
            floor=CreateCollisionShape(BoxShapeDesc{{10,.5f,10},{0,0,0}});
            if(!Check(bool(floor),"mixed-zero floor")||!Add(*mixed,*floor,ground,bodies))return false;
            auto shape=CreateCollisionShape(SphereShapeDesc{1});
            if(!Check(bool(shape),"mixed-zero sphere"))return false;
            PhysicsBodyDesc sphere;sphere.position={0,1.5f,0};sphere.angularVelocity={1,2,3};sphere.linearVelocity={.2f,0,0};
            sphere.material.spinResistanceLength=.05f;sphere.material.rollingResistanceLength=.05f;
            if(!Add(*mixed,*shape,sphere,bodies)||!Trace("one-zero",0,*mixed,bodies))return false;
            for(unsigned tick=1;tick<=48;++tick)
                if(!Advance(one,"one-zero",tick)||!Trace("one-zero",tick,*mixed,bodies))return false;
            std::println("RESISTANCE_ZERO_PASS ticks=168 lattice=180 pair_min_zero=1");return true;
        }
#if !defined(GENGINE_G8_RESISTANCE_PRISTINE)
        inline bool Finite(JPH::Vec3Arg value)
        {return std::isfinite(value.GetX())&&std::isfinite(value.GetY())&&std::isfinite(value.GetZ());}
        inline bool Close(JPH::Vec3Arg a,JPH::Vec3Arg b)
        {
            const double scale=(std::max)(1.,double(b.Length()));
            return Finite(a)&&Finite(b)&&double((a-b).Length())<=PhysicsInertia::FloatTolerance*scale;
        }
        inline glm::dvec3 D(JPH::Vec3Arg value) {return {value.GetX(),value.GetY(),value.GetZ()};}
        inline JPH::Mat44 Matrix(const Mat3& value)
        {
            return {JPH::Vec4(value[0].x,value[0].y,value[0].z,0),JPH::Vec4(value[1].x,value[1].y,value[1].z,0),
                JPH::Vec4(value[2].x,value[2].y,value[2].z,0),JPH::Vec4(0,0,0,1)};
        }
        struct ReferenceResult {Vec3f angular{},spin{},rolling{};glm::dvec3 accumulated{};};
        inline ReferenceResult Reference(const Mat3& inverse,Vec3f initial,Vec3f normal,float lambda,float spinLength,float rollingLength,glm::dvec3 old={})
        {
            ReferenceResult out;out.angular=initial;out.accumulated=old;
            const auto n=glm::normalize(glm::dvec3(normal));
            const Vec3f nf(n);const Vec3f response=inverse*nf;
            const double k=glm::dot(glm::dvec3(nf),glm::dvec3(response));
            const double spinCap=double(spinLength)*(std::max)(0.f,lambda);
            if(spinLength>0 && k>0)
            {
                const double next=std::clamp(old.x-glm::dot(glm::dvec3(initial),glm::dvec3(nf))/k,-spinCap,spinCap);
                const float delta=float(next-old.x);out.spin=nf*delta;out.angular+=inverse*out.spin;out.accumulated.x+=delta;
            }
            const auto absolute=glm::abs(n);
            const auto seed=absolute.x<=absolute.y&&absolute.x<=absolute.z?glm::dvec3(1,0,0):absolute.y<=absolute.z?glm::dvec3(0,1,0):glm::dvec3(0,0,1);
            const auto u=glm::normalize(glm::cross(n,seed)),v=glm::cross(n,u);const glm::dmat3 inv(inverse);
            const double a=glm::dot(u,inv*u),b=glm::dot(u,inv*v),c=glm::dot(v,inv*v);
            const glm::dvec2 rhs(a*old.y+b*old.z-glm::dot(glm::dvec3(out.angular),u),b*old.y+c*old.z-glm::dot(glm::dvec3(out.angular),v));
            const double cap=double(rollingLength)*(std::max)(0.f,lambda);glm::dvec2 next(0);
            const auto solve=[&](double alpha){const double scale=(std::max)({a,c,alpha});const double aa=a/scale+alpha/scale,bb=b/scale,cc=c/scale+alpha/scale;const auto scaled=rhs/scale;next={cc*scaled.x-bb*scaled.y,aa*scaled.y-bb*scaled.x};next/=aa*cc-bb*bb;};
            if(rollingLength>0)
            {
                if(cap>0){solve(0);if(std::hypot(next.x,next.y)>cap){double low=0,high=std::hypot(rhs.x,rhs.y)/cap;for(unsigned i=0;i<48;++i){const double mid=(low+high)*.5;solve(mid);if(std::hypot(next.x,next.y)>cap)low=mid;else high=mid;}solve(high);}}
                out.rolling=Vec3f(u*(next.x-old.y)+v*(next.y-old.z));out.angular+=inverse*out.rolling;
                out.accumulated.y+=glm::dot(u,glm::dvec3(out.rolling));out.accumulated.z+=glm::dot(v,glm::dvec3(out.rolling));
            }
            return out;
        }
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
        inline Mat3 Diagonal(const Vec3f& v)
        { Mat3 m(0);for(unsigned i=0;i<3;++i)m[i][i]=v[i];return m; }
#endif
        inline bool Numerical()
        {
            using Part=JPH::ContactResistanceConstraintPart;
            Part::Properties scalar;
            if(!Check(Part::Prepare(JPH::Mat44::sZero(),Matrix(Mat3(2.5f)),JPH::Vec3::sAxisY(),scalar),"scalar preparation"))return false;
            Part::Accumulator scalarAccumulator{};JPH::Vec3 fixed=JPH::Vec3::sZero(),moving=JPH::Vec3::sAxisY();
            auto scalarResult=Part::Solve(scalar,JPH::Vec3::sAxisY(),2,.2f,0,scalarAccumulator,fixed,moving);
            if(!Check(scalarResult.mValid&&Close(moving,JPH::Vec3::sZero())&&std::abs(scalarAccumulator.mSpin+.4)<=PhysicsInertia::FloatTolerance,"closed-form scalar spin stopping impulse"))return false;
            scalarAccumulator={};moving=JPH::Vec3::sAxisX();
            scalarResult=Part::Solve(scalar,JPH::Vec3::sAxisY(),1,0,.1f,scalarAccumulator,fixed,moving);
            if(!Check(scalarResult.mValid&&Close(moving,JPH::Vec3(.75f,0,0))&&std::abs(std::hypot(scalarAccumulator.mRollingU,scalarAccumulator.mRollingV)-.1)<=PhysicsInertia::FloatTolerance,"closed-form scalar rolling disk impulse"))return false;
            const auto rotation=glm::toMat3(glm::angleAxis(.37f,glm::normalize(Vec3f(1,2,3))));
            const Mat3 diagonal=Diagonal({2,3,5});
            for(const Mat3 inverse:{Mat3(2.5f),diagonal,rotation*diagonal*glm::transpose(rotation)})
                for(const Vec3f normal:{Vec3f(0,1,0),glm::normalize(Vec3f(1,2,3))})
                    for(const auto lengths:{Vec3f(.05f,0,0),Vec3f(0,.05f,0),Vec3f(.05f,.05f,0)})
                    {
                        Part::Properties properties;
                        if(!Check(Part::Prepare(JPH::Mat44::sZero(),Matrix(inverse),JPH::Vec3(normal.x,normal.y,normal.z),properties),"valid scalar/anisotropic preparation"))return false;
                        Part::Accumulator accumulated{};JPH::Vec3 a=JPH::Vec3::sZero(),b(.8f,1.2f,-.7f);
                        glm::dvec3 expectedAccumulator(0);
                        for(float load:{.5f,.5f,.1f,0.f})
                        {
                            const auto expected=Reference(inverse,{b.GetX(),b.GetY(),b.GetZ()},normal,load,lengths.x,lengths.y,expectedAccumulator);
                            const auto result=Part::Solve(properties,JPH::Vec3(normal.x,normal.y,normal.z),load,lengths.x,lengths.y,accumulated,a,b);
                            if(!Check(result.mValid&&result.mRootIterations<=48&&Close(b,JPH::Vec3(expected.angular.x,expected.angular.y,expected.angular.z))&&a==JPH::Vec3::sZero(),"Native accumulated spin/disk numerical equivalence"))return false;
                            expectedAccumulator=expected.accumulated;
                            const double allowance=PhysicsInertia::FloatTolerance;
                            if(!Check(std::abs(accumulated.mSpin-expectedAccumulator.x)<=allowance&&std::hypot(accumulated.mRollingU-expectedAccumulator.y,accumulated.mRollingV-expectedAccumulator.z)<=allowance,"realized Float32 accumulator"))return false;
                        }
                    }
            Part::Properties invalid;
            if(!Check(!Part::Prepare(Matrix(Diagonal({-1,2,3})),JPH::Mat44::sZero(),JPH::Vec3::sAxisY(),invalid),"indefinite response rejected"))return false;
            std::println("RESISTANCE_NUMERICAL_PASS native_reference=1 rolling_root_limit=48 actual_float_tolerance={:.17g}",PhysicsInertia::FloatTolerance);return true;
        }
        struct CopiedRow
        {
            JPH::ContactResistanceObservation value;
            JPH::Mat44 inverse1,inverse2;
            JPH::uint32 id1{},id2{};
            bool kinematic1{},kinematic2{};
        };
        class Observer final : public JPH::ContactResistanceObserver
        {
        public:
            static constexpr std::size_t Capacity=2048;
            std::unique_ptr<std::array<CopiedRow,Capacity>> rows=std::make_unique<std::array<CopiedRow,Capacity>>();
            std::atomic<std::size_t> count{};
            std::size_t discrete{},ccd{},positive{};std::uint64_t roots{};
            void Reset(){count=0;discrete=ccd=positive=0;roots=0;}
            void OnContactResistanceSolved(const JPH::Body& a,const JPH::Body& b,const JPH::ContactResistanceObservation& value) override
            {
                const auto i=count.fetch_add(1,std::memory_order_relaxed);
                if(i>=Capacity)return;
                auto& row=(*rows)[i];row.value=value;row.id1=a.GetID().GetIndexAndSequenceNumber();row.id2=b.GetID().GetIndexAndSequenceNumber();
                row.kinematic1=a.IsKinematic();row.kinematic2=b.IsKinematic();
                row.inverse1=a.IsDynamic()?a.GetInverseInertia():JPH::Mat44::sZero();
                row.inverse2=b.IsDynamic()?b.GetInverseInertia():JPH::Mat44::sZero();
            }
            bool Validate(const char* name,unsigned tick,bool singleSubstep=true)
            {
                if(!Check(count.load()<=Capacity,"bounded read-only row observer"))return false;
                // Each one-substep constraint is solved serially. Check its actual
                // accumulator chain, not only independently bounded increments.
                std::array<std::size_t,Capacity*2> last{};
                for(std::size_t i=0;i<count.load();++i)
                {
                    const auto& row=(*rows)[i];const auto& v=row.value;
                    if(singleSubstep&&!v.mIsCCD)
                    {
                        auto slot=static_cast<std::size_t>((v.mManifoldKey^(std::uint64_t(row.id1)<<32)^row.id2^(std::uint64_t(v.mPointIndex)*0x9e3779b97f4a7c15ull))&(last.size()-1));
                        while(last[slot])
                        {
                            const auto& prior=(*rows)[last[slot]-1];
                            if(prior.id1==row.id1&&prior.id2==row.id2&&prior.value.mManifoldKey==v.mManifoldKey&&prior.value.mPointIndex==v.mPointIndex)
                            {
                                if(!Check(v.mBefore[0]==prior.value.mAfter[0]&&v.mBefore[1]==prior.value.mAfter[1]&&v.mBefore[2]==prior.value.mAfter[2],"one-substep accumulated budget persists between velocity iterations"))return false;
                                break;
                            }
                            slot=(slot+1)&(last.size()-1);
                        }
                        last[slot]=i+1;
                    }
                    const double load=(std::max)(0.,double(v.mNormalLambda));
                    const double spinCap=load*v.mSpinLength,rollCap=load*v.mRollingLength;
                    const double tolerance=PhysicsInertia::FloatTolerance*(std::max)({1.,spinCap,rollCap});
                    if(!Check(std::isfinite(v.mNormalLambda)&&v.mRootIterations<=48&&Finite(v.mNormal)&&Finite(v.mSpinImpulse)&&Finite(v.mRollingImpulse),"finite authoritative load and realized impulse"))return false;
                    if(!Check(std::abs(v.mAfter[0])<=spinCap+tolerance&&std::hypot(v.mAfter[1],v.mAfter[2])<=rollCap+tolerance,"accumulated load cap, no iteration multiplier"))return false;
                    const auto expected1=(v.mAngularBefore1-row.inverse1.Multiply3x3(v.mSpinImpulse))-row.inverse1.Multiply3x3(v.mRollingImpulse);
                    const auto expected2=(v.mAngularBefore2+row.inverse2.Multiply3x3(v.mSpinImpulse))+row.inverse2.Multiply3x3(v.mRollingImpulse);
                    if(!Check(Close(expected1,v.mAngularAfter1)&&Close(expected2,v.mAngularAfter2),"equal/opposite realized angular response"))return false;
                    const auto n=glm::normalize(D(v.mNormal));const auto absolute=glm::abs(n);
                    const auto seed=absolute.x<=absolute.y&&absolute.x<=absolute.z?glm::dvec3(1,0,0):absolute.y<=absolute.z?glm::dvec3(0,1,0):glm::dvec3(0,0,1);
                    const auto u=glm::normalize(glm::cross(n,seed)),t=glm::cross(n,u);
                    if(!Check(std::abs((v.mAfter[1]-v.mBefore[1])-glm::dot(u,D(v.mRollingImpulse)))<=tolerance&&
                        std::abs((v.mAfter[2]-v.mBefore[2])-glm::dot(t,D(v.mRollingImpulse)))<=tolerance,"realized rolling impulse accounting"))return false;
                    if(v.mIsCCD)++ccd;else ++discrete;if(load>0)++positive;roots+=v.mRootIterations;
                    const auto impulse=D(v.mSpinImpulse)+D(v.mRollingImpulse);
                    const double supportWork=(row.kinematic1?glm::dot(D(v.mAngularBefore1),impulse):0)-(row.kinematic2?glm::dot(D(v.mAngularBefore2),impulse):0);
                    std::println("RESISTANCE_ROW case={} tick={} ccd={} point={} manifold={} a={} b={} lambda={:.9g} spin_length={:.9g} roll_length={:.9g} spin_before={:.17g} spin_after={:.17g} roll_before_u={:.17g} roll_before_v={:.17g} roll_after_u={:.17g} roll_after_v={:.17g} root_iterations={} kinematic_work={:.17g}",
                        name,tick,int(v.mIsCCD),v.mPointIndex,v.mManifoldKey,row.id1,row.id2,v.mNormalLambda,v.mSpinLength,v.mRollingLength,v.mBefore[0],v.mAfter[0],v.mBefore[1],v.mBefore[2],v.mAfter[1],v.mAfter[2],v.mRootIterations,supportWork);
                }
                return true;
            }
        };
        struct LayerMap final : JPH::BroadPhaseLayerInterface
        {
            JPH::uint GetNumBroadPhaseLayers() const override {return 2;}
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {return layer.GetValue()==0?"Static":"Dynamic";}
#endif
        };
        struct BroadFilter final : JPH::ObjectVsBroadPhaseLayerFilter
        {bool ShouldCollide(JPH::ObjectLayer a,JPH::BroadPhaseLayer b)const override{return a!=0||b.GetValue()!=0;}};
        struct PairFilter final : JPH::ObjectLayerPairFilter
        {bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b)const override{return a!=0||b!=0;}};
        struct Listener final : JPH::ContactListener
        {
            Observer* observer{};float length{};std::atomic<unsigned> candidates{};
            void Configure(JPH::ContactSettings& settings)
            {settings.mCombinedSpinResistanceLength=length;settings.mCombinedRollingResistanceLength=length;settings.mContactResistanceObserver=observer;}
            void OnContactAdded(const JPH::Body&,const JPH::Body&,const JPH::ContactManifold&,JPH::ContactSettings& settings)override{++candidates;Configure(settings);}
            void OnContactPersisted(const JPH::Body&,const JPH::Body&,const JPH::ContactManifold&,JPH::ContactSettings& settings)override{++candidates;Configure(settings);}
        };
        inline std::atomic<std::uint64_t> AllocCalls{},AllocBytes{};
#if !defined(JPH_GENGINE_PHASE26_OBSERVATION)
        inline std::atomic<bool> AllocEnabled{};
        inline JPH::AllocateFunction OldAllocate{};
        inline JPH::ReallocateFunction OldReallocate{};
        inline JPH::AlignedAllocateFunction OldAligned{};
        struct AllocationScope
        {
            AllocationScope()
            {
                AllocCalls=0;AllocBytes=0;OldAllocate=JPH::Allocate;OldReallocate=JPH::Reallocate;OldAligned=JPH::AlignedAllocate;
                JPH::Allocate=[](std::size_t n)->void*{if(AllocEnabled){++AllocCalls;AllocBytes+=n;}return OldAllocate(n);};
                JPH::Reallocate=[](void* v,std::size_t old,std::size_t n)->void*{if(AllocEnabled){++AllocCalls;AllocBytes+=n;}return OldReallocate(v,old,n);};
                JPH::AlignedAllocate=[](std::size_t n,std::size_t alignment)->void*{if(AllocEnabled){++AllocCalls;AllocBytes+=n;}return OldAligned(n,alignment);};
            }
            void Begin(){AllocCalls=0;AllocBytes=0;AllocEnabled=true;}
            void End(){AllocEnabled=false;}
            ~AllocationScope(){JPH::Allocate=OldAllocate;JPH::Reallocate=OldReallocate;JPH::AlignedAllocate=OldAligned;}
        };
#endif
        struct MeasuredTemp final : JPH::TempAllocator
        {
            JPH::TempAllocatorImpl storage{32*1024*1024};
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            std::atomic<std::size_t> peak{};
#else
            std::size_t peak{};
#endif
            void* Allocate(JPH::uint n)override
            {
                auto* result=storage.Allocate(n);
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
                const auto usage=storage.GetUsage();auto previous=peak.load(std::memory_order_relaxed);
                while(previous<usage&&!peak.compare_exchange_weak(previous,usage,std::memory_order_relaxed));
#else
                peak=(std::max)(peak,storage.GetUsage());
#endif
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
                if(JPH::QualificationObservation::GetSink())
                {
                    namespace Q=JPH::QualificationObservation;
                    Q::StorageRecord record{};record.context=Q::CurrentContext();record.context.site=Q::ArenaSite::Unknown;
                    record.pointer=reinterpret_cast<std::uintptr_t>(result);record.requested_bytes=n;
                    record.aligned_bytes=(std::uint64_t(n)+JPH_RVECTOR_ALIGNMENT-1)&~std::uint64_t(JPH_RVECTOR_ALIGNMENT-1);
                    record.arena_usage_after=usage;record.count=1;record.action=Q::StorageAction::Reserve;
                    Q::PublishSynthetic(record.context,Q::EventKind::Storage,&record,sizeof(record));
                }
#endif
                return result;
            }
            void Free(void* value,JPH::uint n)override
            {
                storage.Free(value,n);
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
                if(JPH::QualificationObservation::GetSink())
                {
                    namespace Q=JPH::QualificationObservation;
                    Q::StorageRecord record{};record.context=Q::CurrentContext();record.context.site=Q::ArenaSite::Unknown;
                    record.pointer=reinterpret_cast<std::uintptr_t>(value);record.requested_bytes=n;
                    record.aligned_bytes=(std::uint64_t(n)+JPH_RVECTOR_ALIGNMENT-1)&~std::uint64_t(JPH_RVECTOR_ALIGNMENT-1);
                    record.arena_usage_after=storage.GetUsage();record.count=1;record.action=Q::StorageAction::Release;
                    Q::PublishSynthetic(record.context,Q::EventKind::Storage,&record,sizeof(record));
                }
#endif
            }
        };
        struct DirectMetrics {std::uint64_t heap{},bytes{},ns{},roots{};std::size_t peak{},discrete{},ccd{},positive{};};
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
        struct PhysicalState
        {
            JPH::uint32 id{},motion{},active{};
            std::array<std::uint64_t,3> center{};
            std::array<std::uint32_t,10> motionBits{}; // quaternion, linear and angular velocity
            bool operator==(const PhysicalState&) const = default;
        };
        struct ImpulseState
        {
            std::uint64_t manifold{};
            JPH::uint32 body1{},body2{},point{},ccd{},roots{};
            std::array<std::uint32_t,12> floats{}; // load, lengths, spin, rolling, normal
            std::array<std::uint64_t,6> accumulators{}; // exact double before and after
            auto operator<=>(const ImpulseState&) const = default;
        };
        static_assert(sizeof(ImpulseState)<=128);
        template<std::size_t Rows,std::size_t Bodies> struct FixedFrame
        {
            std::array<PhysicalState,Bodies> bodies{};
            std::array<ImpulseState,Rows> impulses{};
            std::array<std::uint32_t,8> jointLambda{},jointActive{};
            std::uint32_t bodyCount{},impulseCount{},jointCount{};
        };
        struct DirectParity
        {
            std::array<FixedFrame<256,3>,2> step{};
        };
        inline bool ObservationDetachOkay=true;
        struct ParityFrameHeader
        {
            std::uint64_t magic{0x3159503632504850ull}; // PHP26PY1, little endian
            std::uint64_t epoch{};
            std::uint32_t schema{1},fixture{},caseId{},arm{},update{},substeps{};
            std::uint32_t bodyCount{},impulseCount{},jointCount{};
        };
        struct SyntheticFrameHeader
        {
            std::uint64_t magic{0x3159533632504850ull}; // PHP26SY1, little endian
            std::uint32_t schema{1},caseId{},update{},workerThreads{};
            std::uint32_t valid{},overflow{},invalid{},pointCount{},workCount{},failureCount{};
        };
        static_assert(std::is_trivially_copyable_v<ParityFrameHeader> &&
            std::is_trivially_copyable_v<SyntheticFrameHeader>);
        struct ObservationRawExport
        {
            FILE* events{},*parity{},*synthetic{};
            struct OffReference
            {
                std::uint64_t offset{};
                std::uint32_t fixture{},caseId{},update{},substeps{};
            };
            std::array<OffReference,48> offFrames{};
            std::uint32_t offFrameCount{};
            std::uint64_t eventBytes{},parityBytes{},syntheticBytes{},epoch{};
            std::uint32_t updateFrames{},parityFrames{},syntheticFrames{};
            std::uint32_t lifetimeExported{},storageExported{},heapExported{},failureExported{},identityExported{};
            bool hasUpdate{},valid{true};
            static constexpr std::uint64_t EventCap=48ull*1024*1024;
            static constexpr std::uint64_t ParityCap=14ull*1024*1024;
            static constexpr std::uint64_t SyntheticCap=1ull*1024*1024;
            static FILE* Create(const std::filesystem::path& path,bool readback=false)
            {
                int fd=-1;
                if(_sopen_s(&fd,path.string().c_str(),(readback?_O_RDWR:_O_WRONLY)|_O_CREAT|_O_EXCL|_O_BINARY,
                    _SH_DENYWR,_S_IREAD|_S_IWRITE)!=0 || fd<0)return nullptr;
                FILE* file=_fdopen(fd,readback?"w+b":"wb");
                if(!file){_close(fd);return nullptr;}
                std::setvbuf(file,nullptr,_IONBF,0);
                return file;
            }
            bool Open()
            {
                char* raw=nullptr;std::size_t size=0;
                const auto error=_dupenv_s(&raw,&size,"GENGINE_PHASE26_OBSERVATION_OUTPUT");
                const std::unique_ptr<char,decltype(&std::free)> root(raw,&std::free);
                if(error || !root || size<2)return valid=false;
                const std::filesystem::path directory(root.get());
                events=Create(directory/"observer.bin");
                parity=Create(directory/"parity.bin",true);
                synthetic=Create(directory/"synthetic.bin");
                return valid=events&&parity&&synthetic;
            }
            static bool Write(FILE* file,const void* bytes,std::size_t stride,std::size_t count) noexcept
            {return count==0 || (file && std::fwrite(bytes,stride,count,file)==count);}
            void BeginEpoch(std::uint64_t value) noexcept
            {
                epoch=value;hasUpdate=false;
                lifetimeExported=storageExported=heapExported=failureExported=identityExported=0;
            }
            bool InstallEpoch(std::uint64_t value) noexcept
            {
                if(!valid || epoch || !value)return valid=false;
                auto& recorder=Phase26JoltRecorder::Get();
                if(!ObservationDetachOkay || !recorder.Valid() || recorder.heapLiveBytes ||
                    recorder.installed || JPH::QualificationObservation::GetSink()!=nullptr)return valid=false;
                recorder.ResetUpdate();
                BeginEpoch(value);
                return recorder.InstallLifetime(value);
            }
            void ResetDeltas() noexcept
            {lifetimeExported=storageExported=heapExported=failureExported=identityExported=0;}
            bool Frame(::Rbs::Phase26::JoltFrameKind kind,std::uint64_t value,
                std::uint32_t update=0,std::uint32_t substeps=0) noexcept
            {
                if(!valid || !events || epoch!=value)return valid=false;
                auto& r=Phase26JoltRecorder::Get();
                const bool live=kind==::Rbs::Phase26::JoltFrameKind::Update;
                const auto lifetime=r.lifetimeCount.load(),storage=r.storageCount.load(),heap=r.heapCount.load();
                const auto failures=r.failureCount.load(),identities=r.identityCount.load();
                if(!live && (lifetime<lifetimeExported || storage<storageExported || heap<heapExported ||
                    failures<failureExported || identities<identityExported))return valid=false;
                ::Rbs::Phase26::JoltFrameHeader h;
                h.epoch=value;h.update=live?update:0;h.substeps=live?substeps:0;
                h.kind=static_cast<std::uint32_t>(kind);h.workerThreads=r.WorkerCount();h.valid=r.Valid();
                h.pointCount=live?r.pointCount.load():0;h.ccdCount=live?r.ccdCount.load():0;
                h.lifetimeCount=live?lifetime:lifetime-lifetimeExported;
                h.workCount=live?r.workCount.load():0;h.phaseCount=live?r.phaseCount.load():0;
                h.storageCount=live?storage:storage-storageExported;
                h.heapCount=live?heap:heap-heapExported;
                h.failureCount=live?failures:failures-failureExported;
                h.identityCount=live?identities:identities-identityExported;
                if(kind==::Rbs::Phase26::JoltFrameKind::Interstice && !h.lifetimeCount&&
                    !h.storageCount&&!h.heapCount&&!h.failureCount&&!h.identityCount)return true;
                if(h.pointCount>r.points.size()||h.ccdCount>r.ccd.size()||
                    h.lifetimeCount>r.lifetimes.size()||h.workCount>r.work.size()||
                    h.phaseCount>r.phases.size()||h.storageCount>r.storage.size()||
                    h.heapCount>r.heap.size()||h.failureCount>r.failures.size()||
                    h.identityCount>r.identities.size())return valid=false;
                const std::uint64_t extra=sizeof(h)+
                    h.pointCount*(sizeof(r.points[0])+sizeof(r.pointKind[0])+sizeof(r.pointWorker[0]))+
                    h.ccdCount*(sizeof(r.ccd[0])+sizeof(r.ccdKind[0])+sizeof(r.ccdWorker[0]))+
                    h.lifetimeCount*(sizeof(r.lifetimes[0])+sizeof(r.lifetimeWorker[0]))+
                    h.workCount*(sizeof(r.work[0])+sizeof(r.workWorker[0]))+
                    h.phaseCount*(sizeof(r.phases[0])+sizeof(r.phaseWorker[0]))+
                    h.storageCount*(sizeof(r.storage[0])+sizeof(r.storageWorker[0]))+
                    h.heapCount*(sizeof(r.heap[0])+sizeof(r.heapWorker[0]))+
                    h.failureCount*(sizeof(r.failures[0])+sizeof(r.failureWorker[0]))+
                    h.identityCount*(sizeof(r.identities[0])+sizeof(r.identityWorker[0]));
                if(eventBytes+extra>EventCap)return valid=false;
                const auto lo=live?0:lifetimeExported,so=live?0:storageExported,
                    ho=live?0:heapExported,fo=live?0:failureExported,io=live?0:identityExported;
                const bool okay=Write(events,&h,sizeof(h),1)&&
                    Write(events,r.points.data(),sizeof(r.points[0]),h.pointCount)&&
                    Write(events,r.pointKind.data(),sizeof(r.pointKind[0]),h.pointCount)&&
                    Write(events,r.pointWorker.data(),sizeof(r.pointWorker[0]),h.pointCount)&&
                    Write(events,r.ccd.data(),sizeof(r.ccd[0]),h.ccdCount)&&
                    Write(events,r.ccdKind.data(),sizeof(r.ccdKind[0]),h.ccdCount)&&
                    Write(events,r.ccdWorker.data(),sizeof(r.ccdWorker[0]),h.ccdCount)&&
                    Write(events,r.lifetimes.data()+lo,sizeof(r.lifetimes[0]),h.lifetimeCount)&&
                    Write(events,r.lifetimeWorker.data()+lo,sizeof(r.lifetimeWorker[0]),h.lifetimeCount)&&
                    Write(events,r.work.data(),sizeof(r.work[0]),h.workCount)&&
                    Write(events,r.workWorker.data(),sizeof(r.workWorker[0]),h.workCount)&&
                    Write(events,r.phases.data(),sizeof(r.phases[0]),h.phaseCount)&&
                    Write(events,r.phaseWorker.data(),sizeof(r.phaseWorker[0]),h.phaseCount)&&
                    Write(events,r.storage.data()+so,sizeof(r.storage[0]),h.storageCount)&&
                    Write(events,r.storageWorker.data()+so,sizeof(r.storageWorker[0]),h.storageCount)&&
                    Write(events,r.heap.data()+ho,sizeof(r.heap[0]),h.heapCount)&&
                    Write(events,r.heapWorker.data()+ho,sizeof(r.heapWorker[0]),h.heapCount)&&
                    Write(events,r.failures.data()+fo,sizeof(r.failures[0]),h.failureCount)&&
                    Write(events,r.failureWorker.data()+fo,sizeof(r.failureWorker[0]),h.failureCount)&&
                    Write(events,r.identities.data()+io,sizeof(r.identities[0]),h.identityCount)&&
                    Write(events,r.identityWorker.data()+io,sizeof(r.identityWorker[0]),h.identityCount);
                if(!okay)return valid=false;
                eventBytes+=extra;
                lifetimeExported=lifetime;storageExported=storage;heapExported=heap;
                failureExported=failures;identityExported=identities;
                if(live){hasUpdate=true;++updateFrames;}
                return true;
            }
            bool BeforeUpdate(std::uint64_t value) noexcept
            {
                if(epoch!=value)return valid=false;
                return Frame(hasUpdate?::Rbs::Phase26::JoltFrameKind::Interstice:
                    ::Rbs::Phase26::JoltFrameKind::Construction,value);
            }
            bool AfterUpdate(std::uint64_t value,std::uint32_t update,std::uint32_t substeps) noexcept
            {return Frame(::Rbs::Phase26::JoltFrameKind::Update,value,update,substeps);}
            bool EndEpoch(std::uint64_t value) noexcept
            {
                if(epoch!=value)return valid=false;
                const bool okay=Frame(::Rbs::Phase26::JoltFrameKind::Teardown,value);
                epoch=0;return okay;
            }
            template<std::size_t Rows,std::size_t Bodies>
            bool Parity(std::uint32_t fixture,std::uint32_t caseId,bool arm,std::uint64_t value,
                std::uint32_t update,std::uint32_t substeps,const FixedFrame<Rows,Bodies>& frame) noexcept
            {
                if(!valid||!parity||frame.bodyCount>Bodies||frame.impulseCount>Rows||
                    frame.jointCount>frame.jointLambda.size())return valid=false;
                ParityFrameHeader h;h.epoch=value;h.fixture=fixture;h.caseId=caseId;
                h.arm=arm?1:0;h.update=update;h.substeps=substeps;
                h.bodyCount=frame.bodyCount;h.impulseCount=frame.impulseCount;h.jointCount=frame.jointCount;
                const std::uint64_t extra=sizeof(h)+h.bodyCount*sizeof(PhysicalState)+
                    h.impulseCount*sizeof(ImpulseState)+h.jointCount*2*sizeof(std::uint32_t);
                if(parityBytes+extra>ParityCap)return valid=false;
                if(!Write(parity,&h,sizeof(h),1)||
                    !Write(parity,frame.bodies.data(),sizeof(PhysicalState),h.bodyCount)||
                    !Write(parity,frame.impulses.data(),sizeof(ImpulseState),h.impulseCount)||
                    !Write(parity,frame.jointLambda.data(),sizeof(std::uint32_t),h.jointCount)||
                    !Write(parity,frame.jointActive.data(),sizeof(std::uint32_t),h.jointCount))return valid=false;
                if(!arm)
                {
                    if(offFrameCount==offFrames.size())return valid=false;
                    offFrames[offFrameCount++]={parityBytes,fixture,caseId,update,substeps};
                }
                else
                {
                    const OffReference* matched=nullptr;
                    for(std::uint32_t i=0;i<offFrameCount;++i)
                        if(offFrames[i].fixture==fixture&&offFrames[i].caseId==caseId&&
                            offFrames[i].update==update&&offFrames[i].substeps==substeps)
                        {matched=&offFrames[i];break;}
                    if(!matched||std::fseek(parity,static_cast<long>(matched->offset),SEEK_SET)!=0)return valid=false;
                    ParityFrameHeader previous;
                    if(std::fread(&previous,sizeof(previous),1,parity)!=1||previous.arm!=0||
                        previous.fixture!=fixture||previous.caseId!=caseId||previous.update!=update||
                        previous.substeps!=substeps||previous.bodyCount!=h.bodyCount||
                        previous.impulseCount!=h.impulseCount||previous.jointCount!=h.jointCount)return valid=false;
                    for(std::uint32_t i=0;i<h.bodyCount;++i)
                    {PhysicalState prior{};if(std::fread(&prior,sizeof(prior),1,parity)!=1||
                        !(prior==frame.bodies[i]))return valid=false;}
                    for(std::uint32_t i=0;i<h.impulseCount;++i)
                    {ImpulseState prior{};if(std::fread(&prior,sizeof(prior),1,parity)!=1||
                        !(prior==frame.impulses[i]))return valid=false;}
                    for(std::uint32_t i=0;i<h.jointCount;++i)
                    {std::uint32_t prior{};if(std::fread(&prior,sizeof(prior),1,parity)!=1||
                        prior!=frame.jointLambda[i])return valid=false;}
                    for(std::uint32_t i=0;i<h.jointCount;++i)
                    {std::uint32_t prior{};if(std::fread(&prior,sizeof(prior),1,parity)!=1||
                        prior!=frame.jointActive[i])return valid=false;}
                    if(std::fseek(parity,static_cast<long>(parityBytes+extra),SEEK_SET)!=0)return valid=false;
                }
                parityBytes+=extra;++parityFrames;return true;
            }
            bool Synthetic(std::uint32_t caseId,std::uint32_t update) noexcept
            {
                if(!valid||!synthetic)return valid=false;
                const auto& r=Phase26JoltRecorder::Get();
                SyntheticFrameHeader h;h.caseId=caseId;h.update=update;
                h.workerThreads=r.WorkerCount();h.valid=r.Valid();
                h.overflow=r.overflow.load();h.invalid=r.invalid.load();
                h.pointCount=r.pointCount.load();h.workCount=r.workCount.load();
                h.failureCount=r.failureCount.load();
                if(h.workCount>r.work.size()||h.failureCount>r.failures.size())return valid=false;
                const std::uint64_t extra=sizeof(h)+
                    h.workCount*(sizeof(r.work[0])+sizeof(r.workWorker[0]))+
                    h.failureCount*(sizeof(r.failures[0])+sizeof(r.failureWorker[0]));
                if(syntheticBytes+extra>SyntheticCap)return valid=false;
                if(!Write(synthetic,&h,sizeof(h),1)||
                    !Write(synthetic,r.work.data(),sizeof(r.work[0]),h.workCount)||
                    !Write(synthetic,r.workWorker.data(),sizeof(r.workWorker[0]),h.workCount)||
                    !Write(synthetic,r.failures.data(),sizeof(r.failures[0]),h.failureCount)||
                    !Write(synthetic,r.failureWorker.data(),sizeof(r.failureWorker[0]),h.failureCount))return valid=false;
                syntheticBytes+=extra;++syntheticFrames;return true;
            }
            bool Close() noexcept
            {
                if(events){valid &= std::fclose(events)==0;events=nullptr;}
                if(parity){valid &= std::fclose(parity)==0;parity=nullptr;}
                if(synthetic){valid &= std::fclose(synthetic)==0;synthetic=nullptr;}
                valid &= eventBytes+parityBytes+syntheticBytes<64ull*1024*1024;
                return valid;
            }
        };
        inline ObservationRawExport ObservationRaw{};
        struct ObservationTotals
        {
            std::uint64_t points{},ccd{},retired{},work{},storage{},heap{};
            std::size_t arenaPeak{};
            void Add(const Phase26JoltRecorder::Recorder& r,std::size_t peak)
            {
                points+=r.pointCount.load();ccd+=r.ccdCount.load();retired+=r.lifetimeCount.load();
                work+=r.workCount.load();storage+=r.storageCount.load();heap+=r.heapCount.load();
                arenaPeak=(std::max)(arenaPeak,peak);
            }
        };
        inline ObservationTotals ObservationActual{};
        template<class T,std::size_t Rows,std::size_t Bodies>
        inline bool CaptureImpulses(const T& observed,FixedFrame<Rows,Bodies>& frame)
        {
            if(!Check(observed.count.load()<=Rows,"bounded original impulse parity rows"))return false;
            frame.impulseCount=static_cast<std::uint32_t>(observed.count.load());
            for(std::size_t i=0;i<observed.count.load();++i)
            {
                const auto& row=(*observed.rows)[i];const auto& v=row.value;
                auto& copy=frame.impulses[i];copy.manifold=v.mManifoldKey;
                copy.body1=row.id1;copy.body2=row.id2;copy.point=v.mPointIndex;
                copy.ccd=JPH::uint32(v.mIsCCD);copy.roots=v.mRootIterations;
                std::size_t n=0;copy.floats[n++]=Bits(v.mNormalLambda);
                copy.floats[n++]=Bits(v.mSpinLength);copy.floats[n++]=Bits(v.mRollingLength);
                for(const auto value:{v.mSpinImpulse.GetX(),v.mSpinImpulse.GetY(),v.mSpinImpulse.GetZ(),
                    v.mRollingImpulse.GetX(),v.mRollingImpulse.GetY(),v.mRollingImpulse.GetZ(),
                    v.mNormal.GetX(),v.mNormal.GetY(),v.mNormal.GetZ()})copy.floats[n++]=Bits(value);
                for(unsigned k=0;k<3;++k)
                {copy.accumulators[k]=std::bit_cast<std::uint64_t>(v.mBefore[k]);
                    copy.accumulators[k+3]=std::bit_cast<std::uint64_t>(v.mAfter[k]);}
            }
            std::sort(frame.impulses.begin(),frame.impulses.begin()+frame.impulseCount);
            return true;
        }
        template<std::size_t Rows,std::size_t Bodies,std::size_t Known>
        inline bool CapturePhysical(JPH::PhysicsSystem& physics,const std::array<JPH::BodyID,Known>& ids,
            FixedFrame<Rows,Bodies>& frame)
        {
            frame.bodyCount=0;
            for(const auto id:ids)
            {
                if(id.IsInvalid())continue;
                if(!Check(frame.bodyCount<Bodies,"bounded authoritative physical state"))return false;
                JPH::BodyLockRead lock(physics.GetBodyLockInterface(),id);
                if(!Check(lock.Succeeded(),"known fixture BodyID readback"))return false;
                const auto& body=lock.GetBody();auto& state=frame.bodies[frame.bodyCount++];
                state.id=id.GetIndexAndSequenceNumber();state.motion=JPH::uint32(body.GetMotionType());
                state.active=JPH::uint32(body.IsActive());
                const auto position=body.GetCenterOfMassPosition();
                const auto rotation=body.GetRotation();
                const auto linear=body.GetLinearVelocity(),angular=body.GetAngularVelocity();
                state.center={std::bit_cast<std::uint64_t>(double(position.GetX())),
                    std::bit_cast<std::uint64_t>(double(position.GetY())),
                    std::bit_cast<std::uint64_t>(double(position.GetZ()))};
                state.motionBits={Bits(rotation.GetW()),Bits(rotation.GetX()),Bits(rotation.GetY()),Bits(rotation.GetZ()),
                    Bits(linear.GetX()),Bits(linear.GetY()),Bits(linear.GetZ()),
                    Bits(angular.GetX()),Bits(angular.GetY()),Bits(angular.GetZ())};
            }
            return true;
        }
        template<std::size_t Rows,std::size_t Bodies>
        inline bool SameFixedFrame(const FixedFrame<Rows,Bodies>& a,const FixedFrame<Rows,Bodies>& b)
        {
            if(!Check(a.bodyCount==b.bodyCount&&a.impulseCount==b.impulseCount&&
                a.jointCount==b.jointCount,"off/on physical, impulse and joint membership"))return false;
            for(unsigned i=0;i<a.bodyCount;++i)if(!Check(a.bodies[i]==b.bodies[i],
                "off/on exact authoritative body state and activation"))return false;
            for(unsigned i=0;i<a.impulseCount;++i)if(!Check(a.impulses[i]==b.impulses[i],
                "off/on exact original resistance impulse"))return false;
            for(unsigned i=0;i<a.jointCount;++i)
                if(!Check(a.jointLambda[i]==b.jointLambda[i]&&a.jointActive[i]==b.jointActive[i],
                    "off/on exact distance-joint retained state"))return false;
            return true;
        }
        inline bool CaptureDirectParity(JPH::PhysicsSystem& physics,const Observer& observed,
            const std::array<JPH::BodyID,3>& ids,DirectParity& parity,unsigned step)
        {
            if(!Check(step<parity.step.size(),"direct parity step range"))return false;
            return CapturePhysical(physics,ids,parity.step[step])&&CaptureImpulses(observed,parity.step[step]);
        }
        inline bool SameDirectParity(const DirectParity& a,const DirectParity& b)
        {
            for(std::size_t step=0;step<a.step.size();++step)
                if(!SameFixedFrame(a.step[step],b.step[step]))return false;
            return true;
        }
        inline bool ObservationContext(const JPH::QualificationObservation::Context& context,
            const JPH::PhysicsSystem& world,std::uint64_t epoch,unsigned update,int substeps)
        {
            return Check(context.world==&world&&context.epoch==epoch&&context.update==update&&
                context.substep<static_cast<JPH::uint32>(substeps),"exact SDK world/epoch/update/substep context");
        }
        inline bool ValidateArenaStream(const Phase26JoltRecorder::Recorder& r,std::size_t peak)
        {
            namespace Q=JPH::QualificationObservation;
            std::uint64_t reserved=0,released=0,observedPeak=0;
            for(unsigned i=0;i<r.storageCount.load();++i)
            {
                const auto& event=r.storage[i];
                if(event.context.site!=Q::ArenaSite::Unknown)continue;
                if(event.action==Q::StorageAction::Reserve)reserved+=event.aligned_bytes;
                else if(event.action==Q::StorageAction::Release)released+=event.aligned_bytes;
                else return Check(false,"actual temp arena stream has only reserve/release");
                observedPeak=(std::max)(observedPeak,event.arena_usage_after);
            }
            return Check(reserved==released&&observedPeak==peak&&peak<=32*1024*1024,
                "exact bounded 32MiB arena requests/releases/peak");
        }
        inline bool ObservationFinite(const float (&v)[3])
        {return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);}
        inline bool ValidateDiscreteGeometry(JPH::PhysicsSystem& world,
            const JPH::QualificationObservation::PointRecord& p)
        {
            const std::array<JPH::BodyID,2> ids{JPH::BodyID(p.body1),JPH::BodyID(p.body2)};
            JPH::BodyLockMultiRead locked(world.GetBodyLockInterface(),ids.data(),int(ids.size()));
            const auto* a=locked.GetBody(0),*b=locked.GetBody(1);
            if(!Check(a&&b,"discrete witness full BodyIDs remain live"))return false;
            const auto pa=a->GetCenterOfMassPosition()+a->GetRotation()*
                JPH::Vec3(p.anchor1[0],p.anchor1[1],p.anchor1[2]);
            const auto pb=b->GetCenterOfMassPosition()+b->GetRotation()*
                JPH::Vec3(p.anchor2[0],p.anchor2[1],p.anchor2[2]);
            const double depth=(double(pa.GetX())-double(pb.GetX()))*p.solver_world_normal[0]+
                (double(pa.GetY())-double(pb.GetY()))*p.solver_world_normal[1]+
                (double(pa.GetZ())-double(pb.GetZ()))*p.solver_world_normal[2];
            const double normalLength=std::hypot(double(p.solver_world_normal[0]),
                double(p.solver_world_normal[1]),double(p.solver_world_normal[2]));
            return Check(std::isfinite(depth)&&normalLength>.9&&normalLength<1.1,
                "finite signed solver-normal depth from completed COM/rotation anchors");
        }
        inline bool ValidateDirectObservation(const char* name,JPH::PhysicsSystem& world,
            const Observer& old,JPH::BodyID floor,JPH::BodyID body,JPH::BodyID later,
            std::uint64_t epoch,unsigned update,int substeps,float length,unsigned kind)
        {
            namespace Q=JPH::QualificationObservation;
            const auto& r=Phase26JoltRecorder::Get();
            if(!Check(r.Valid()&&r.failureCount.load()==0,"direct observer no context/capacity failures"))return false;
            std::size_t positivePoints=0,accepted=0,candidates=0,manifold=0;
            for(unsigned i=0;i<r.pointCount.load();++i)
            {
                const auto& p=r.points[i];
                const auto kindOfPoint=r.pointKind[i];
                if(!ObservationContext(p.context,world,epoch,update,substeps)||
                    !Check((kindOfPoint==Q::EventKind::Point&&p.space==Q::AnchorSpace::COMLocal)||
                        (kindOfPoint==Q::EventKind::CCDManifoldPoint&&
                            (p.space==Q::AnchorSpace::WorldAtImpact||
                             (p.space==Q::AnchorSpace::COMLocal&&p.ordinal==JPH::uint32(-1)))),
                        "direct typed contact anchor space" )||
                    !Check(p.body1!=p.body2&&
                        ObservationFinite(p.anchor1)&&ObservationFinite(p.anchor2)&&
                        ObservationFinite(p.cached_local_normal)&&ObservationFinite(p.solver_world_normal)&&
                        std::isfinite(p.normal_lambda),"direct complete discrete solved witness"))return false;
                if(kindOfPoint==Q::EventKind::Point)positivePoints+=p.normal_lambda>0;
                else ++manifold;
                if(kindOfPoint==Q::EventKind::Point&&update==1&&
                    !ValidateDiscreteGeometry(world,p))return false;
                if(update==1)
                {
                    const auto a=p.body1,b=p.body2;
                    if(!Check((a==floor.GetIndexAndSequenceNumber()||b==floor.GetIndexAndSequenceNumber()||
                        (!later.IsInvalid()&&(a==later.GetIndexAndSequenceNumber()||b==later.GetIndexAndSequenceNumber())))&&
                        (a==body.GetIndexAndSequenceNumber()||b==body.GetIndexAndSequenceNumber()),
                        "direct full BodyID witness pair"))return false;
                }
                else if(!Check(p.body1!=body.GetIndexAndSequenceNumber()&&p.body2!=body.GetIndexAndSequenceNumber(),
                    "retired direct BodyID has no live solved witness"))return false;
            }
            for(unsigned i=0;i<r.ccdCount.load();++i)
            {
                const auto& c=r.ccd[i];
                if(!ObservationContext(c.context,world,epoch,update,substeps)||
                    !Check(std::isfinite(c.fraction)&&std::isfinite(c.fraction_plus_slop)&&
                        ObservationFinite(c.response_point)&&ObservationFinite(c.solver_world_normal)&&
                        std::isfinite(c.normal_lambda),"direct finite CCD candidate/acceptance"))return false;
                switch(r.ccdKind[i])
                {
                case Q::EventKind::CCDCandidate:++candidates;break;
                case Q::EventKind::CCDAccepted:++accepted;break;
                default:return Check(false,"direct typed CCD event kind");
                }
                if(update==2&&!Check(c.body1!=body.GetIndexAndSequenceNumber()&&
                    c.body2!=body.GetIndexAndSequenceNumber(),"retired direct BodyID has no CCD response"))return false;
            }
            for(unsigned i=0;i<r.lifetimeCount.load();++i)
                if(!ObservationContext(r.lifetimes[i].context,world,epoch,update,substeps))return false;
            for(unsigned i=0;i<r.workCount.load();++i)
                if(!ObservationContext(r.work[i].context,world,epoch,update,substeps))return false;
            if(update==1)
            {
                if(kind==6&&!Check(r.pointCount.load()==0&&accepted==0&&old.count.load()==0,
                    "sensor has no solved witness or original resistance row"))return false;
                if(kind==5&&!Check(positivePoints==0,"speculative separating witness has zero solved load"))return false;
                if(kind==3&&length>0&&!Check(candidates>0&&accepted>0&&manifold>0,
                    "earliest CCD has prepared candidate, manifold and accepted response"))return false;
                if(length>0&&kind<5&&!Check(positivePoints>0||accepted>0,
                    "direct positive resistance fixture has actual solved witness"))return false;
                for(unsigned i=0;i<old.count.load();++i)
                {
                    const auto& row=(*old.rows)[i];const auto& v=row.value;
                    bool found=false;
                    if(v.mIsCCD)
                    {
                        for(unsigned j=0;j<r.ccdCount.load();++j)
                            if(r.ccdKind[j]==Q::EventKind::CCDAccepted&&
                                ((r.ccd[j].body1==row.id1&&r.ccd[j].body2==row.id2)||
                                 (r.ccd[j].body1==row.id2&&r.ccd[j].body2==row.id1)))found=true;
                    }
                    else for(unsigned j=0;j<r.pointCount.load();++j)
                        if(r.pointKind[j]==Q::EventKind::Point&&r.points[j].ordinal==v.mPointIndex&&
                            ((r.points[j].body1==row.id1&&r.points[j].body2==row.id2)||
                             (r.points[j].body1==row.id2&&r.points[j].body2==row.id1)))found=true;
                    if(!Check(found,"original resistance impulse maps to actual solved point/accepted CCD"))return false;
                }
            }
            std::println("PHASE26_OBSERVER_DIRECT case={} update={} points={} positive={} ccd_candidates={} ccd_manifold={} ccd_accepted={} retirement={}",
                name,update,r.pointCount.load(),positivePoints,candidates,manifold,accepted,r.lifetimeCount.load());
            return true;
        }
#endif
        inline bool DirectCase(const char* name,int substeps,float length,unsigned kind,DirectMetrics& metrics
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            ,bool observe=false,DirectParity* parity=nullptr
#endif
            )
        {
            // No GEngine world is alive here; the test owns and restores the SDK lifetime.
            if(!Check(JPH::Factory::sInstance==nullptr,"direct SDK lifetime unowned"))return false;
            JPH::RegisterDefaultAllocator();
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            Phase26JoltRecorder::AllocatorScope allocations;
#else
            AllocationScope allocations;
#endif
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            const auto epoch=std::uint64_t(0x26000000)+CompletedCheckTicks+1;
            struct DetachObserver
            {
                bool enabled{};
                std::uint64_t epoch{};
                ~DetachObserver(){if(enabled){ObservationDetachOkay &= ObservationRaw.EndEpoch(epoch);
                    ObservationDetachOkay &= Phase26JoltRecorder::Get().Detach();}}
            } detach;
            if(observe)
            {
                detach.enabled=ObservationRaw.InstallEpoch(epoch);detach.epoch=epoch;
                if(!Check(detach.enabled,"direct lifetime sink acquired before Factory"))return false;
            }
#endif
            JPH::Factory::sInstance=new JPH::Factory;JPH::RegisterTypes();
            struct Lifetime {~Lifetime(){JPH::UnregisterTypes();delete JPH::Factory::sInstance;JPH::Factory::sInstance=nullptr;}} lifetime;
            LayerMap map;BroadFilter broad;PairFilter pairs;JPH::PhysicsSystem physics;
            physics.Init(32,0,128,128,map,broad,pairs);physics.SetGravity(kind==3?JPH::Vec3::sZero():JPH::Vec3(0,-12,0));
            MeasuredTemp temp;JPH::JobSystemThreadPool jobs(JPH::cMaxPhysicsJobs,JPH::cMaxPhysicsBarriers,2);
            Observer observed;Listener listener;listener.observer=&observed;listener.length=length;physics.SetContactListener(&listener);
            auto& bodies=physics.GetBodyInterface();
            const auto orientation=kind==1?JPH::Quat::sRotation(JPH::Vec3::sAxisZ(),.3f):kind==2?JPH::Quat::sRotation(JPH::Vec3::sAxisZ(),JPH::JPH_PI*.5f):JPH::Quat::sIdentity();
            const auto normal=orientation*JPH::Vec3::sAxisY();
            JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(10,.5f,10),0),JPH::RVec3::sZero(),orientation,JPH::EMotionType::Static,0);
            floor.mFriction=.5f;floor.mRestitution=.5f;
            if(kind==2){floor.mMotionType=JPH::EMotionType::Kinematic;floor.mObjectLayer=1;floor.mAngularVelocity=normal*.2f;}
            const auto floorID=bodies.CreateAndAddBody(floor,kind==2?JPH::EActivation::Activate:JPH::EActivation::DontActivate);
            JPH::BodyID laterCandidate;
            if(kind==3){floor.mPosition=JPH::RVec3(0,-3,0);laterCandidate=bodies.CreateAndAddBody(floor,JPH::EActivation::DontActivate);}
            const auto position=kind==3?JPH::RVec3(0,5,0):JPH::RVec3(normal*(kind==4?1.f:kind==5?1.515f:1.5f));
            JPH::ShapeRefC shape=kind==4?JPH::ShapeRefC(new JPH::BoxShape(JPH::Vec3(.8f,.5f,.7f),0)):JPH::ShapeRefC(new JPH::SphereShape(1));
            JPH::BodyCreationSettings body(shape.GetPtr(),position,orientation,JPH::EMotionType::Dynamic,1);
            body.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;body.mMassPropertiesOverride.mMass=1;
            body.mLinearDamping=body.mAngularDamping=0;body.mFriction=.5f;body.mRestitution=.5f;body.mAngularVelocity={1,2,3};
            body.mMotionQuality=kind==3?JPH::EMotionQuality::LinearCast:JPH::EMotionQuality::Discrete;
            if(kind==3)body.mLinearVelocity={0,-400,0};
            if(kind==5){body.mLinearVelocity={0,3,0};physics.SetGravity(JPH::Vec3::sZero());}
            if(kind==6)body.mIsSensor=true;
            if(kind==2)physics.SetGravity(normal*-12);
            const auto id=bodies.CreateAndAddBody(body,JPH::EActivation::Activate);
            if(!Check(!floorID.IsInvalid()&&!id.IsInvalid(),"direct SDK fixture bodies"))return false;
            const auto start=std::chrono::steady_clock::now();JPH::EPhysicsUpdateError error;
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            if(observe&&!Check(ObservationRaw.BeforeUpdate(epoch),"raw direct construction frame"))return false;
#endif
            allocations.Begin();
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            if(observe&&!Check(Phase26JoltRecorder::Get().Prepare(&physics,epoch,1,static_cast<JPH::uint32>(substeps)),"direct observation sink arm"))return false;
#endif
            error=physics.Update(1.f/60.f,substeps,&temp,&jobs);
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            const bool observedStep=!observe||Phase26JoltRecorder::Get().FinishUpdate();
#endif
            allocations.End();metrics.heap=AllocCalls;metrics.bytes=AllocBytes;
            metrics.ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();++CompletedCheckTicks;
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            if(observe&&!Check(ObservationRaw.AfterUpdate(epoch,1,substeps),"raw direct solved Update frame"))return false;
            if(!Check(observedStep,"direct valid diagnostic completed step"))return false;
#endif
            if(!Check(CompletedCheckTicks<=TickLimit&&error==JPH::EPhysicsUpdateError::None&&temp.storage.IsEmpty(),"direct SDK completed step and arena release")||!observed.Validate(name,1,substeps==1))return false;
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            if(observe&&!ValidateDirectObservation(name,physics,observed,floorID,id,laterCandidate,
                epoch,1,substeps,length,kind))return false;
            if(observe&&!ValidateArenaStream(Phase26JoltRecorder::Get(),temp.peak.load()))return false;
            if(observe)ObservationActual.Add(Phase26JoltRecorder::Get(),temp.peak.load());
            if(parity&&(!CaptureDirectParity(physics,observed,{floorID,id,laterCandidate},*parity,0)||
                !ObservationRaw.Parity(1,kind?6+kind:(substeps==1?0:substeps==2?2:4)+(length>0?2:1),
                    observe,epoch,1,substeps,parity->step[0])))return false;
#endif
            metrics.peak=temp.peak;metrics.discrete=observed.discrete;metrics.ccd=observed.ccd;metrics.positive=observed.positive;metrics.roots=observed.roots;
            if(length>0&&kind<5&&!Check(observed.positive>0&&(kind!=3||observed.ccd>0),"actual solved discrete/accepted CCD positive load"))return false;
            if(kind==5&&!Check(observed.positive==0,"separating speculative contact has no positive solved-load budget"))return false;
            if(kind==6&&!Check(observed.count==0,"sensor has no resistance solve"))return false;
            if(kind==3)for(std::size_t i=0;i<observed.count.load();++i){const auto& row=(*observed.rows)[i];if(row.value.mIsCCD&&!Check(row.id1==floorID.GetIndexAndSequenceNumber()||row.id2==floorID.GetIndexAndSequenceNumber(),"only actual earliest CCD contact spends impulse budget"))return false;}
            if(length==0 && !Check(observed.count==0,"exact zero-length numerical bypass"))return false;
            bodies.RemoveBody(id);bodies.DestroyBody(id);bodies.RemoveBody(floorID);bodies.DestroyBody(floorID);
            if(!laterCandidate.IsInvalid()){bodies.RemoveBody(laterCandidate);bodies.DestroyBody(laterCandidate);}
            body.mPosition=JPH::RVec3(20,20,20);body.mLinearVelocity=JPH::Vec3::sZero();
            const auto replacement=bodies.CreateAndAddBody(body,JPH::EActivation::Activate);
            if(!Check(!replacement.IsInvalid()&&replacement!=id,"retired BodyID sequence not reused"))return false;
            {JPH::BodyLockRead stale(physics.GetBodyLockInterface(),id);if(!Check(!stale.Succeeded(),"stale SDK BodyID rejected"))return false;}
            observed.Reset();
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            temp.peak=0;
            if(observe&&!Check(ObservationRaw.BeforeUpdate(epoch),"raw direct retirement interstice"))return false;
            if(observe&&!Check(Phase26JoltRecorder::Get().Prepare(&physics,epoch,2,static_cast<JPH::uint32>(substeps)),"direct retirement observation arm"))return false;
#endif
            error=physics.Update(1.f/60.f,substeps,&temp,&jobs);
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            const bool observedRetirement=!observe||Phase26JoltRecorder::Get().FinishUpdate();
#endif
            ++CompletedCheckTicks;
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            if(observe&&!Check(ObservationRaw.AfterUpdate(epoch,2,substeps),"raw direct retirement Update frame"))return false;
            if(!Check(observedRetirement,"direct valid diagnostic retirement step"))return false;
#endif
            if(!Check(error==JPH::EPhysicsUpdateError::None&&observed.count==0&&temp.storage.IsEmpty(),"retired contact carries no resistance load"))return false;
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
            if(observe&&!ValidateDirectObservation(name,physics,observed,floorID,id,laterCandidate,
                epoch,2,substeps,length,kind))return false;
            if(observe&&!ValidateArenaStream(Phase26JoltRecorder::Get(),temp.peak.load()))return false;
            if(observe)ObservationActual.Add(Phase26JoltRecorder::Get(),temp.peak.load());
            if(parity&&(!CaptureDirectParity(physics,observed,{replacement,JPH::BodyID(),JPH::BodyID()},*parity,1)||
                !ObservationRaw.Parity(1,kind?6+kind:(substeps==1?0:substeps==2?2:4)+(length>0?2:1),
                    observe,epoch,2,substeps,parity->step[1])))return false;
#endif
            bodies.RemoveBody(replacement);bodies.DestroyBody(replacement);physics.SetContactListener(nullptr);
            std::println("RESISTANCE_DIRECT case={} substeps={} length={} heap_calls={} heap_bytes={} temp_peak={} update_ns={} discrete_rows={} ccd_rows={} positive_rows={} root_iterations={} callbacks={}",name,substeps,length,metrics.heap,metrics.bytes,metrics.peak,metrics.ns,metrics.discrete,metrics.ccd,metrics.positive,metrics.roots,listener.candidates.load());
            return Check(temp.peak<=32*1024*1024,"unchanged 32 MiB arena");
        }
        inline bool DirectChecks()
        {
            const auto before=CompletedCheckTicks;
            for(int substeps:{1,2,16})
            {
                DirectMetrics zero,positive;
                if(!DirectCase("matched-floor-zero",substeps,0,0,zero)||!DirectCase("matched-floor-positive",substeps,.05f,0,positive))return false;
                if(!Check(zero.heap==positive.heap&&zero.bytes==positive.bytes,"no added steady SDK heap allocations on matched contact workload"))return false;
            }
            for(unsigned kind:{1u,2u,3u,4u,5u,6u}){DirectMetrics metrics;if(!DirectCase(kind==1?"slope":kind==2?"wall":kind==3?"accepted-ccd":kind==4?"box-patch":kind==5?"speculative-zero":"sensor",1,.05f,kind,metrics))return false;}
            std::println("RESISTANCE_DIRECT_PASS ticks={} substeps=1,2,16 floor_wall_slope_box_ccd=1 paired_heap=1 retirement=1 sensor_speculative=1",CompletedCheckTicks-before);return true;
        }
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
        inline bool ObservationDirectChecks()
        {
            const auto before=CompletedCheckTicks;
            for(const int substeps:{1,2,16})
            {
                std::array<std::array<DirectMetrics,2>,2> metrics{};
                for(unsigned lengthIndex=0;lengthIndex<2;++lengthIndex)
                {
                    const float length=lengthIndex?.05f:0.f;
                    DirectParity off,on;
                    if(!DirectCase(lengthIndex?"matched-floor-positive":"matched-floor-zero",substeps,length,0,
                        metrics[lengthIndex][0],false,&off) ||
                        !DirectCase(lengthIndex?"matched-floor-positive":"matched-floor-zero",substeps,length,0,
                        metrics[lengthIndex][1],true,&on) || !SameDirectParity(off,on))return false;
                }
                for(unsigned arm=0;arm<2;++arm)
                    if(!Check(metrics[0][arm].heap==metrics[1][arm].heap&&
                        metrics[0][arm].bytes==metrics[1][arm].bytes,
                        "unchanged matched zero/positive direct SDK heap criterion"))return false;
            }
            for(unsigned kind:{1u,2u,3u,4u,5u,6u})
            {
                const char* name=kind==1?"slope":kind==2?"wall":kind==3?"accepted-ccd":
                    kind==4?"box-patch":kind==5?"speculative-zero":"sensor";
                DirectMetrics offMetrics,onMetrics;DirectParity off,on;
                if(!DirectCase(name,1,.05f,kind,offMetrics,false,&off)||
                    !DirectCase(name,1,.05f,kind,onMetrics,true,&on)||!SameDirectParity(off,on))return false;
            }
            return Check(CompletedCheckTicks-before==48,"exact paired direct-fixture Update allotment");
        }

        struct CompactImpulseRow
        {
            JPH::ContactResistanceObservation value;
            JPH::uint32 id1{},id2{};
        };
        class CompactImpulseObserver final : public JPH::ContactResistanceObserver
        {
        public:
        static constexpr std::size_t Capacity=2816;
            std::unique_ptr<std::array<CompactImpulseRow,Capacity>> rows=std::make_unique<std::array<CompactImpulseRow,Capacity>>();
            std::atomic<std::size_t> count{};
            void Reset() noexcept {count.store(0,std::memory_order_relaxed);}
            void OnContactResistanceSolved(const JPH::Body& a,const JPH::Body& b,
                const JPH::ContactResistanceObservation& value) override
            {
                const auto i=count.fetch_add(1,std::memory_order_relaxed);
                if(i>=Capacity)return;
                auto& row=(*rows)[i];row.value=value;
                row.id1=a.GetID().GetIndexAndSequenceNumber();row.id2=b.GetID().GetIndexAndSequenceNumber();
            }
        };
        struct CompactListener final : JPH::ContactListener
        {
            CompactImpulseObserver* observer{};
            std::atomic<unsigned> candidates{},removed{};
            void Configure(JPH::ContactSettings& settings)
            {
                settings.mCombinedSpinResistanceLength=.05f;
                settings.mCombinedRollingResistanceLength=.05f;
                settings.mContactResistanceObserver=observer;
            }
            void OnContactAdded(const JPH::Body&,const JPH::Body&,const JPH::ContactManifold&,
                JPH::ContactSettings& settings) override {++candidates;Configure(settings);}
            void OnContactPersisted(const JPH::Body&,const JPH::Body&,const JPH::ContactManifold&,
                JPH::ContactSettings& settings) override {++candidates;Configure(settings);}
            void OnContactRemoved(const JPH::SubShapeIDPair&) override {++removed;}
        };
        using FixtureFrame=FixedFrame<CompactImpulseObserver::Capacity,32>;
        using SmallFixtureFrame=FixedFrame<256,32>;
        template<std::size_t Rows,std::size_t Bodies>
        inline bool CaptureFixtureFrame(JPH::PhysicsSystem& physics,const CompactImpulseObserver& observed,
            const std::array<JPH::BodyID,32>& ids,
            const std::array<JPH::Ref<JPH::TwoBodyConstraint>,8>& joints,std::size_t jointCount,
            FixedFrame<Rows,Bodies>& frame)
        {
            if(!CapturePhysical(physics,ids,frame)||!CaptureImpulses(observed,frame)||
                !Check(jointCount<=frame.jointLambda.size(),"bounded retained distance-joint parity"))return false;
            frame.jointCount=static_cast<std::uint32_t>(jointCount);
            for(std::size_t i=0;i<jointCount;++i)
            {
                if(!Check(joints[i].GetPtr()!=nullptr,"retained joint still live"))return false;
                const auto* distance=static_cast<const JPH::DistanceConstraint*>(joints[i].GetPtr());
                frame.jointLambda[i]=Bits(distance->GetTotalLambdaPosition());
                frame.jointActive[i]=JPH::uint32(distance->IsActive());
            }
            return true;
        }

        struct ObservationAllocatorRegistration
        {ObservationAllocatorRegistration(){JPH::RegisterDefaultAllocator();}};
        struct ObservationFactoryLifetime
        {
            ObservationFactoryLifetime(){JPH::Factory::sInstance=new JPH::Factory;JPH::RegisterTypes();}
            ~ObservationFactoryLifetime(){JPH::UnregisterTypes();delete JPH::Factory::sInstance;JPH::Factory::sInstance=nullptr;}
        };
        struct ObservationDetach
        {
            bool armed{};
            std::uint64_t epoch{};
            ~ObservationDetach(){if(armed){ObservationDetachOkay &= ObservationRaw.EndEpoch(epoch);
                ObservationDetachOkay &= Phase26JoltRecorder::Get().Detach();}}
        };
        struct ObservationWorld
        {
            ObservationAllocatorRegistration registration;
            Phase26JoltRecorder::AllocatorScope allocations;
            ObservationDetach detach;
            ObservationFactoryLifetime factory;
            LayerMap map;BroadFilter broad;PairFilter pairs;
            JPH::PhysicsSystem physics;
            MeasuredTemp temp;
            JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs,JPH::cMaxPhysicsBarriers,2};
            CompactImpulseObserver observed;
            CompactListener listener;
            std::array<JPH::BodyID,32> ids{};
            std::size_t bodyCount{};
            std::array<JPH::Ref<JPH::TwoBodyConstraint>,8> joints{};
            std::size_t jointCount{};
            ObservationWorld(bool arm,std::uint64_t epoch):detach{arm&&ObservationRaw.InstallEpoch(epoch),epoch}
            {
                physics.Init(32,0,128,128,map,broad,pairs);
                listener.observer=&observed;
                physics.SetContactListener(&listener);
            }
            ~ObservationWorld()
            {
                physics.SetContactListener(nullptr);
                for(std::size_t i=0;i<jointCount;++i)
                    if(joints[i]){physics.RemoveConstraint(joints[i].GetPtr());joints[i]=nullptr;}
                auto& bodies=physics.GetBodyInterface();
                for(std::size_t i=0;i<bodyCount;++i)
                    if(!ids[i].IsInvalid())
                    {
                        if(bodies.IsAdded(ids[i]))bodies.RemoveBody(ids[i]);
                        bodies.DestroyBody(ids[i]);ids[i]=JPH::BodyID();
                    }
            }
            JPH::BodyID Add(const JPH::BodyCreationSettings& settings,JPH::EActivation activation)
            {
                if(bodyCount==ids.size())return JPH::BodyID();
                const auto id=physics.GetBodyInterface().CreateAndAddBody(settings,activation);
                if(!id.IsInvalid())ids[bodyCount++]=id;
                return id;
            }
            void Retire(JPH::BodyID id)
            {
                auto& bodies=physics.GetBodyInterface();
                if(bodies.IsAdded(id))bodies.RemoveBody(id);
                bodies.DestroyBody(id);
                for(std::size_t i=0;i<bodyCount;++i)if(ids[i]==id){ids[i]=JPH::BodyID();break;}
            }
            bool AddJoint(JPH::BodyID a,JPH::BodyID b,JPH::RVec3Arg pa,JPH::RVec3Arg pb)
            {
                if(jointCount==joints.size())return false;
                JPH::DistanceConstraintSettings settings;
                settings.mSpace=JPH::EConstraintSpace::WorldSpace;
                settings.mPoint1=pa;settings.mPoint2=pb;
                settings.mMinDistance=settings.mMaxDistance=2.25f;
                JPH::Ref<JPH::TwoBodyConstraint> joint(physics.GetBodyInterface().CreateConstraint(&settings,a,b));
                if(!joint)return false;
                physics.AddConstraint(joint.GetPtr());
                joints[jointCount++]=joint;
                return true;
            }
            template<std::size_t Rows,std::size_t Bodies>
            bool Step(const char* name,bool arm,std::uint64_t epoch,unsigned update,int substeps,
                FixedFrame<Rows,Bodies>& frame)
            {
                if(!Check(CompletedCheckTicks<TickLimit&&temp.storage.IsEmpty()&&(!arm||detach.armed),
                    "fixture Update budget, empty arena and lifetime sink ownership"))return false;
                observed.Reset();temp.peak=0;
                if(arm&&!Check(ObservationRaw.BeforeUpdate(epoch),"raw fixture construction or interstice frame"))return false;
                allocations.Begin();
                if(arm)
                {
                    if(!Check(Phase26JoltRecorder::Get().Prepare(&physics,epoch,update,
                        static_cast<JPH::uint32>(substeps)),"fixture diagnostic sink arm"))
                    {allocations.End();return false;}
                }
                const auto error=physics.Update(1.f/60.f,substeps,&temp,&jobs);
                const bool observationGood=!arm||Phase26JoltRecorder::Get().FinishUpdate();
                allocations.End();++CompletedCheckTicks;
                if(arm&&!Check(ObservationRaw.AfterUpdate(epoch,update,substeps),"raw fixture solved Update frame"))return false;
                if(!Check(error==JPH::EPhysicsUpdateError::None&&observationGood&&temp.storage.IsEmpty()&&
                    temp.peak.load()<=32*1024*1024&&observed.count.load()<=CompactImpulseObserver::Capacity,
                    "fixture complete SDK Update, observer and 32MiB arena"))return false;
                if(!CaptureFixtureFrame(physics,observed,ids,joints,jointCount,frame))return false;
                const std::uint32_t fixture=epoch==0x2600ccd0u?2:epoch==0x26001fe0u?4:3;
                const std::uint32_t caseId=fixture==2?201:fixture==4?301:100+substeps;
                if(!Check(ObservationRaw.Parity(fixture,caseId,arm,epoch,update,substeps,frame),
                    "raw fixture authoritative parity frame"))return false;
                if(arm)
                {
                    const auto& r=Phase26JoltRecorder::Get();
                    if(!ValidateArenaStream(r,temp.peak.load()))return false;
                    ObservationActual.Add(r,temp.peak.load());
                    for(unsigned i=0;i<r.pointCount.load();++i)
                        if(!ObservationContext(r.points[i].context,physics,epoch,update,substeps))return false;
                    for(unsigned i=0;i<r.ccdCount.load();++i)
                        if(!ObservationContext(r.ccd[i].context,physics,epoch,update,substeps))return false;
                    for(unsigned i=0;i<r.workCount.load();++i)
                        if(!ObservationContext(r.work[i].context,physics,epoch,update,substeps))return false;
                }
                std::printf("PHASE26_OBSERVER_FIXTURE case=%s arm=%d update=%u substeps=%d points=%u ccd=%u work=%u heap=%u worker_threads=%u arena_peak=%zu\n",
                    name,int(arm),update,substeps,arm?Phase26JoltRecorder::Get().pointCount.load():0,
                    arm?Phase26JoltRecorder::Get().ccdCount.load():0,
                    arm?Phase26JoltRecorder::Get().workCount.load():0,
                    arm?Phase26JoltRecorder::Get().heapCount.load():0,
                    arm?Phase26JoltRecorder::Get().WorkerCount():0,temp.peak.load());
                return true;
            }
        };
        inline JPH::BodyCreationSettings ObservationSphere(JPH::RVec3Arg position,
            JPH::Vec3Arg velocity=JPH::Vec3::sZero(),JPH::EMotionQuality quality=JPH::EMotionQuality::Discrete)
        {
            JPH::BodyCreationSettings body(new JPH::SphereShape(1),position,JPH::Quat::sIdentity(),
                JPH::EMotionType::Dynamic,1);
            body.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;
            body.mMassPropertiesOverride.mMass=1;
            body.mLinearDamping=body.mAngularDamping=0;
            body.mFriction=body.mRestitution=.5f;
            body.mAngularVelocity={1,2,3};body.mLinearVelocity=velocity;body.mMotionQuality=quality;
            return body;
        }
        inline JPH::BodyCreationSettings ObservationFloor(JPH::RVec3Arg position,
            JPH::QuatArg rotation=JPH::Quat::sIdentity())
        {
            JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(10,.5f,10),0),
                position,rotation,JPH::EMotionType::Static,0);
            floor.mFriction=floor.mRestitution=.5f;
            return floor;
        }
        inline bool HasPair(JPH::uint32 a,JPH::uint32 b,JPH::BodyID x,JPH::BodyID y)
        {return (a==x.GetIndexAndSequenceNumber()&&b==y.GetIndexAndSequenceNumber())||
                (a==y.GetIndexAndSequenceNumber()&&b==x.GetIndexAndSequenceNumber());}
        inline bool ValidateCCDArbitration(const Phase26JoltRecorder::Recorder& r,JPH::BodyID wall,
            JPH::BodyID a,JPH::BodyID b)
        {
            namespace Q=JPH::QualificationObservation;
            const Q::CCDRecord *abCandidate=nullptr,*bwCandidate=nullptr,*abAccepted=nullptr;
            unsigned abAcceptances=0,bwAcceptances=0;
            for(unsigned i=0;i<r.ccdCount.load();++i)
            {
                const auto kind=r.ccdKind[i];const auto& c=r.ccd[i];
                if(kind==Q::EventKind::CCDCandidate)
                {
                    if(HasPair(c.body1,c.body2,a,b))abCandidate=&c;
                    if(HasPair(c.body1,c.body2,b,wall))bwCandidate=&c;
                }
                else if(kind==Q::EventKind::CCDAccepted)
                {
                    if(HasPair(c.body1,c.body2,a,b)){abAccepted=&c;++abAcceptances;}
                    if(HasPair(c.body1,c.body2,b,wall))++bwAcceptances;
                    unsigned matching=0;
                    for(unsigned j=0;j<r.ccdCount.load();++j)
                    {
                        const auto& d=r.ccd[j];
                        if(r.ccdKind[j]==Q::EventKind::CCDCandidate&&
                            c.context.epoch==d.context.epoch&&c.context.update==d.context.update&&
                            c.context.substep==d.context.substep&&c.body1==d.body1&&c.body2==d.body2&&
                            c.subshape1==d.subshape1&&c.subshape2==d.subshape2&&
                            Bits(c.fraction)==Bits(d.fraction)&&
                            Bits(c.fraction_plus_slop)==Bits(d.fraction_plus_slop)&&
                            Bits(c.response_point[0])==Bits(d.response_point[0])&&
                            Bits(c.response_point[1])==Bits(d.response_point[1])&&
                            Bits(c.response_point[2])==Bits(d.response_point[2]))++matching;
                    }
                    if(!Check(matching==1,"CCD accepted response has one exact prepared candidate"))return false;
                }
            }
            if(!Check(abCandidate&&bwCandidate&&abAccepted&&abAcceptances==1&&bwAcceptances==0&&
                abCandidate->fraction<bwCandidate->fraction&&abAccepted->normal_lambda>0&&
                abAccepted->context.substep==0&&bwCandidate->context.substep==0,
                "CCD moving-body A-B accepted before rejected B-wall candidate"))return false;
            unsigned impactPoints=0,acceptedLocal=0,bwImpact=0;
            for(unsigned i=0;i<r.pointCount.load();++i)
                if(r.pointKind[i]==Q::EventKind::CCDManifoldPoint)
                {
                    const auto& p=r.points[i];
                    if(!Check(ObservationFinite(p.anchor1)&&ObservationFinite(p.anchor2),
                        "CCD copied finite manifold anchors"))return false;
                    if(p.space==Q::AnchorSpace::WorldAtImpact)
                    {
                        ++impactPoints;
                        if(HasPair(p.body1,p.body2,a,b))
                        {
                            const float* aPoint=p.body1==a.GetIndexAndSequenceNumber()?p.anchor1:p.anchor2;
                            const float* bPoint=p.body1==b.GetIndexAndSequenceNumber()?p.anchor1:p.anchor2;
                            const double fraction=abCandidate->fraction;
                            const double ra=std::hypot(double(aPoint[0])-6*fraction,
                                double(aPoint[1])-15,double(aPoint[2]));
                            const double rb=std::hypot(double(bPoint[0])-(2.6+3*fraction),
                                double(bPoint[1])-15,double(bPoint[2]));
                            if(!Check(ra>=.8&&ra<=1.2&&rb>=.8&&rb<=1.2,
                                "A-B candidate anchors use translated impact-pose sphere centers"))return false;
                        }
                        if(HasPair(p.body1,p.body2,b,wall))
                        {
                            ++bwImpact;
                            const float* bPoint=p.body1==b.GetIndexAndSequenceNumber()?p.anchor1:p.anchor2;
                            const float* wallPoint=p.body1==wall.GetIndexAndSequenceNumber()?p.anchor1:p.anchor2;
                            const double fraction=bwCandidate->fraction;
                            const double rb=std::hypot(double(bPoint[0])-(2.6+3*fraction),
                                double(bPoint[1])-15,double(bPoint[2]));
                            if(!Check(rb>=.8&&rb<=1.2&&std::abs(double(wallPoint[0])-5.4)<=.2,
                                "B-wall rejected candidate anchors use moving B and rotated wall impact pose"))return false;
                        }
                    }
                    else if(p.space==Q::AnchorSpace::COMLocal&&p.ordinal==JPH::uint32(-1))
                    {
                        if(!Check(HasPair(p.body1,p.body2,a,b)&&p.normal_lambda>0,
                            "accepted CCD local anchors use surviving A-B and final lambda"))return false;
                        const float* aAnchor=p.body1==a.GetIndexAndSequenceNumber()?p.anchor1:p.anchor2;
                        const float* bAnchor=p.body1==b.GetIndexAndSequenceNumber()?p.anchor1:p.anchor2;
                        const auto ra=std::hypot(aAnchor[0],aAnchor[1],aAnchor[2]);
                        const auto rb=std::hypot(bAnchor[0],bAnchor[1],bAnchor[2]);
                        if(!Check(ra>=.8f&&ra<=1.2f&&rb>=.8f&&rb<=1.2f,
                            "accepted A anchor uses translated impact COM rather than step-start COM"))return false;
                        ++acceptedLocal;
                    }
                    else return Check(false,"typed CCD candidate world or accepted impact-local anchor");
                }
            return Check(impactPoints>0&&bwImpact>0&&acceptedLocal==1&&
                abAccepted->manifold_points>0,"CCD candidate manifolds and accepted impact-pose anchors complete");
        }
        inline bool ObservationArbitrationArm(bool arm,
            std::array<SmallFixtureFrame,2>& off,SmallFixtureFrame& scratch)
        {
            if(!Check(JPH::Factory::sInstance==nullptr,"arbitration direct SDK lifetime unowned"))return false;
            ObservationWorld world(arm,0x2600ccd0u);world.physics.SetGravity(JPH::Vec3::sZero());
            const auto wall=world.Add(ObservationFloor(JPH::RVec3(static_cast<JPH::Real>(5.9),15,0),
                JPH::Quat::sRotation(JPH::Vec3::sAxisZ(),-JPH::JPH_PI*.5f)),JPH::EActivation::DontActivate);
            const auto a=world.Add(ObservationSphere(JPH::RVec3(0,15,0),JPH::Vec3(360,0,0),
                JPH::EMotionQuality::LinearCast),JPH::EActivation::Activate);
            const auto b=world.Add(ObservationSphere(JPH::RVec3(static_cast<JPH::Real>(2.6),15,0),JPH::Vec3(180,0,0),
                JPH::EMotionQuality::LinearCast),JPH::EActivation::Activate);
            if(!Check(!wall.IsInvalid()&&!a.IsInvalid()&&!b.IsInvalid()&&
                a.GetIndexAndSequenceNumber()<b.GetIndexAndSequenceNumber(),
                "frozen CCD arbitration wall/A/B full BodyID ordering"))return false;
            auto& firstFrame=arm?scratch:off[0];
            if(!world.Step("ccd-arbitration",arm,0x2600ccd0u,1,1,firstFrame))return false;
            if(arm&&(!ValidateCCDArbitration(Phase26JoltRecorder::Get(),wall,a,b)||
                !SameFixedFrame(off[0],firstFrame)))return false;
            world.Retire(a);world.Retire(b);
            auto replacement=ObservationSphere(JPH::RVec3(20,20,20));
            replacement.mLinearVelocity=JPH::Vec3::sZero();
            const auto reused=world.Add(replacement,JPH::EActivation::Activate);
            if(!Check(!reused.IsInvalid()&&reused!=a&&reused!=b,
                "CCD retired full BodyID rejected after replacement"))return false;
            auto& secondFrame=arm?scratch:off[1];
            if(!world.Step("ccd-retirement",arm,0x2600ccd0u,2,1,secondFrame))return false;
            if(arm)
            {
                const auto& r=Phase26JoltRecorder::Get();
                for(unsigned i=0;i<r.ccdCount.load();++i)
                {
                    const auto& c=r.ccd[i];
                    if(r.ccdKind[i]==JPH::QualificationObservation::EventKind::CCDAccepted&&
                        !Check(c.body1!=a.GetIndexAndSequenceNumber()&&c.body2!=a.GetIndexAndSequenceNumber()&&
                            c.body1!=b.GetIndexAndSequenceNumber()&&c.body2!=b.GetIndexAndSequenceNumber(),
                            "CCD retired IDs have no accepted response"))return false;
                }
                if(!SameFixedFrame(off[1],secondFrame))return false;
            }
            return true;
        }
        inline bool ObservationArbitrationChecks()
        {
            const auto before=CompletedCheckTicks;
            auto off=std::make_unique<std::array<SmallFixtureFrame,2>>();
            auto scratch=std::make_unique<SmallFixtureFrame>();
            if(!ObservationArbitrationArm(false,*off,*scratch)||
                !ObservationArbitrationArm(true,*off,*scratch))return false;
            return Check(CompletedCheckTicks-before==4&&ObservationDetachOkay&&
                JPH::Factory::sInstance==nullptr,"exact CCD arbitration updates and clean SDK teardown");
        }
        inline bool ValidateConcurrentFrame(JPH::PhysicsSystem& world,const Phase26JoltRecorder::Recorder& r,
            const std::array<JPH::BodyID,8>& floors,const std::array<JPH::BodyID,8>& left,
            const std::array<JPH::BodyID,8>& right,int substeps)
        {
            namespace Q=JPH::QualificationObservation;
            std::array<std::array<bool,2>,8> seen{};
            for(unsigned j=0;j<r.pointCount.load();++j)
            {
                const auto kind=r.pointKind[j];const auto& p=r.points[j];
                if(!Check(kind==Q::EventKind::Point&&p.space==Q::AnchorSpace::COMLocal&&
                    ObservationFinite(p.anchor1)&&ObservationFinite(p.anchor2)&&
                    ObservationFinite(p.cached_local_normal)&&ObservationFinite(p.solver_world_normal)&&
                    std::isfinite(p.normal_lambda),
                    "concurrent fixture has finite discrete COM-local witnesses only"))return false;
                bool matched=false;
                for(unsigned i=0;i<8;++i)
                {
                    if(HasPair(p.body1,p.body2,floors[i],left[i])){seen[i][0]=true;matched=true;}
                    if(HasPair(p.body1,p.body2,floors[i],right[i])){seen[i][1]=true;matched=true;}
                }
                if(!Check(matched,"concurrent contact belongs to one sphere/floor island"))return false;
                if(!ValidateDiscreteGeometry(world,p))return false;
            }
            for(const auto& pair:seen)for(const bool value:pair)
                if(!Check(value,"all sixteen independent sphere/floor identities observed"))return false;
            std::array<bool,16> substepSeen{};
            std::uint64_t jointSetup=0,contactSolve=0;
            for(unsigned j=0;j<r.workCount.load();++j)
            {
                const auto& work=r.work[j];
                if(work.context.substep<substepSeen.size())substepSeen[work.context.substep]=true;
                if(work.kind==Q::WorkKind::JointSetupDispatch)jointSetup+=work.count;
                if(work.kind==Q::WorkKind::ContactNormalPointAttempt)contactSolve+=work.count;
            }
            for(int i=0;i<substeps;++i)
                if(!Check(substepSeen[static_cast<std::size_t>(i)],"complete actual substep work context"))return false;
            if(!Check(jointSetup>0&&contactSolve>0,"actual joint and contact dispatch work observed"))return false;
            if(!Check(r.heapOwnershipValid,"zero joint-induced heap and exact source-bound baseline heap ownership"))return false;
            return true;
        }
        inline bool ObservationConcurrentArm(bool arm,int substeps,FixtureFrame& scratch)
        {
            if(!Check(JPH::Factory::sInstance==nullptr,"concurrent direct SDK lifetime unowned"))return false;
            ObservationWorld world(arm,0x26000000u+static_cast<unsigned>(substeps));
            world.physics.SetGravity(JPH::Vec3(0,-12,0));
            std::array<JPH::BodyID,8> floors{},left{},right{};
            for(unsigned i=0;i<8;++i)
            {
                const float x=30.f*float(i);
                floors[i]=world.Add(ObservationFloor(JPH::RVec3(x,0,0)),JPH::EActivation::DontActivate);
                left[i]=world.Add(ObservationSphere(JPH::RVec3(x-1.125f,1.5f,0)),JPH::EActivation::Activate);
                right[i]=world.Add(ObservationSphere(JPH::RVec3(x+1.125f,1.5f,0)),JPH::EActivation::Activate);
                if(!Check(!floors[i].IsInvalid()&&!left[i].IsInvalid()&&!right[i].IsInvalid()&&
                    world.AddJoint(left[i],right[i],JPH::RVec3(x-1.125f,1.5f,0),
                        JPH::RVec3(x+1.125f,1.5f,0)),"frozen concurrent floor/spheres and rigid distance constraint"))return false;
                const auto* joint=world.joints[i].GetPtr();
                if(!Check(joint&&joint->GetBody1()->GetID()==left[i]&&
                    joint->GetBody2()->GetID()==right[i],"eight exact joint endpoint identities"))return false;
            }
            if(!Check(world.bodyCount==24&&world.jointCount==8,"eight 24-body islands within direct capacity"))return false;
            for(unsigned update=1;update<=4;++update)
            {
                if(!world.Step("concurrent-joints",arm,0x26000000u+static_cast<unsigned>(substeps),
                    update,substeps,scratch))return false;
                if(arm&&!ValidateConcurrentFrame(world.physics,Phase26JoltRecorder::Get(),floors,left,right,substeps))return false;
            }
            return true;
        }
        inline bool ObservationConcurrentChecks()
        {
            const auto before=CompletedCheckTicks;
            auto scratch=std::make_unique<FixtureFrame>();
            for(const int substeps:{1,2,16})
            {
                if(!ObservationConcurrentArm(false,substeps,*scratch)||
                    !ObservationConcurrentArm(true,substeps,*scratch))return false;
            }
            return Check(CompletedCheckTicks-before==24&&ObservationDetachOkay&&
                JPH::Factory::sInstance==nullptr,"exact concurrent fixture updates and clean SDK teardown");
        }
        inline bool ObservationLifecycleArm(bool arm,
            std::array<SmallFixtureFrame,6>& off,SmallFixtureFrame& scratch)
        {
            if(!Check(JPH::Factory::sInstance==nullptr,"lifecycle direct SDK lifetime unowned"))return false;
            ObservationWorld world(arm,0x26001fe0u);world.physics.SetGravity(JPH::Vec3(0,-12,0));
            auto& bodies=world.physics.GetBodyInterface();
            const auto floor=world.Add(ObservationFloor(JPH::RVec3::sZero()),JPH::EActivation::DontActivate);
            auto sphere=ObservationSphere(JPH::RVec3(0,1.49f,0));sphere.mAngularVelocity=JPH::Vec3::sZero();
            const auto first=world.Add(sphere,JPH::EActivation::DontActivate);
            if(!Check(!floor.IsInvalid()&&!first.IsInvalid()&&!bodies.IsActive(first),
                "frozen inactive overlapping sphere and floor"))return false;
            auto& firstFrame=arm?scratch:off[0];
            if(!world.Step("lifecycle-inactive",arm,0x26001fe0u,1,1,firstFrame))return false;
            const double overlap=1.5-double(bodies.GetPosition(first).GetY());
            if(!Check(overlap>0&&overlap<.02&&!bodies.IsActive(first),
                "independent inactive sphere/floor overlap without forced sleep"))return false;
            if(arm&&(!Check(Phase26JoltRecorder::Get().pointCount.load()==0,
                "inactive overlap has no fabricated solved witness")||
                !SameFixedFrame(off[0],firstFrame)))return false;
            bodies.ActivateBody(first);
            auto& secondFrame=arm?scratch:off[1];
            if(!world.Step("lifecycle-activate",arm,0x26001fe0u,2,1,secondFrame))return false;
            if(arm)
            {
                bool found=false;
                const auto& r=Phase26JoltRecorder::Get();
                for(unsigned i=0;i<r.pointCount.load();++i)
                    if(r.pointKind[i]==JPH::QualificationObservation::EventKind::Point&&
                        HasPair(r.points[i].body1,r.points[i].body2,floor,first))found=true;
                if(!Check(found,"activated overlapping pair has actual discrete witness")||
                    !SameFixedFrame(off[1],secondFrame))return false;
            }
            world.Retire(first);
            {JPH::BodyLockRead stale(world.physics.GetBodyLockInterface(),first);
                if(!Check(!stale.Succeeded(),"retired lifecycle BodyID rejected"))return false;}
            sphere.mPosition=JPH::RVec3(0,20,0);
            const auto farBody=world.Add(sphere,JPH::EActivation::DontActivate);
            if(!Check(!farBody.IsInvalid()&&farBody.GetIndex()==first.GetIndex()&&
                farBody.GetIndexAndSequenceNumber()!=first.GetIndexAndSequenceNumber(),
                "lifecycle recycled index receives new full BodyID sequence"))return false;
            auto& thirdFrame=arm?scratch:off[2];
            if(!world.Step("lifecycle-retire",arm,0x26001fe0u,3,1,thirdFrame))return false;
            if(arm)
            {
                bool retired=false;
                const auto& r=Phase26JoltRecorder::Get();
                for(unsigned i=0;i<r.lifetimeCount.load();++i)
                    if(r.lifetimes[i].life_kind==JPH::QualificationObservation::LifeKind::RetiredUnpersisted&&
                        HasPair(r.lifetimes[i].body1,r.lifetimes[i].body2,floor,first))retired=true;
                if(!Check(retired&&r.pointCount.load()==0,
                    "retired contact is historical and not live witness")||
                    !SameFixedFrame(off[2],thirdFrame))return false;
            }
            bodies.ActivateBody(farBody);
            auto& fourthFrame=arm?scratch:off[3];
            if(!world.Step("lifecycle-far",arm,0x26001fe0u,4,1,fourthFrame))return false;
            if(arm&&(!Check(Phase26JoltRecorder::Get().pointCount.load()==0,
                "activated far replacement has no floor contact")||
                !SameFixedFrame(off[3],fourthFrame)))return false;
            world.Retire(farBody);
            sphere.mPosition=JPH::RVec3(0,1.5f,0);
            const auto next=world.Add(sphere,JPH::EActivation::Activate);
            if(!Check(!next.IsInvalid()&&next.GetIndexAndSequenceNumber()!=first.GetIndexAndSequenceNumber()&&
                next.GetIndexAndSequenceNumber()!=farBody.GetIndexAndSequenceNumber(),
                "new contact uses distinct full BodyID"))return false;
            auto& fifthFrame=arm?scratch:off[4];
            if(!world.Step("lifecycle-recontact",arm,0x26001fe0u,5,1,fifthFrame))return false;
            if(arm)
            {
                bool found=false;
                const auto& r=Phase26JoltRecorder::Get();
                for(unsigned i=0;i<r.pointCount.load();++i)
                    if(r.pointKind[i]==JPH::QualificationObservation::EventKind::Point&&
                        HasPair(r.points[i].body1,r.points[i].body2,floor,next))found=true;
                if(!Check(found,"new full BodyID has its own actual contact witness")||
                    !SameFixedFrame(off[4],fifthFrame))return false;
            }
            world.Retire(next);world.Retire(floor);
            auto& sixthFrame=arm?scratch:off[5];
            if(!world.Step("lifecycle-empty",arm,0x26001fe0u,6,1,sixthFrame))return false;
            return Check(world.physics.GetNumBodies()==0&&(!arm||
                (Phase26JoltRecorder::Get().pointCount.load()==0&&
                 Phase26JoltRecorder::Get().ccdCount.load()==0)),
                "empty retired world has no live witness or CCD response")&&
                (!arm||SameFixedFrame(off[5],sixthFrame));
        }
        inline bool ObservationLifecycleChecks()
        {
            const auto before=CompletedCheckTicks;
            auto off=std::make_unique<std::array<SmallFixtureFrame,6>>();
            auto scratch=std::make_unique<SmallFixtureFrame>();
            if(!ObservationLifecycleArm(false,*off,*scratch)||
                !ObservationLifecycleArm(true,*off,*scratch))return false;
            return Check(CompletedCheckTicks-before==12&&ObservationDetachOkay&&
                JPH::Factory::sInstance==nullptr,"exact lifecycle updates and clean SDK teardown");
        }
        inline bool ObservationSyntheticChecks()
        {
            namespace Q=JPH::QualificationObservation;
            if(!Check(JPH::Factory::sInstance==nullptr,"synthetic direct SDK lifetime unowned"))return false;
            ObservationWorld world(true,0x26005a7eu);world.physics.SetGravity(JPH::Vec3::sZero());
            auto& r=Phase26JoltRecorder::Get();
            const auto context=[&](unsigned update)
            {
                return Q::Context{&world.physics,0x26005a7eu,update,0,Q::Phase::SharedStepWork,Q::ArenaSite::Shared};
            };
            if(!Check(ObservationRaw.BeforeUpdate(0x26005a7eu),
                "raw synthetic construction provenance"))return false;
            if(!Check(r.Prepare(&world.physics,0x26005a7eu,1,1),"synthetic concurrent sink arm"))return false;
            ObservationRaw.ResetDeltas();
            const auto publishWork=[&](unsigned start)
            {
                for(unsigned i=0;i<64;++i)
                {
                    Q::WorkRecord record{context(1),Q::WorkKind::SolverBatch,start+i,1,1};
                    Q::PublishSynthetic(record.context,Q::EventKind::Work,&record,sizeof(record));
                }
            };
            std::thread first(publishWork,0),second(publishWork,64);
            first.join();second.join();
            if(!Check(r.FinishUpdate()&&r.workCount.load()==2&&r.failureCount.load()==0&&
                r.heapCount.load()==0&&r.WorkerCount()==2,
                "synthetic two-worker exact-key publication complete"))return false;
            std::array<std::uint32_t,2> workerSums{};
            for(unsigned i=0;i<2;++i)
            {
                const auto& work=r.work[i];
                if(!Check(work.kind==Q::WorkKind::SolverBatch&&work.context.epoch==0x26005a7eu&&
                    work.context.update==1&&work.context.substep==0&&work.changed==64&&
                    work.root_iterations==64&&r.workWorker[i]!=0,
                    "synthetic aggregate retains exact context and worker identity"))return false;
                workerSums[i]=work.count;
            }
            std::sort(workerSums.begin(),workerSums.end());
            if(!Check(workerSums==std::array<std::uint32_t,2>{2016,6112},
                "synthetic worker aggregates preserve 128 publications and sum 8128"))return false;
            if(!Check(ObservationRaw.Synthetic(1,1),"raw synthetic concurrent worker record"))return false;
            if(!Check(r.Prepare(&world.physics,0x26005a7eu,2,1),"synthetic overflow sink arm"))return false;
            Q::PointRecord point{};point.context=context(2);point.body1=1;point.body2=2;
            point.space=Q::AnchorSpace::COMLocal;
            for(std::size_t i=0;i<=r.points.size();++i)
                Q::PublishSynthetic(point.context,Q::EventKind::Point,&point,sizeof(point));
            if(!Check(!r.FinishUpdate()&&!r.Valid(),"synthetic point capacity overflow rejected"))return false;
            if(!Check(ObservationRaw.Synthetic(2,2),"raw expected synthetic overflow record"))return false;
            if(!Check(r.Prepare(&world.physics,0x26005a7eu,3,1),"synthetic late sink arm"))return false;
            if(!Check(r.FinishUpdate(),"empty synthetic update completes"))return false;
            point.context=context(3);
            Q::PublishSynthetic(point.context,Q::EventKind::Point,&point,sizeof(point));
            bool late=false;
            for(unsigned i=0;i<r.failureCount.load();++i)
                late|=r.failures[i].code==Q::FailureCode::LateUpdate;
            if(!Check(late&&!r.Valid(),"late callback rejected after synthetic barrier"))return false;
            if(!Check(ObservationRaw.Synthetic(3,3),"raw expected synthetic late record"))return false;
            if(!Check(r.Prepare(&world.physics,0x26005a7eu,4,1),"synthetic owner context arm"))return false;
            point.context=context(4);point.context.world=&point;
            Q::PublishSynthetic(point.context,Q::EventKind::Point,&point,sizeof(point));
            bool wrongOwner=false;
            for(unsigned i=0;i<r.failureCount.load();++i)
                wrongOwner|=r.failures[i].code==Q::FailureCode::WrongOwner;
            if(!Check(!r.FinishUpdate()&&wrongOwner,"synthetic wrong-world callback rejected"))return false;
            if(!Check(ObservationRaw.Synthetic(4,4),"raw expected synthetic wrong-owner record"))return false;
            if(!Check(r.Prepare(&world.physics,0x26005a7eu,5,1),"synthetic substep context arm"))return false;
            point.context=context(5);point.context.substep=1;
            Q::PublishSynthetic(point.context,Q::EventKind::Point,&point,sizeof(point));
            bool outOfRange=false;
            for(unsigned i=0;i<r.failureCount.load();++i)
                outOfRange|=r.failures[i].code==Q::FailureCode::OutOfRangeSubstep;
            if(!Check(!r.FinishUpdate()&&outOfRange,"synthetic out-of-range substep rejected"))return false;
            if(!Check(ObservationRaw.Synthetic(5,5),"raw expected synthetic substep record"))return false;
            if(!Check(r.Prepare(&world.physics,0x26005a7eu,6,1)&&r.FinishUpdate(),
                "synthetic expected failures reset before clean teardown"))return false;
            if(!Check(ObservationRaw.Synthetic(6,6),"raw synthetic clean reset record"))return false;
            std::println("PHASE26_OBSERVER_SYNTHETIC_PASS worker_publications=128 aggregate_records=2 aggregate_count=8128 overflow_rejected=1 late_rejected=1 wrong_owner=1 substep=1 physics_updates=0");
            return true;
        }
        inline constexpr std::size_t DirectFixtureBytes=2*sizeof(DirectParity)+
            sizeof(CopiedRow)*Observer::Capacity;
        inline constexpr std::size_t ConcurrentFixtureBytes=sizeof(FixtureFrame)+
            sizeof(CompactImpulseRow)*CompactImpulseObserver::Capacity;
        inline constexpr std::size_t LifecycleFixtureBytes=sizeof(std::array<SmallFixtureFrame,6>)+
            sizeof(SmallFixtureFrame)+sizeof(CompactImpulseRow)*CompactImpulseObserver::Capacity;
        inline constexpr std::size_t SDKCandidateTableBytes=4096*96; // Conservative accepted CCD slot ceiling.
        inline constexpr std::size_t ArenaValidationScratchBytes=512*3*sizeof(std::uint64_t);
        inline constexpr std::size_t CombinedRecorderBytes=sizeof(::Rbs::Phase26::Session)+
            sizeof(::Rbs::Phase26Memory::events)+sizeof(Phase26JoltRecorder::Recorder)+
            sizeof(ObservationRawExport)+sizeof(ObservationTotals)+
            (std::max)({DirectFixtureBytes,ConcurrentFixtureBytes,LifecycleFixtureBytes})+
            SDKCandidateTableBytes+ArenaValidationScratchBytes;
        static_assert(CombinedRecorderBytes<=8*1024*1024,
            "combined Phase26 recorder including fixture parity exceeds accepted 8MiB");
        struct ObservationResult
        {
            bool abi{},direct{},ccd{},concurrent{},lifecycle{},synthetic{},parity{},witness{},allocation{},teardown{},raw{},complete{};
        };
        inline bool WriteObservationResult(const ObservationResult& result)
        {
            namespace Q=JPH::QualificationObservation;
            char* raw=nullptr;std::size_t size=0;
            const auto error=_dupenv_s(&raw,&size,"GENGINE_PHASE26_OBSERVATION_OUTPUT");
            const std::unique_ptr<char,decltype(&std::free)> root(raw,&std::free);
            if(error || !root || size<2)
            {std::println(stderr,"PHASE26_OBSERVER_RESULT_FAIL missing output directory");return false;}
            const auto target=(std::filesystem::path(root.get())/"result.json").string();
            std::string data="{\n  \"schema\": \"phase26-jolt-observer-correctness-01\",\n";
            data+=std::format("  \"status\": \"{}\",\n  \"complete\": {},\n  \"updates\": {},\n  \"planned_updates\": 88,\n  \"hard_cap\": 120,\n",
                result.complete?"PASS":"FAIL",result.complete?"true":"false",CompletedCheckTicks);
            data+=std::format("  \"abi_sentinel_expected\": {},\n  \"abi_sentinel_actual\": {},\n",
                Q::kExpectedObservationAbiSentinel,Q::kObservationAbiSentinel);
            data+=std::format("  \"recorder_bytes\": {},\n  \"combined_recorder_bytes\": {},\n  \"sdk_ccd_candidate_table_ceiling_bytes\": {},\n  \"arena_validation_scratch_bytes\": {},\n  \"existing_session_bytes\": {},\n  \"existing_event_bytes\": {},\n  \"direct_observer_bytes\": {},\n  \"concurrent_observer_bytes\": {},\n",
                sizeof(Phase26JoltRecorder::Recorder),CombinedRecorderBytes,SDKCandidateTableBytes,ArenaValidationScratchBytes,
                sizeof(::Rbs::Phase26::Session),
                sizeof(::Rbs::Phase26Memory::events),sizeof(CopiedRow)*Observer::Capacity,
                sizeof(CompactImpulseRow)*CompactImpulseObserver::Capacity);
            data+=std::format("  \"gates\": {{\"abi\":{},\"direct\":{},\"ccd_arbitration\":{},\"concurrent_contacts_joints\":{},\"lifecycle\":{},\"synthetic\":{},\"parity\":{},\"witness\":{},\"allocation\":{},\"teardown\":{},\"raw_export\":{}}},\n",
                result.abi?"true":"false",result.direct?"true":"false",result.ccd?"true":"false",
                result.concurrent?"true":"false",result.lifecycle?"true":"false",
                result.synthetic?"true":"false",result.parity?"true":"false",
                result.witness?"true":"false",result.allocation?"true":"false",
                result.teardown?"true":"false",result.raw?"true":"false");
            const auto& r=Phase26JoltRecorder::Get();
            data+=std::format("  \"heap_source_bounds\": {{\"schema\":1,\"base_job_pool_bytes\":{},\"base_job_slot_bytes\":{},\"base_job_slots\":{},\"broadphase_node_bytes\":{},\"broadphase_center_bytes\":{},\"body_slot_count_max\":{},\"base_job_pool_requests\":{},\"ownership_validated_updates\":{},\"ownership_failures\":{}}},\n",
                Q::kBaseJobPoolBytes,Q::kBaseJobSlotBytes,JPH::cMaxPhysicsJobs,Q::kBroadPhaseNodeBytes,
                Q::kBroadPhaseCenterBytes,r.heapBodySlotsMax,r.baseJobPoolTotalRequests,
                r.heapValidatedUpdates,r.heapOwnershipFailures);
            data+=std::format("  \"metrics\": {{\"points\":{},\"ccd\":{},\"retired\":{},\"work_records\":{},\"storage_records\":{},\"heap_records\":{},\"arena_peak_bytes\":{},\"arena_reserve_calls\":{},\"arena_reserve_bytes\":{},\"arena_release_calls\":{},\"arena_release_bytes\":{},\"arena_highwater_bytes\":{},\"unclassified_arena\":{},\"heap_allocation_calls\":{},\"heap_allocation_bytes\":{},\"heap_free_calls\":{},\"heap_free_bytes\":{},\"heap_live_bytes\":{},\"heap_peak_bytes\":{},\"unmatched_lifetime_frees\":{}}},\n",
                ObservationActual.points,ObservationActual.ccd,ObservationActual.retired,ObservationActual.work,
                ObservationActual.storage,ObservationActual.heap,ObservationActual.arenaPeak,
                r.arenaReserveCalls,r.arenaReserveBytes,r.arenaReleaseCalls,r.arenaReleaseBytes,
                r.arenaHighWater,r.unclassifiedArenaCount,r.heapAllocationCalls,r.heapAllocationBytes,
                r.heapFreeCalls,r.heapFreeBytes,r.heapLiveBytes,r.heapPeakBytes,r.unmatchedLifetimeFrees);
            using H=::Rbs::Phase26::JoltFrameHeader;
            using P=Q::PointRecord;using I=Q::IdentityRecord;using S=Q::StorageRecord;
            using E=Q::HeapRecord;using C=Q::Context;
            data+=std::format("  \"raw_export\": {{\"observer_frames\":{},\"parity_frames\":{},\"off_frame_references\":{},\"synthetic_frames\":{},\"observer_bytes\":{},\"parity_bytes\":{},\"synthetic_bytes\":{},\"observer_cap_bytes\":{},\"parity_cap_bytes\":{},\"synthetic_cap_bytes\":{},\"in_process_exact_readback\":true,\"valid\":{}}},\n",
                ObservationRaw.updateFrames,ObservationRaw.parityFrames,ObservationRaw.offFrameCount,ObservationRaw.syntheticFrames,
                ObservationRaw.eventBytes,ObservationRaw.parityBytes,ObservationRaw.syntheticBytes,
                ObservationRaw.EventCap,ObservationRaw.ParityCap,ObservationRaw.SyntheticCap,
                ObservationRaw.valid?"true":"false");
            data+=std::format("  \"jolt_observation_layout\": {{\"file\":\"observer.bin\",\"schema\":2,\"frame_header_bytes\":{},\"frame_kind_offset\":{},\"frame_counts_offset\":{},\"point_bytes\":{},\"ccd_bytes\":{},\"lifetime_bytes\":{},\"work_bytes\":{},\"phase_bytes\":{},\"storage_bytes\":{},\"heap_bytes\":{},\"failure_bytes\":{},\"identity_bytes\":{},\"kind_bytes\":{},\"worker_id_bytes\":{},",
                sizeof(H),offsetof(H,kind),offsetof(H,pointCount),sizeof(P),sizeof(Q::CCDRecord),
                sizeof(Q::LifetimeRecord),sizeof(Q::WorkRecord),sizeof(Q::PhaseRecord),sizeof(S),
                sizeof(E),sizeof(Q::FailureRecord),sizeof(I),sizeof(Q::EventKind),sizeof(std::uint32_t));
            data+=std::format("\"work_kind_count\":{},\"work_entry_bytes\":{},\"work_count_offset\":{},\"work_changed_offset\":{},\"work_roots_offset\":{},\"point_capacity\":{},\"ccd_capacity\":{},\"lifetime_capacity\":{},\"work_capacity\":{},\"phase_capacity\":{},\"storage_capacity\":{},\"heap_capacity\":{},\"failure_capacity\":{},\"identity_capacity\":{},",
                ::Rbs::Phase26::Sample::WorkKinds,sizeof(::Rbs::Phase26::Sample::WorkTriple),
                offsetof(::Rbs::Phase26::Sample::WorkTriple,count),
                offsetof(::Rbs::Phase26::Sample::WorkTriple,changed),
                offsetof(::Rbs::Phase26::Sample::WorkTriple,rootIterations),
                r.points.size(),r.ccd.size(),r.lifetimes.size(),r.work.size(),r.phases.size(),
                r.storage.size(),r.heap.size(),r.failures.size(),r.identities.size());
            data+=std::format("\"context_layout\":{{\"bytes\":{},\"world\":{},\"epoch\":{},\"update\":{},\"substep\":{},\"phase\":{},\"site\":{}}},",
                sizeof(C),offsetof(C,world),offsetof(C,epoch),offsetof(C,update),offsetof(C,substep),
                offsetof(C,phase),offsetof(C,site));
            data+=std::format("\"point_offsets\":{{\"body1\":{},\"subshape1\":{},\"body2\":{},\"subshape2\":{},\"ordinal\":{},\"space\":{},\"anchor1\":{},\"anchor2\":{},\"cached_normal\":{},\"solver_normal\":{},\"lambda\":{}}},",
                offsetof(P,body1),offsetof(P,subshape1),offsetof(P,body2),offsetof(P,subshape2),
                offsetof(P,ordinal),offsetof(P,space),offsetof(P,anchor1),offsetof(P,anchor2),
                offsetof(P,cached_local_normal),offsetof(P,solver_world_normal),offsetof(P,normal_lambda));
            data+=std::format("\"identity_offsets\":{{\"sdk_body_id\":{},\"managed_index\":{},\"managed_generation\":{},\"managed_registry\":{},\"action\":{}}},",
                offsetof(I,sdk_body_id),offsetof(I,managed_index),offsetof(I,managed_generation),
                offsetof(I,managed_registry),offsetof(I,action));
            data+=std::format("\"storage_offsets\":{{\"pointer\":{},\"requested\":{},\"aligned\":{},\"usage_after\":{},\"count\":{},\"action\":{}}},",
                offsetof(S,pointer),offsetof(S,requested_bytes),offsetof(S,aligned_bytes),
                offsetof(S,arena_usage_after),offsetof(S,count),offsetof(S,action));
            data+=std::format("\"heap_offsets\":{{\"pointer\":{},\"old_pointer\":{},\"requested\":{},\"old_bytes\":{},\"action\":{}}},",
                offsetof(E,pointer),offsetof(E,old_pointer),offsetof(E,requested_bytes),
                offsetof(E,old_bytes),offsetof(E,action));
            data+=std::format("\"heap_live_bytes\":{},\"heap_peak_bytes\":{},\"heap_allocation_calls\":{},\"heap_allocation_bytes\":{},\"heap_free_calls\":{},\"heap_free_bytes\":{},\"unmatched_lifetime_frees\":{},\"arena_reserve_calls\":{},\"arena_reserve_bytes\":{},\"arena_release_calls\":{},\"arena_release_bytes\":{},\"arena_high_water\":{},\"unclassified_arena_count\":{},\"recorder_valid\":{},\"sink_detached\":{},\"arena_capacities\":[",
                r.heapLiveBytes,r.heapPeakBytes,r.heapAllocationCalls,r.heapAllocationBytes,
                r.heapFreeCalls,r.heapFreeBytes,r.unmatchedLifetimeFrees,r.arenaReserveCalls,
                r.arenaReserveBytes,r.arenaReleaseCalls,r.arenaReleaseBytes,r.arenaHighWater,
                r.unclassifiedArenaCount,r.Valid()?"true":"false",Q::GetSink()==nullptr?"true":"false");
            for(std::size_t site=0;site<r.storageCapacityBytes.size();++site)
                data+=std::format("{}{{\"site\":{},\"bytes\":{},\"count\":{}}}",
                    site?",":"",site,r.storageCapacityBytes[site],r.storageCapacityCounts[site]);
            data+=" ]},\n";
            data+=std::format("  \"parity_layout\": {{\"file\":\"parity.bin\",\"schema\":1,\"frame_header_bytes\":{},\"epoch_offset\":{},\"fixture_offset\":{},\"case_id_offset\":{},\"arm_offset\":{},\"update_offset\":{},\"substeps_offset\":{},\"counts_offset\":{},\"physical_state_bytes\":{},\"impulse_state_bytes\":{},\"joint_entry_bytes\":{},",
                sizeof(ParityFrameHeader),offsetof(ParityFrameHeader,epoch),offsetof(ParityFrameHeader,fixture),
                offsetof(ParityFrameHeader,caseId),offsetof(ParityFrameHeader,arm),
                offsetof(ParityFrameHeader,update),offsetof(ParityFrameHeader,substeps),
                offsetof(ParityFrameHeader,bodyCount),sizeof(PhysicalState),sizeof(ImpulseState),sizeof(std::uint32_t));
            data+=std::format("\"physical_offsets\":{{\"id\":{},\"motion\":{},\"active\":{},\"center\":{},\"motion_bits\":{}}},\"impulse_offsets\":{{\"manifold\":{},\"body1\":{},\"body2\":{},\"point\":{},\"ccd\":{},\"roots\":{},\"floats\":{},\"accumulators\":{}}},",
                offsetof(PhysicalState,id),offsetof(PhysicalState,motion),offsetof(PhysicalState,active),
                offsetof(PhysicalState,center),offsetof(PhysicalState,motionBits),
                offsetof(ImpulseState,manifold),offsetof(ImpulseState,body1),offsetof(ImpulseState,body2),
                offsetof(ImpulseState,point),offsetof(ImpulseState,ccd),offsetof(ImpulseState,roots),
                offsetof(ImpulseState,floats),offsetof(ImpulseState,accumulators));
            data+="\"cases\":[{\"id\":1,\"name\":\"matched-floor-zero-1\"},{\"id\":2,\"name\":\"matched-floor-positive-1\"},{\"id\":3,\"name\":\"matched-floor-zero-2\"},{\"id\":4,\"name\":\"matched-floor-positive-2\"},{\"id\":5,\"name\":\"matched-floor-zero-16\"},{\"id\":6,\"name\":\"matched-floor-positive-16\"},{\"id\":7,\"name\":\"slope\"},{\"id\":8,\"name\":\"wall\"},{\"id\":9,\"name\":\"accepted-ccd\"},{\"id\":10,\"name\":\"box-patch\"},{\"id\":11,\"name\":\"speculative-zero\"},{\"id\":12,\"name\":\"sensor\"},{\"id\":101,\"name\":\"concurrent-1\"},{\"id\":102,\"name\":\"concurrent-2\"},{\"id\":116,\"name\":\"concurrent-16\"},{\"id\":201,\"name\":\"ccd-arbitration\"},{\"id\":301,\"name\":\"lifecycle\"}]},\n";
            data+=std::format("  \"synthetic_layout\": {{\"file\":\"synthetic.bin\",\"schema\":1,\"frame_header_bytes\":{},\"case_id_offset\":{},\"update_offset\":{},\"point_count_offset\":{},\"work_count_offset\":{},\"failure_count_offset\":{},\"work_bytes\":{},\"failure_bytes\":{},\"worker_id_bytes\":{},\"cases\":[{{\"id\":1,\"name\":\"two-worker-publication\"}},{{\"id\":2,\"name\":\"expected-point-overflow\"}},{{\"id\":3,\"name\":\"expected-late-update\"}},{{\"id\":4,\"name\":\"expected-wrong-owner\"}},{{\"id\":5,\"name\":\"expected-out-of-range-substep\"}},{{\"id\":6,\"name\":\"clean-reset\"}}]}}\n}}\n",
                sizeof(SyntheticFrameHeader),offsetof(SyntheticFrameHeader,caseId),
                offsetof(SyntheticFrameHeader,update),offsetof(SyntheticFrameHeader,pointCount),
                offsetof(SyntheticFrameHeader,workCount),
                offsetof(SyntheticFrameHeader,failureCount),sizeof(Q::WorkRecord),
                sizeof(Q::FailureRecord),sizeof(std::uint32_t));
            if(ObservationRaw.eventBytes+ObservationRaw.parityBytes+ObservationRaw.syntheticBytes+
                data.size()>64ull*1024*1024)return Check(false,"combined raw observer evidence within 64MiB");
            int fd=-1;
            const auto open=_sopen_s(&fd,target.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,
                _SH_DENYWR,_S_IREAD|_S_IWRITE);
            if(open!=0||fd<0)
            {std::println(stderr,"PHASE26_OBSERVER_RESULT_FAIL create-only result path");return false;}
            const auto written=_write(fd,data.data(),static_cast<unsigned>(data.size()));
            const auto closed=_close(fd);
            return Check(written==static_cast<int>(data.size())&&closed==0,
                "create-only observer result complete write");
        }
        inline int ObservationCorrectness()
        {
            Log::Initialize();CompletedCheckTicks=0;ObservationActual={};ObservationDetachOkay=true;
            ObservationResult result;
            const bool rawOpen=ObservationRaw.Open();
            Check(rawOpen,"three create-only observer evidence streams opened before first Update");
            do
            {
                if(!rawOpen)break;
                result.abi=JPH::QualificationObservation::kObservationAbiSentinel==
                    JPH::QualificationObservation::kExpectedObservationAbiSentinel;
                if(!Check(result.abi,"diagnostic SDK ABI sentinel"))break;
                result.direct=ObservationDirectChecks();if(!result.direct)break;
                result.ccd=ObservationArbitrationChecks();if(!result.ccd)break;
                result.concurrent=ObservationConcurrentChecks();if(!result.concurrent)break;
                result.lifecycle=ObservationLifecycleChecks();if(!result.lifecycle)break;
                result.synthetic=ObservationSyntheticChecks();if(!result.synthetic)break;
                result.parity=result.direct&&result.ccd&&result.concurrent&&result.lifecycle;
                result.witness=result.direct&&result.ccd&&result.concurrent&&result.lifecycle;
                const auto& recorder=Phase26JoltRecorder::Get();
                result.allocation=result.direct&&result.concurrent&&ObservationActual.arenaPeak<=32*1024*1024&&
                    recorder.arenaHighWater<=32*1024*1024&&
                    recorder.arenaReserveBytes==recorder.arenaReleaseBytes&&
                    recorder.unclassifiedArenaCount==0&&recorder.heapLiveBytes==0&&
                    recorder.unmatchedLifetimeFrees==0;
                result.teardown=ObservationDetachOkay&&JPH::Factory::sInstance==nullptr&&
                    JPH::QualificationObservation::GetSink()==nullptr&&Phase26JoltRecorder::Get().Valid();
                if(!Check(result.teardown,"all diagnostic sink, SDK and allocator ownership retired"))break;
                result.complete=CompletedCheckTicks==88&&result.parity&&result.witness&&result.allocation;
                if(!Check(result.complete,"exact 88 planned Updates within 120 hard cap and 32MiB arena"))break;
            }while(false);
            const bool rawClosed=ObservationRaw.Close();
            result.raw=rawClosed&&ObservationRaw.updateFrames==44&&
                ObservationRaw.parityFrames==88&&ObservationRaw.offFrameCount==44&&
                ObservationRaw.syntheticFrames==6;
            Check(result.raw,"complete raw typed, parity and separately labeled synthetic evidence");
            result.complete &= result.raw;
            std::println("PHASE26_OBSERVER_PROCESS_END updates={} complete={} direct={} ccd={} concurrent={} lifecycle={} synthetic={} teardown={}",
                CompletedCheckTicks,int(result.complete),int(result.direct),int(result.ccd),
                int(result.concurrent),int(result.lifecycle),int(result.synthetic),int(result.teardown));
            if(!WriteObservationResult(result))return 1;
            return result.complete?0:1;
        }
#if defined(GENGINE_PHASE26_CONTACT_REGRESSION)
        // Q7 is an isolated test profile. The unchanged backend-check dispatch
        // reaches this branch; every Physics tick still belongs to _Scene.
        struct ContactRegressionCapture
        {
            enum Stream : unsigned {Initial,Body,Contact,Metric,Frame,Impulse,Episode,Pair,StreamCount};
            std::array<FILE*,StreamCount> file{};
            std::array<PhysicsBodyHandle,5> handles{};
            std::array<PhysicsBodyState,5> bodies{},previous{};
            std::array<std::uint32_t,5> bodyContacts{},tailQuiet{};
            std::array<double,5> bodyDepth{};
            _Scene* scene{};
            PhysicsWorld* world{};
            Phase26Observation::Observer observer{};
            std::uint64_t epoch{},textBytes{};
            unsigned episode{},tick{},count{},completedEpisodes{};
            double tailMinY{},tailMaxY{};
            const char* failure{};
            char line[2048]{};
            std::filesystem::path output;
            bool good{true};
            bool Fail(const char* why) noexcept
            {if(!failure)failure=why;good=false;return false;}
            template<class... T> bool Row(Stream stream,const char* format,T... values) noexcept
            {
                const int n=std::snprintf(line,sizeof(line),format,values...);
                if(n<0||static_cast<std::size_t>(n)>=sizeof(line)||!file[stream]||
                    textBytes+ObservationRaw.eventBytes+std::uint64_t(n)>63ull*1024*1024)
                    return Fail("Q7 bounded output row");
                if(std::fwrite(line,1,static_cast<std::size_t>(n),file[stream])!=static_cast<std::size_t>(n))
                    return Fail("Q7 output write");
                textBytes+=std::uint64_t(n);return true;
            }
            bool RawRoom() noexcept
            {
                // The entire existing Recorder is an upper bound for one raw
                // frame's arrays, counts and worker columns; reserve before writing.
                return textBytes+ObservationRaw.eventBytes+sizeof(Phase26JoltRecorder::Recorder)+
                    sizeof(::Rbs::Phase26::JoltFrameHeader)<=63ull*1024*1024||Fail("Q7 combined export capacity");
            }
            bool Open()
            {
                char* raw=nullptr;std::size_t size{};
                const auto error=_dupenv_s(&raw,&size,"GENGINE_PHASE26_OBSERVATION_OUTPUT");
                const std::unique_ptr<char,decltype(&std::free)> owned(raw,&std::free);
                if(error||!owned||size<2)return Fail("Q7 exact output directory");
                output=std::filesystem::path(owned.get());
                if(!output.is_absolute()||!std::filesystem::is_directory(output))return Fail("Q7 output directory admission");
                constexpr const char* names[]{"initial.csv","bodies.csv","contacts.csv","metrics.csv","frames.csv","impulse.csv","episodes.csv","pairs.csv"};
                for(unsigned i=0;i<StreamCount;++i)if(!(file[i]=ObservationRawExport::Create(output/names[i])))return Fail("Q7 create-only streams");
                if(!ObservationRaw.Open())return Fail("Q7 create-only raw observer stream");
                return Row(Initial,"episode,index,slot,generation,registry,shape,type,inverse_mass,elasticity,friction,layer,mask,radius,px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,spin_length,rolling_length,quiet_linear,quiet_angular,quiet_dwell,ixx,iyy,izz\n")&&
                    Row(Body,"episode,tick,index,slot,generation,registry,px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,sleeping,contacts,penetration\n")&&
                    Row(Contact,"episode,tick,a,b,a_slot,a_generation,a_registry,b_slot,b_generation,b_registry,subshape_a,subshape_b,ordinal,ax,ay,az,bx,by,bz,nx,ny,nz,depth,lax,lay,laz,lbx,lby,lbz\n")&&
                    Row(Metric,"episode,tick,bodies,contacts,sleeping,quiet,wakes,sleeps,linear,angular,drift,kinetic,potential,energy,penetration,capacity\n")&&
                    Row(Frame,"episode,tick,elapsed,pending,discarded,total_steps,steps_last_update,alpha\n")&&
                    Row(Impulse,"episode,after_tick,sleeping_before,awake_after,energy_before,energy_after,impulse_x\n")&&
                    Row(Episode,"episode,name,ticks,bodies,hz,passes,sleep\n")&&
                    Row(Pair,"episode,tick,a,b,depth\n");
            }
            bool Read() noexcept
            {
                if(!world||world->GetBodyCount()!=count||world->GetJointCount())return Fail("Q7 exact live membership");
                for(unsigned i=0;i<count;++i){
                    auto state=world->ReadBody(handles[i]);
                    if(!state||state->handle!=handles[i]||!world->IsBodyValid(handles[i]))return Fail("Q7 authoritative live body state");
                    bodies[i]=*state;
                }
                return bodies[0].position==Vec3f(0)&&bodies[0].orientation==Quat(1,0,0,0)||Fail("Q7 static ground drift");
            }
            static double Kinetic(const PhysicsBodyState& b) noexcept
            {
                if(b.inverseMass<=0)return 0;
                const auto rotation=glm::dmat3(glm::mat3_cast(b.orientation));
                const auto angular=glm::transpose(rotation)*glm::dvec3(b.angularVelocity);
                const auto inertia=glm::inverse(glm::dmat3(b.inverseInertiaBody));
                return .5/double(b.inverseMass)*glm::dot(glm::dvec3(b.linearVelocity),glm::dvec3(b.linearVelocity))+
                    .5*glm::dot(angular,inertia*angular);
            }
            static double Potential(const PhysicsBodyState& b) noexcept
            {return b.inverseMass>0?12.*double(b.position.y)/double(b.inverseMass):0;}
            unsigned Find(std::uint32_t id) const noexcept
            {
                const auto& r=Phase26JoltRecorder::Get();unsigned found=count;
                for(unsigned j=0;j<r.identityCount.load()&&j<r.identities.size();++j){
                    const auto& m=r.identities[j];
                    if(m.action!=JPH::QualificationObservation::IdentityAction::Snapshot||m.sdk_body_id!=id)continue;
                    for(unsigned i=0;i<count;++i)if(handles[i].index==m.managed_index&&handles[i].generation==m.managed_generation&&handles[i].registry==m.managed_registry){
                        if(found!=count&&found!=i)return count;found=i;
                    }
                }
                return found;
            }
            bool BoxPairDepth(const PhysicsBodyState& a,const PhysicsBodyState& b,double& depth) noexcept
            {
                // Independent geometric witness for the authored two-unit cubes.
                // SAT uses six face normals and nine edge cross products; an
                // exactly zero cross axis is redundant, never an epsilon waiver.
                const auto ra=glm::dmat3(glm::mat3_cast(a.orientation));
                const auto rb=glm::dmat3(glm::mat3_cast(b.orientation));
                const auto delta=glm::dvec3(b.position)-glm::dvec3(a.position);
                depth=std::numeric_limits<double>::max();
                for(unsigned axis=0;axis<15;++axis){
                    glm::dvec3 direction;
                    if(axis<3)direction=glm::cross(ra[(axis+1)%3],ra[(axis+2)%3]);
                    else if(axis<6){const auto k=axis-3;direction=glm::cross(rb[(k+1)%3],rb[(k+2)%3]);}
                    else direction=glm::cross(ra[(axis-6)/3],rb[(axis-6)%3]);
                    const double squared=glm::dot(direction,direction);
                    if(!std::isfinite(squared))return Fail("Q7 finite OBB axis");
                    if(squared==0){if(axis<6)return Fail("Q7 degenerate box frame");continue;}
                    direction/=std::sqrt(squared);
                    double radius=0;
                    for(unsigned k=0;k<3;++k)radius+=std::abs(glm::dot(ra[k],direction))+std::abs(glm::dot(rb[k],direction));
                    const double overlap=radius-std::abs(glm::dot(delta,direction));
                    if(!std::isfinite(overlap))return Fail("Q7 finite independent pair overlap");
                    if(overlap<0){depth=0;return true;}
                    depth=(std::min)(depth,overlap);
                }
                return std::isfinite(depth)&&depth>=0||Fail("Q7 finite independent pair depth");
            }
            bool Capture() noexcept
            {
                namespace Q=JPH::QualificationObservation;
                auto& r=Phase26JoltRecorder::Get();if(!Read())return false;
                bodyContacts.fill(0);bodyDepth.fill(0);
                double penetration=0,kinetic=0,potential=0,linear=0,angular=0,drift=0;
                unsigned contacts=0,sleeping=0,quiet=0,wakes=0,sleeps=0;
                for(unsigned i=1;i<count;++i){
                    const auto& b=bodies[i];kinetic+=Kinetic(b);potential+=Potential(b);
                    const double v=glm::length(glm::dvec3(b.linearVelocity)),w=glm::length(glm::dvec3(b.angularVelocity));
                    linear=(std::max)(linear,v);angular=(std::max)(angular,w);
                    const Vec3f initial(0,episode==0?1.5f+2.01f*float(i-1):1.5f,0);
                    drift=(std::max)(drift,glm::length(glm::dvec3(b.position)-glm::dvec3(initial)));
                    sleeping+=unsigned(b.sleeping);quiet+=unsigned(b.sleeping||(v<=double(.05f)&&w<=double(.02f)));
                    wakes+=unsigned(previous[i].sleeping&&!b.sleeping);sleeps+=unsigned(!previous[i].sleeping&&b.sleeping);
                    // Exact legacy ground-support observation, independent of
                    // callback/contact presence, including sleeping bodies.
                    if(std::abs(b.position.x)<49&&std::abs(b.position.z)<49){
                        double bottom=double(b.position.y)-1.;
                        if(episode==0){bottom=std::numeric_limits<double>::max();
                            for(int x:{-1,1})for(int y:{-1,1})for(int z:{-1,1})
                                bottom=(std::min)(bottom,double((b.position+b.orientation*Vec3f(float(x),float(y),float(z))).y));}
                        bodyDepth[i]=(std::max)(0.,.5-bottom);penetration=(std::max)(penetration,bodyDepth[i]);
                    }
                }
                if(episode==0)for(unsigned a=1;a<count;++a)for(unsigned b=a+1;b<count;++b){
                    double depth{};if(!BoxPairDepth(bodies[a],bodies[b],depth))return false;
                    bodyDepth[a]=(std::max)(bodyDepth[a],depth);bodyDepth[b]=(std::max)(bodyDepth[b],depth);
                    penetration=(std::max)(penetration,depth);
                    if(!Row(Pair,"%u,%u,%u,%u,%.17g\n",episode,tick,a,b,depth))return false;
                }
                for(unsigned j=0;j<r.pointCount.load()&&j<r.points.size();++j){
                    const auto& p=r.points[j];const auto kind=r.pointKind[j];
                    if(kind==Q::EventKind::CCDManifoldPoint&&p.space==Q::AnchorSpace::WorldAtImpact)continue;
                    if(p.space!=Q::AnchorSpace::COMLocal||p.context.epoch!=epoch||p.context.update!=tick||p.context.substep||
                        p.context.world!=r.sink.world||(kind==Q::EventKind::CCDManifoldPoint&&p.ordinal!=UINT32_MAX))return Fail("Q7 accepted witness context");
                    const unsigned a=Find(p.body1),b=Find(p.body2);
                    if(a>=count||b>=count||a==b||++contacts>32)return Fail("Q7 accepted witness identity or existing32point cap");
                    const glm::dvec3 la(p.anchor1[0],p.anchor1[1],p.anchor1[2]),lb(p.anchor2[0],p.anchor2[1],p.anchor2[2]);
                    const glm::dvec3 n(p.solver_world_normal[0],p.solver_world_normal[1],p.solver_world_normal[2]);
                    const auto pa=glm::dvec3(bodies[a].centerOfMass)+glm::dquat(bodies[a].orientation)*la;
                    const auto pb=glm::dvec3(bodies[b].centerOfMass)+glm::dquat(bodies[b].orientation)*lb;
                    const double depth=glm::dot(pa-pb,n);
                    if(!std::isfinite(depth))return Fail("Q7 finite accepted witness");
                    ++bodyContacts[a];++bodyContacts[b];bodyDepth[a]=(std::max)(bodyDepth[a],depth);bodyDepth[b]=(std::max)(bodyDepth[b],depth);
                    penetration=(std::max)(penetration,depth);
                    if(!Row(Contact,"%u,%u,%u,%u,%u,%llu,%llu,%u,%llu,%llu,%u,%u,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
                        episode,tick,a,b,handles[a].index,static_cast<unsigned long long>(handles[a].generation),static_cast<unsigned long long>(handles[a].registry),
                        handles[b].index,static_cast<unsigned long long>(handles[b].generation),static_cast<unsigned long long>(handles[b].registry),p.subshape1,p.subshape2,p.ordinal,
                        pa.x,pa.y,pa.z,pb.x,pb.y,pb.z,n.x,n.y,n.z,depth,la.x,la.y,la.z,lb.x,lb.y,lb.z))return false;
                }
                for(unsigned i=0;i<count;++i){const auto& b=bodies[i];
                    if(!Row(Body,"%u,%u,%u,%u,%llu,%llu,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%u,%u,%.17g\n",
                        episode,tick,i,handles[i].index,static_cast<unsigned long long>(handles[i].generation),static_cast<unsigned long long>(handles[i].registry),
                        double(b.position.x),double(b.position.y),double(b.position.z),double(b.orientation.w),double(b.orientation.x),double(b.orientation.y),double(b.orientation.z),
                        double(b.linearVelocity.x),double(b.linearVelocity.y),double(b.linearVelocity.z),double(b.angularVelocity.x),double(b.angularVelocity.y),double(b.angularVelocity.z),
                        unsigned(b.sleeping),bodyContacts[i],bodyDepth[i]))return false;
                }
                if(!std::isfinite(kinetic)||!std::isfinite(potential))return Fail("Q7 finite energy");
                if(!Row(Metric,"%u,%u,%u,%u,%u,%u,%u,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%llu\n",
                    episode,tick,count,contacts,sleeping,quiet,wakes,sleeps,linear,angular,drift,kinetic,potential,kinetic+potential,penetration,
                    static_cast<unsigned long long>(r.arenaHighWater)))return false;
                const unsigned limit=episode==0?1200:600;
                if(tick>limit-120){
                    if(penetration>.035)return Fail("Q7 frozen final support penetration");
                    for(unsigned i=1;i<count;++i){const auto& b=bodies[i];
                        const bool isQuiet=b.sleeping||(glm::length(glm::dvec3(b.linearVelocity))<=double(.05f)&&
                            glm::length(glm::dvec3(b.angularVelocity))<=double(.02f));
                        tailQuiet[i]+=unsigned(isQuiet);
                        if(episode==1){
                            if(tick==limit-119)tailMinY=tailMaxY=double(b.position.y);
                            tailMinY=(std::min)(tailMinY,double(b.position.y));tailMaxY=(std::max)(tailMaxY,double(b.position.y));
                            if(!isQuiet||tailMaxY-tailMinY>.02||std::abs(b.linearVelocity.y)>.05)return Fail("Q7 frozen sphere quiet height speed");
                        }
                    }
                }
                if(tick==limit&&episode==0)for(unsigned i=1;i<count;++i)
                    if(tailQuiet[i]<108)return Fail("Q7 frozen stack quiet fraction");
                if(episode==1&&tick==301&&bodies[1].sleeping)return Fail("Q7 frozen next-tick wake");
                previous=bodies;return true;
            }
            void Arm() noexcept
            {
                observer.context=this;
                observer.begin=[](void* context) noexcept {auto& s=*static_cast<ContactRegressionCapture*>(context);
                    if(!s.good||CompletedCheckTicks>=1800||!s.RawRoom()||!ObservationRaw.BeforeUpdate(s.epoch)||
                        !Phase26JoltRecorder::Get().Prepare(nullptr,s.epoch,s.tick+1,1))s.Fail("Q7 observation begin");
                    ObservationRaw.ResetDeltas();};
                observer.end=[](void* context,std::uint64_t) noexcept {auto& s=*static_cast<ContactRegressionCapture*>(context);
                    ++s.tick;++CompletedCheckTicks;
                    const bool observed=Phase26JoltRecorder::Get().FinishUpdate();
                    if(!s.RawRoom()||!ObservationRaw.AfterUpdate(s.epoch,s.tick,1))s.Fail("Q7 raw completed update");
                    if(!observed||!s.scene||s.scene->GetPhysicsSystem()->GetCompletedSteps()!=s.tick)s.Fail("Q7 completed update identity");
                    if(s.good)s.Capture();};
                Phase26Observation::observer=&observer;
            }
            bool CaptureMarker(unsigned limit)
            {
                constexpr const char* names[]{"initial.csv","bodies.csv","contacts.csv","metrics.csv","frames.csv","impulse.csv","episodes.csv","pairs.csv"};
                if(textBytes+ObservationRaw.eventBytes+2048>63ull*1024*1024)return Fail("Q7 marker capacity");
                for(auto* stream:file)if(!stream||std::fflush(stream)!=0||_ftelli64(stream)<0)return Fail("Q7 durable stream flush");
                if(!ObservationRaw.events||std::fflush(ObservationRaw.events)!=0||_ftelli64(ObservationRaw.events)<0)return Fail("Q7 durable raw flush");
                FILE* markerFile=ObservationRawExport::Create(output/("case-"+std::to_string(episode)+"-capture.json"));
                if(!markerFile)return Fail("Q7 create-only case marker");
                std::int64_t bytes=std::fprintf(markerFile,"{\"schema\":1,\"episode\":%u,\"ticks\":%u,\"expected_ticks\":%u,\"name\":\"%s\",\"hz\":60,\"rendered_application_scene\":false,\"capture_complete\":true,\"qualification_pass\":false,\"application_shutdown_observed\":false,\"stream_end_offsets\":{",
                    episode,tick,limit,episode==0?"stack60":"wake60");
                bool okay=bytes>0;
                for(unsigned i=0;i<StreamCount;++i){
                    const int n=std::fprintf(markerFile,"%s\"%s\":%lld",i?",":"",names[i],static_cast<long long>(_ftelli64(file[i])));
                    okay &= n>0;if(n>0)bytes+=n;
                }
                const int n=std::fprintf(markerFile,",\"observer.bin\":%lld}}\n",static_cast<long long>(_ftelli64(ObservationRaw.events)));
                okay &= n>0;if(n>0)bytes+=n;
                const bool closed=std::fclose(markerFile)==0;
                if(!okay||!closed||bytes<0||bytes>2048)return Fail("Q7 completed case marker write");
                textBytes+=std::uint64_t(bytes);return true;
            }
            bool RunEpisode(unsigned which)
            {
                episode=which;tick=0;count=which==0?5:2;epoch=0x26007000ull+which+1;
                tailQuiet.fill(0);tailMinY=tailMaxY=0;
                {auto& lifetimeRecorder=Phase26JoltRecorder::Get();
                    if(!lifetimeRecorder.Valid()||lifetimeRecorder.heapLiveBytes||JPH::QualificationObservation::GetSink())return Fail("Q7 clean episode entry");
                    lifetimeRecorder.ResetUpdate();ObservationRaw.BeginEpoch(epoch);
                    if(!lifetimeRecorder.InstallLifetime(epoch))return Fail("Q7 epoch sink ownership");}
                _Scene local;scene=&local;
                struct Retire {ContactRegressionCapture& s;_Scene& scene;~Retire(){
                    Phase26Observation::observer=nullptr;scene.OnRuntimeStop();s.world=nullptr;s.scene=nullptr;
                    auto& r=Phase26JoltRecorder::Get();
                    if(JPH::QualificationObservation::GetSink()==&r.sink&&!r.Detach())s.Fail("Q7 early sink retirement");
                    if(!r.ProcessHeap()||!r.ProcessCapacity()||r.heapLiveBytes||r.baseJobPoolPointer||
                        !r.Valid()||JPH::QualificationObservation::GetSink()!=nullptr)s.Fail("Q7 managed episode teardown");
                    if(!s.RawRoom()||!ObservationRaw.EndEpoch(s.epoch))s.Fail("Q7 raw teardown export");
                }} retire{*this,local};
                if(!local.ConfigurePhysicsBackend(PhysicsBackend::Jolt))return Fail("Q7 Jolt Scene selection");
                std::array<std::optional<_Entity>,5> entities{};
                for(unsigned n=count;n>0;--n){const unsigned i=n-1;
                    const auto created=local.CreateEntity("Q7 "+std::to_string(which)+" "+std::to_string(i));
                    if(!created)return Fail("Q7 fixture entity creation");auto e=*created;entities[i].emplace(e);
                    auto& transform=e.GetComponent<Transform3DComponent>();
                    transform.Translation=i?Vec3f(0,which==0?1.5f+2.01f*float(i-1):1.5f,0):Vec3f(0);
                    transform.QuatRotation=Quat(1,0,0,0);transform.Scale=i?Vec3f(2):Vec3f(100,1,100);
                    RigidBody3DComponent rb;rb.Type=i?BodyType::Dynamic:BodyType::Static;rb.CollisionLayer=1;rb.CollisionMask=~std::uint32_t(0);
                    e.AddOrReplaceComponent<RigidBody3DComponent>(rb);
                    Fixture3DProperty property;property.m_Position=transform.Translation;property.m_Orientation=transform.QuatRotation;
                    property.m_InvMass=i?1.f:0.f;property.m_Elasticity=property.m_Friction=.5f;
                    if(which==1&&i){transform.Scale=Vec3f(1);SphereFixture3DComponent fixture;fixture.Radius=1;fixture.Property=property;
                        e.AddOrReplaceComponent<SphereFixture3DComponent>(fixture);}
                    else{BoxFixture3DComponent fixture;fixture.Shape=BoxShapeDesc{};fixture.Property=property;e.AddOrReplaceComponent<BoxFixture3DComponent>(fixture);}
                }
                if(!local.OnRuntimeStart())return Fail("Q7 Scene runtime start");
                auto* system=local.GetPhysicsSystem();world=system->GetPhysicsWorld();
                if(!world||world->GetBackend()!=PhysicsBackend::Jolt||system->GetSolverIterations()!=1||!system->IsSleepingEnabled()||
                    local.GetPhysicsStep().count()!=1.0/60.0||world->GetBodyCount()!=count)return Fail("Q7 frozen policy or membership");
                world->SetGravity(Vec3f(0,-12,0));
                for(unsigned i=0;i<count;++i){auto binding=local.GetBodyBinding(*entities[i]);auto ordinal=world->BodyAt(i);
                    if(!binding||!ordinal||*binding!=*ordinal)return Fail("Q7 exact creation order");handles[i]=*binding;
                    auto b=world->ReadBody(handles[i]);if(!b)return Fail("Q7 initial readback");
                    auto properties=world->ReadBodyProperties(handles[i]);
                    if(!properties)return Fail("Q7 initial property readback");
                    properties->material.spinResistanceLength=properties->material.rollingResistanceLength=.05f;
                    // Complete the authored initialization before arming the step observer.
                    // The existing synchronous transaction retains its construction heap evidence.
                    if(!world->UpdateBodyProperties(handles[i],*properties))return Fail("Q7 synchronous authored resistance");
                    const auto actual=world->ReadBodyProperties(handles[i]);const auto after=world->ReadBody(handles[i]);
                    if(!actual||*actual!=*properties||!after||system->GetCompletedSteps()!=0||
                        after->handle!=b->handle||after->type!=b->type||after->position!=b->position||
                        after->orientation!=b->orientation||after->linearVelocity!=b->linearVelocity||
                        after->angularVelocity!=b->angularVelocity||after->inverseMass!=b->inverseMass||
                        after->material!=properties->material||after->shapeType!=b->shapeType||
                        after->localBounds.mins!=b->localBounds.mins||after->localBounds.maxs!=b->localBounds.maxs||
                        after->worldBounds.mins!=b->worldBounds.mins||after->worldBounds.maxs!=b->worldBounds.maxs||
                        after->centerOfMass!=b->centerOfMass||after->inverseInertiaBody!=b->inverseInertiaBody||
                        after->inverseInertiaWorld!=b->inverseInertiaWorld||after->sleep!=b->sleep||
                        after->inactiveSeconds!=b->inactiveSeconds||after->sleeping!=b->sleeping)
                        return Fail("Q7 initialization changed another body property or state");
                }
                local.SetPaused(false);local.Update(Timestep(0.0));
                if(system->GetCompletedSteps()!=0||!Read())return Fail("Q7 zero-tick initial synchronization");
                previous=bodies;
                for(unsigned i=0;i<count;++i){const auto& b=bodies[i];
                    const Vec3f position=i?Vec3f(0,which==0?1.5f+2.01f*float(i-1):1.5f,0):Vec3f(0);
                    const auto& authored=entities[i]->GetComponent<RigidBody3DComponent>();
                    if(b.type!=(i?BodyType::Dynamic:BodyType::Static)||b.inverseMass!=(i?1.f:0.f)||
                        b.position!=position||b.orientation!=Quat(1,0,0,0)||b.linearVelocity!=Vec3f(0)||b.angularVelocity!=Vec3f(0)||
                        b.material.friction!=.5f||b.material.restitution!=.5f||b.material.spinResistanceLength!=.05f||b.material.rollingResistanceLength!=.05f||
                        authored.CollisionLayer!=1||authored.CollisionMask!=~std::uint32_t(0))return Fail("Q7 exact authored initial fields");
                    const auto inertia=b.inverseMass>0?glm::inverse(glm::dmat3(b.inverseInertiaBody)):glm::dmat3(0);
                    if(!Row(Initial,"%u,%u,%u,%llu,%llu,%u,%u,%.17g,%.17g,%.17g,%u,%u,1,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,.5,%.17g,%.17g,%.17g\n",
                        which,i,handles[i].index,static_cast<unsigned long long>(handles[i].generation),static_cast<unsigned long long>(handles[i].registry),unsigned(which==0||i==0),unsigned(i!=0),
                        double(b.inverseMass),double(b.material.restitution),double(b.material.friction),1u,~std::uint32_t(0),
                        double(b.position.x),double(b.position.y),double(b.position.z),double(b.orientation.w),double(b.orientation.x),double(b.orientation.y),double(b.orientation.z),
                        double(b.linearVelocity.x),double(b.linearVelocity.y),double(b.linearVelocity.z),double(b.angularVelocity.x),double(b.angularVelocity.y),double(b.angularVelocity.z),
                        double(b.material.spinResistanceLength),double(b.material.rollingResistanceLength),double(.05f),double(.02f),inertia[0][0],inertia[1][1],inertia[2][2]))return false;
                }
                Arm();const unsigned limit=which==0?1200:600;
                for(unsigned expected=1;expected<=limit&&good;++expected){
                    if(CompletedCheckTicks>=1800||!RawRoom())return Fail("Q7 aggregate tick or output cap");
                    local.Update(Timestep(1.0/60.0));
                    const auto timing=local.GetPhysicsTiming();
                    if(tick!=expected||timing.totalSteps!=expected||timing.stepsLastUpdate!=1||
                        !std::isfinite(timing.discardedSeconds)||timing.discardedSeconds<0||system->GetBackendFailure()||system->HasJointFailure())return Fail("Q7 scheduler or Physics completion");
                    if(!Row(Frame,"%u,%u,%.17g,%.17g,%.17g,%llu,%u,%.17g\n",which,tick,1.0/60.0,timing.pendingSeconds,timing.discardedSeconds,
                        static_cast<unsigned long long>(timing.totalSteps),timing.stepsLastUpdate,double(local.GetRenderInterpolationAlpha())))return false;
                    if(!good)return false;
                    if(which==1&&tick==300){
                        const auto before=bodies[1];if(!before.sleeping)return Fail("Q7 wake sleeping premise at300");
                        if(!world->ApplyLinearImpulse(handles[1],Vec3f(.5f,0,0)))return Fail("Q7 exact central impulse");
                        const auto after=world->ReadBody(handles[1]);if(!after||after->sleeping)return Fail("Q7 immediate commanded wake");
                        if(!Row(Impulse,"1,300,1,1,%.17g,%.17g,0.5\n",Kinetic(before)+Potential(before),Kinetic(*after)+Potential(*after)))return false;
                    }
                }
                Phase26Observation::observer=nullptr;
                if(!good||tick!=limit||!Row(Episode,"%u,%s,%u,%u,60,1,1\n",which,which==0?"stack60":"wake60",tick,count))return false;
                if(!CaptureMarker(limit))return false;
                ++completedEpisodes;return true;
            }
            bool Close() noexcept
            {
                for(auto& stream:file)if(stream){if(std::fclose(stream)!=0)Fail("Q7 stream close");stream=nullptr;}
                if(!ObservationRaw.Close())Fail("Q7 raw stream close");return good;
            }
        };
        inline constexpr std::size_t ContactRegressionRecorderBytes=CombinedRecorderBytes+sizeof(ContactRegressionCapture)+sizeof(_Scene)+sizeof(std::array<std::optional<_Entity>,5>)+sizeof(std::uint64_t)+sizeof(std::string)+16384;
        static_assert(ContactRegressionRecorderBytes<=8*1024*1024,
            "Q7 combined fixed recorder exceeds unchanged8MiB limit");
        extern "C" {
            // Read this emitted data symbol from the RBS COFF object before any
            // Q7 process. Volatile prevents folding the runtime report away.
            __declspec(selectany) extern const volatile std::uint64_t GEnginePhase26Q7RecorderBytes=ContactRegressionRecorderBytes;
        }
        inline int ContactRegression()
        {
            Log::Initialize();CompletedCheckTicks=0;
            auto capture=std::make_unique<ContactRegressionCapture>();
            const bool opened=capture->Open();
            if(opened&&capture->RunEpisode(0)&&capture->good)capture->RunEpisode(1);
            const bool closed=capture->Close();
            const auto& r=Phase26JoltRecorder::Get();
            const bool complete=closed&&capture->good&&capture->completedEpisodes==2&&CompletedCheckTicks==1800&&
                ObservationRaw.updateFrames==1800&&r.heapLiveBytes==0&&r.arenaReserveBytes==r.arenaReleaseBytes&&
                r.unclassifiedArenaCount==0&&r.heapOwnershipFailures==0&&JPH::Factory::sInstance==nullptr&&JPH::QualificationObservation::GetSink()==nullptr;
            FILE* result=ObservationRawExport::Create(capture->output/"result.json");
            if(!result)return 1;
            const auto bytes=std::snprintf(capture->line,sizeof(capture->line),"{\"schema\":\"phase26-jolt-contact-regression-01\",\"status\":\"%s\",\"complete\":%s,\"updates\":%llu,\"episodes\":%u,\"failure\":\"%s\",\"recorder_bytes\":%zu,\"raw_bytes\":%llu,\"text_bytes\":%llu,\"native_inactivity_timer\":\"NOT_EXPORTED_NOT_JOLT_STATE\",\"quiet_thresholds\":\"frozen0.05f_and0.02f_separate_from_native_Jolt_sleep_test\",\"contact_population\":\"actual_accepted_contacts_no_fabricated_sleeping_cache\",\"pair_geometry\":\"15axis_OBB_all6_dynamic_cube_pairs_per_stack_tick_no_new_threshold\",\"numerical_qualification\":\"PENDING_UNCHANGED_OFFLINE_PREDICATES\"}\n",
                complete?"CAPTURE_COMPLETE_ANALYSIS_PENDING":"FAIL",complete?"true":"false",static_cast<unsigned long long>(CompletedCheckTicks),capture->completedEpisodes,
                capture->failure?capture->failure:"",static_cast<std::size_t>(GEnginePhase26Q7RecorderBytes),
                static_cast<unsigned long long>(ObservationRaw.eventBytes),static_cast<unsigned long long>(capture->textBytes));
            namespace Q=JPH::QualificationObservation;
            bool written=bytes>2&&static_cast<std::size_t>(bytes)<sizeof(capture->line);
            std::string data;
            if(written){
                data.assign(capture->line,static_cast<std::size_t>(bytes)-2);data+=",\n";
            data+=std::format("  \"heap_source_bounds\": {{\"schema\":1,\"base_job_pool_bytes\":{},\"base_job_slot_bytes\":{},\"base_job_slots\":{},\"broadphase_node_bytes\":{},\"broadphase_center_bytes\":{},\"body_slot_count_max\":{},\"base_job_pool_requests\":{},\"ownership_validated_updates\":{},\"ownership_failures\":{}}},\n",
                Q::kBaseJobPoolBytes,Q::kBaseJobSlotBytes,JPH::cMaxPhysicsJobs,Q::kBroadPhaseNodeBytes,
                Q::kBroadPhaseCenterBytes,r.heapBodySlotsMax,r.baseJobPoolTotalRequests,
                r.heapValidatedUpdates,r.heapOwnershipFailures);
            using H=::Rbs::Phase26::JoltFrameHeader;
            using P=Q::PointRecord;using I=Q::IdentityRecord;using S=Q::StorageRecord;
            using E=Q::HeapRecord;using C=Q::Context;
            data+=std::format("  \"jolt_observation_layout\": {{\"file\":\"observer.bin\",\"schema\":2,\"frame_header_bytes\":{},\"frame_kind_offset\":{},\"frame_counts_offset\":{},\"point_bytes\":{},\"ccd_bytes\":{},\"lifetime_bytes\":{},\"work_bytes\":{},\"phase_bytes\":{},\"storage_bytes\":{},\"heap_bytes\":{},\"failure_bytes\":{},\"identity_bytes\":{},\"kind_bytes\":{},\"worker_id_bytes\":{},",
                sizeof(H),offsetof(H,kind),offsetof(H,pointCount),sizeof(P),sizeof(Q::CCDRecord),
                sizeof(Q::LifetimeRecord),sizeof(Q::WorkRecord),sizeof(Q::PhaseRecord),sizeof(S),
                sizeof(E),sizeof(Q::FailureRecord),sizeof(I),sizeof(Q::EventKind),sizeof(std::uint32_t));
            data+=std::format("\"work_kind_count\":{},\"work_entry_bytes\":{},\"work_count_offset\":{},\"work_changed_offset\":{},\"work_roots_offset\":{},\"point_capacity\":{},\"ccd_capacity\":{},\"lifetime_capacity\":{},\"work_capacity\":{},\"phase_capacity\":{},\"storage_capacity\":{},\"heap_capacity\":{},\"failure_capacity\":{},\"identity_capacity\":{},",
                ::Rbs::Phase26::Sample::WorkKinds,sizeof(::Rbs::Phase26::Sample::WorkTriple),
                offsetof(::Rbs::Phase26::Sample::WorkTriple,count),
                offsetof(::Rbs::Phase26::Sample::WorkTriple,changed),
                offsetof(::Rbs::Phase26::Sample::WorkTriple,rootIterations),
                r.points.size(),r.ccd.size(),r.lifetimes.size(),r.work.size(),r.phases.size(),
                r.storage.size(),r.heap.size(),r.failures.size(),r.identities.size());
            data+=std::format("\"context_layout\":{{\"bytes\":{},\"world\":{},\"epoch\":{},\"update\":{},\"substep\":{},\"phase\":{},\"site\":{}}},",
                sizeof(C),offsetof(C,world),offsetof(C,epoch),offsetof(C,update),offsetof(C,substep),
                offsetof(C,phase),offsetof(C,site));
            data+=std::format("\"point_offsets\":{{\"body1\":{},\"subshape1\":{},\"body2\":{},\"subshape2\":{},\"ordinal\":{},\"space\":{},\"anchor1\":{},\"anchor2\":{},\"cached_normal\":{},\"solver_normal\":{},\"lambda\":{}}},",
                offsetof(P,body1),offsetof(P,subshape1),offsetof(P,body2),offsetof(P,subshape2),
                offsetof(P,ordinal),offsetof(P,space),offsetof(P,anchor1),offsetof(P,anchor2),
                offsetof(P,cached_local_normal),offsetof(P,solver_world_normal),offsetof(P,normal_lambda));
            data+=std::format("\"identity_offsets\":{{\"sdk_body_id\":{},\"managed_index\":{},\"managed_generation\":{},\"managed_registry\":{},\"action\":{}}},",
                offsetof(I,sdk_body_id),offsetof(I,managed_index),offsetof(I,managed_generation),
                offsetof(I,managed_registry),offsetof(I,action));
            data+=std::format("\"storage_offsets\":{{\"pointer\":{},\"requested\":{},\"aligned\":{},\"usage_after\":{},\"count\":{},\"action\":{}}},",
                offsetof(S,pointer),offsetof(S,requested_bytes),offsetof(S,aligned_bytes),
                offsetof(S,arena_usage_after),offsetof(S,count),offsetof(S,action));
            data+=std::format("\"heap_offsets\":{{\"pointer\":{},\"old_pointer\":{},\"requested\":{},\"old_bytes\":{},\"action\":{}}},",
                offsetof(E,pointer),offsetof(E,old_pointer),offsetof(E,requested_bytes),
                offsetof(E,old_bytes),offsetof(E,action));
            data+=std::format("\"heap_live_bytes\":{},\"heap_peak_bytes\":{},\"heap_allocation_calls\":{},\"heap_allocation_bytes\":{},\"heap_free_calls\":{},\"heap_free_bytes\":{},\"unmatched_lifetime_frees\":{},\"arena_reserve_calls\":{},\"arena_reserve_bytes\":{},\"arena_release_calls\":{},\"arena_release_bytes\":{},\"arena_high_water\":{},\"unclassified_arena_count\":{},\"recorder_valid\":{},\"sink_detached\":{},\"arena_capacities\":[",
                r.heapLiveBytes,r.heapPeakBytes,r.heapAllocationCalls,r.heapAllocationBytes,
                r.heapFreeCalls,r.heapFreeBytes,r.unmatchedLifetimeFrees,r.arenaReserveCalls,
                r.arenaReserveBytes,r.arenaReleaseCalls,r.arenaReleaseBytes,r.arenaHighWater,
                r.unclassifiedArenaCount,r.Valid()?"true":"false",Q::GetSink()==nullptr?"true":"false");
            for(std::size_t site=0;site<r.storageCapacityBytes.size();++site)
                data+=std::format("{}{{\"site\":{},\"bytes\":{},\"count\":{}}}",
                    site?",":"",site,r.storageCapacityBytes[site],r.storageCapacityCounts[site]);
            data+=" ]}}\n";
                written=data.size()<=16384&&capture->textBytes+ObservationRaw.eventBytes+data.size()<=64ull*1024*1024&&
                    std::fwrite(data.data(),1,data.size(),result)==data.size();
            }
            const bool resultClosed=std::fclose(result)==0;written=written&&resultClosed;
            std::println("PHASE26_Q7_PROCESS_END updates={} complete={} episodes={}",CompletedCheckTicks,int(complete&&written),capture->completedEpisodes);
            return complete&&written?0:1;
        }
#endif

#endif
#if defined(GENGINE_G8_CPU_ATTRIBUTION_CHECKS)
        // Measurement-only mode. Existing fixtures and their admission are unchanged.
        inline constexpr std::uint64_t CpuUpdateLimit=2400;
        inline constexpr std::uint64_t CpuPlannedUpdates=1824;
        inline std::uint64_t CpuStateHash(const std::string& bytes)
        {
            std::uint64_t hash=14695981039346656037ull;
            for(char value:bytes){hash^=static_cast<unsigned char>(value);hash*=1099511628211ull;}
            return hash;
        }
        inline std::string CpuStateHex(const std::string& bytes)
        {
            constexpr char digits[]="0123456789abcdef";
            std::string result;result.reserve(bytes.size()*2);
            for(char value:bytes){const auto byte=static_cast<unsigned char>(value);result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}
            return result;
        }
        struct CpuSample
        {
            int substeps{},arm{},round{},position{};bool warmup{},prestateEqual{},poststateEqual{};
            const char* order{};std::int64_t ns{};
            std::uint64_t update{},callbacks{},points{},removed{},heapCalls{},heapBytes{},arenaPeak{},stateHash{};
            std::size_t stateBytes{};
            std::int64_t observedRows=-1,positiveRows=-1,rootIterations=-1;
            double minimumNormal{},maximumNormal{};
        };
        struct CpuRecording
        {
            std::vector<CpuSample> samples;
            std::string initial;
            std::array<std::array<std::string,3>,3> output;
            std::array<int,3> substeps{2,1,16};
            bool complete{};
            CpuRecording(){samples.reserve(CpuPlannedUpdates);}
            ~CpuRecording()
            {
                // Print only after the measurement loop has completed or failed.
                std::println("CPU_ATTRIBUTION_PROTOCOL arms=A:0:off,B:0.05:off,C:0.05:existing_observer substep_order=2,1,16 warmup_per_arm=32 measured_per_arm=256,128,128 workers=2 dt=1/60 snapshot=SaveState_All allocator_scheduler_reset=no reset_and_serialization=outside_timer radius=1 mass=1 friction=0.5 restitution=0.5 damping=0 angular=1,2,3 timer=Update_wall worker_wait=unavailable hash=FNV1a64_exact_byte_comparison_authoritative");
                if(!initial.empty())std::println("CPU_ATTRIBUTION_STATE kind=initial substeps=0 arm=all bytes={} state_hash={:016x} hex={}",initial.size(),CpuStateHash(initial),CpuStateHex(initial));
                for(std::size_t group=0;group<output.size();++group)for(std::size_t arm=0;arm<3;++arm)
                    if(!output[group][arm].empty())std::println("CPU_ATTRIBUTION_STATE kind=post substeps={} arm={} bytes={} state_hash={:016x} hex={}",substeps[group],static_cast<char>('A'+arm),output[group][arm].size(),CpuStateHash(output[group][arm]),CpuStateHex(output[group][arm]));
                for(const auto& value:samples)
                    std::println("CPU_ATTRIBUTION_SAMPLE substeps={} arm={} warmup={} round={} order={} position={} update={} ns={} callbacks={} points={} removed={} observed_rows={} positive_rows={} root_iterations={} minimum_normal={:.9g} maximum_normal={:.9g} heap_calls={} heap_bytes={} arena_peak={} state_bytes={} state_hash={:016x} prestate_equal={} poststate_equal={}",
                        value.substeps,static_cast<char>('A'+value.arm),int(value.warmup),value.round,value.order,value.position,value.update,value.ns,value.callbacks,value.points,value.removed,value.observedRows,value.positiveRows,value.rootIterations,value.minimumNormal,value.maximumNormal,value.heapCalls,value.heapBytes,value.arenaPeak,value.stateBytes,value.stateHash,int(value.prestateEqual),int(value.poststateEqual));
                if(complete)std::println("CPU_ATTRIBUTION_PASS updates={} measured=1536 warmup=288 snapshot_restore=exact B_C_outputs=exact performance_acceptance=not_decided",CompletedCheckTicks);
                std::println("CPU_ATTRIBUTION_PROCESS_END updates={}",CompletedCheckTicks);
            }
        };
        struct CpuListener final : JPH::ContactListener
        {
            Listener material;
            std::atomic<std::uint64_t> callbacks{},points{},removed{};
            void Reset(){callbacks=0;points=0;removed=0;}
            void Contact(const JPH::ContactManifold& manifold,JPH::ContactSettings& settings)
            {++callbacks;points+=manifold.mRelativeContactPointsOn1.size();material.Configure(settings);}
            void OnContactAdded(const JPH::Body&,const JPH::Body&,const JPH::ContactManifold& manifold,JPH::ContactSettings& settings)override{Contact(manifold,settings);}
            void OnContactPersisted(const JPH::Body&,const JPH::Body&,const JPH::ContactManifold& manifold,JPH::ContactSettings& settings)override{Contact(manifold,settings);}
            void OnContactRemoved(const JPH::SubShapeIDPair&)override{++removed;}
        };
        inline int CpuAttribution()
        {
            Log::Initialize();CompletedCheckTicks=0;
            CpuRecording recording;
            if(!Check(JPH::Factory::sInstance==nullptr,"CPU SDK lifetime unowned"))return 1;
            JPH::RegisterDefaultAllocator();AllocationScope allocations;
            JPH::Factory::sInstance=new JPH::Factory;JPH::RegisterTypes();
            struct Lifetime{~Lifetime(){JPH::UnregisterTypes();delete JPH::Factory::sInstance;JPH::Factory::sInstance=nullptr;}} lifetime;
            LayerMap map;BroadFilter broad;PairFilter pairs;JPH::PhysicsSystem physics;
            physics.Init(32,0,128,128,map,broad,pairs);physics.SetGravity({0,-12,0});
            MeasuredTemp temp;JPH::JobSystemThreadPool jobs(JPH::cMaxPhysicsJobs,JPH::cMaxPhysicsBarriers,2);
            Observer observed;CpuListener listener;physics.SetContactListener(&listener);
            struct Bodies
            {
                JPH::PhysicsSystem& physics;JPH::BodyID floor,dynamic;
                ~Bodies(){physics.SetContactListener(nullptr);auto& bodies=physics.GetBodyInterface();for(const auto id:{dynamic,floor})if(!id.IsInvalid()){bodies.RemoveBody(id);bodies.DestroyBody(id);}}
            } owned{physics};
            auto& bodies=physics.GetBodyInterface();
            JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(10,.5f,10),0),JPH::RVec3::sZero(),JPH::Quat::sIdentity(),JPH::EMotionType::Static,0);
            floor.mFriction=.5f;floor.mRestitution=.5f;
            owned.floor=bodies.CreateAndAddBody(floor,JPH::EActivation::DontActivate);
            JPH::BodyCreationSettings sphere(new JPH::SphereShape(1),JPH::RVec3(0,1.5f,0),JPH::Quat::sIdentity(),JPH::EMotionType::Dynamic,1);
            sphere.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;sphere.mMassPropertiesOverride.mMass=1;
            sphere.mLinearDamping=sphere.mAngularDamping=0;sphere.mFriction=.5f;sphere.mRestitution=.5f;sphere.mAngularVelocity={1,2,3};
            sphere.mMotionQuality=JPH::EMotionQuality::Discrete;
            owned.dynamic=bodies.CreateAndAddBody(sphere,JPH::EActivation::Activate);
            if(!Check(!owned.floor.IsInvalid()&&!owned.dynamic.IsInvalid(),"CPU unchanged floor/sphere fixture"))return 1;
            JPH::StateRecorderImpl snapshot;
            physics.SaveState(snapshot,JPH::EStateRecorderState::All);
            if(!Check(!snapshot.IsFailed(),"CPU initial complete SDK snapshot"))return 1;
            recording.initial=snapshot.GetData();
            if(!Check(!recording.initial.empty(),"CPU nonempty frozen state"))return 1;
            constexpr std::array<std::array<int,3>,6> orders{{{{0,1,2}},{{1,2,0}},{{2,0,1}},{{0,2,1}},{{2,1,0}},{{1,0,2}}}};
            constexpr std::array<const char*,6> names{"ABC","BCA","CAB","ACB","CBA","BAC"};
            for(std::size_t group=0;group<recording.substeps.size();++group)
            {
                const int substeps=recording.substeps[group],measured=substeps==2?256:128;
                for(int sequence=0;sequence<32+measured;++sequence)
                {
                    const bool warmup=sequence<32;const int round=warmup?sequence:sequence-32;
                    const auto orderIndex=static_cast<std::size_t>(sequence)%orders.size();
                    std::array<CpuSample,3> paired{};
                    for(int position=0;position<3;++position)
                    {
                        const int arm=orders[orderIndex][static_cast<std::size_t>(position)];
                        if(!Check(CompletedCheckTicks<CpuUpdateLimit,"CPU hard Update budget"))return 1;
                        snapshot.Rewind();
                        if(!Check(physics.RestoreState(snapshot)&&!snapshot.IsFailed(),"CPU supported complete state restore"))return 1;
                        JPH::StateRecorderImpl restored;physics.SaveState(restored,JPH::EStateRecorderState::All);
                        if(!Check(!restored.IsFailed()&&restored.GetData()==recording.initial,"CPU pre-Update state byte identity"))return 1;
                        if(!Check(physics.GetNumBodies()==2&&physics.GetNumActiveBodies(JPH::EBodyType::RigidBody)==1&&bodies.IsActive(owned.dynamic),"CPU unchanged body identity and active workload"))return 1;
                        listener.material.length=arm==0?0.f:.05f;listener.material.observer=arm==2?&observed:nullptr;
                        listener.Reset();observed.Reset();temp.peak=0;
                        if(!Check(temp.storage.IsEmpty(),"CPU arena empty before Update"))return 1;
                        allocations.Begin();
                        const auto start=std::chrono::steady_clock::now();
                        const auto error=physics.Update(1.f/60.f,substeps,&temp,&jobs);
                        const auto finish=std::chrono::steady_clock::now();
                        allocations.End();++CompletedCheckTicks;
                        CpuSample value;value.substeps=substeps;value.arm=arm;value.round=round;value.position=position;value.warmup=warmup;value.order=names[orderIndex];value.prestateEqual=true;
                        value.update=CompletedCheckTicks;value.ns=std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count();
                        value.callbacks=listener.callbacks.load();value.points=listener.points.load();value.removed=listener.removed.load();
                        value.heapCalls=AllocCalls.load();value.heapBytes=AllocBytes.load();value.arenaPeak=temp.peak;
                        recording.samples.push_back(value);auto& sample=recording.samples.back();
                        if(!Check(error==JPH::EPhysicsUpdateError::None&&temp.storage.IsEmpty()&&temp.peak<=32*1024*1024,"CPU valid Update and unchanged arena"))return 1;
                        if(!Check(observed.count.load()<=Observer::Capacity,"CPU bounded existing observer"))return 1;
                        if(arm==2)
                        {
                            sample.observedRows=static_cast<std::int64_t>(observed.count.load());sample.positiveRows=0;sample.rootIterations=0;
                            sample.minimumNormal=std::numeric_limits<double>::infinity();sample.maximumNormal=-std::numeric_limits<double>::infinity();
                            for(std::size_t i=0;i<observed.count.load();++i)
                            {
                                const auto& observation=(*observed.rows)[i].value;
                                if(!Check(std::isfinite(observation.mNormalLambda)&&observation.mRootIterations<=48&&Finite(observation.mNormal)&&Finite(observation.mSpinImpulse)&&Finite(observation.mRollingImpulse)&&Finite(observation.mAngularBefore1)&&Finite(observation.mAngularBefore2)&&Finite(observation.mAngularAfter1)&&Finite(observation.mAngularAfter2),"CPU finite read-only resistance observation"))return 1;
                                for(unsigned axis=0;axis<3;++axis)if(!Check(std::isfinite(observation.mBefore[axis])&&std::isfinite(observation.mAfter[axis]),"CPU finite realized accumulator"))return 1;
                                sample.positiveRows+=observation.mNormalLambda>0?1:0;sample.rootIterations+=observation.mRootIterations;
                                sample.minimumNormal=(std::min)(sample.minimumNormal,double(observation.mNormalLambda));sample.maximumNormal=(std::max)(sample.maximumNormal,double(observation.mNormalLambda));
                            }
                            if(!Check(sample.observedRows>0&&sample.positiveRows>0,"CPU positive law observation present"))return 1;
                        }
                        else if(!Check(observed.count.load()==0,"CPU observer disabled in A and B"))return 1;
                        JPH::StateRecorderImpl after;physics.SaveState(after,JPH::EStateRecorderState::All);
                        if(!Check(!after.IsFailed(),"CPU complete output snapshot"))return 1;
                        const auto actual=after.GetData();auto& expected=recording.output[group][static_cast<std::size_t>(arm)];
                        if(expected.empty())expected=actual;
                        sample.stateBytes=actual.size();sample.stateHash=CpuStateHash(actual);sample.poststateEqual=actual==expected;
                        if(!Check(sample.poststateEqual,"CPU repeated same-arm serialized state"))return 1;
                        paired[static_cast<std::size_t>(arm)]=sample;
                    }
                    if(!Check(recording.output[group][1]==recording.output[group][2]&&paired[1].stateHash==paired[2].stateHash&&paired[1].callbacks==paired[2].callbacks&&paired[1].points==paired[2].points&&paired[1].removed==paired[2].removed,"CPU B/C exact output and callback equivalence"))return 1;
                }
            }
            if(!Check(CompletedCheckTicks==CpuPlannedUpdates&&recording.samples.size()==CpuPlannedUpdates,"CPU exact admitted sample/tick count"))return 1;
            recording.complete=true;return 0;
        }
#endif
#if !defined(JPH_GENGINE_PHASE26_OBSERVATION)
        inline bool ImpulseRegression()
        {
            PhysicsSystem system;auto* world=new PhysicsWorld;
            if(!Check(bool(world->ConfigureBackend(PhysicsBackend::Jolt)),"impulse Jolt backend")){delete world;return false;}
            system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
            PhysicsBodyHandle a,b; if(!AddSphere(*world,{0,0,0},a)||!AddSphere(*world,{10,0,0},b))return false;
            if(!Check(bool(world->ApplyLinearImpulse(a,{.25f,0,0}))&&bool(world->ApplyLinearImpulse(a,{.5f,0,0})),"consecutive legacy impulses")||
                !Check(world->ReadBody(a)->linearVelocity==Vec3f(.75f,0,0)&&system.GetCompletedSteps()==0,"legacy pending sum without stepping"))return false;
            const auto original=world->ReadBodyProperties(a);auto* raw=ChainWakeTestAccess::Raw(*world,a);
            if(!Check(original&&raw&&raw->SetSpinResistanceLength(.1f)&&raw->SetRollingResistanceLength(.15f),"pending authored material test input"))return false;
            if(!Check(world->ReadBodyProperties(a)->material==original->material,"properties read actual cached SDK material"))return false;
            auto proposed=*original;proposed.material.friction=.25f;ChainWakeTestAccess::FailPropertyReadbackOnce(*world);
            if(!Check(!world->UpdateBodyProperties(a,proposed)&&world->ReadBodyProperties(a)->material==original->material&&raw->GetSpinResistanceLength()==.1f&&raw->GetRollingResistanceLength()==.15f&&raw->m_LinearVelocity==Vec3f(.75f,0,0),"material rollback preserves backend and pending legacy state"))return false;
            if(!Advance(system,"impulse",1))return false;
            const auto synchronized=world->ReadImpulseMotion(a);const auto properties=world->ReadBodyProperties(a);
            if(!Check(synchronized&&properties&&synchronized->linear==Vec3f(.75f,0,0)&&properties->material.spinResistanceLength==.1f&&properties->material.rollingResistanceLength==.15f,"pending legacy synchronization preserved"))return false;
            const auto beforeA=*world->ReadBody(a),beforeB=*world->ReadBody(b);
            std::array<PhysicsImpulseCommand,2> commands={PhysicsImpulseCommand{a,{0,.1f,0}},PhysicsImpulseCommand{b,{0,.2f,0}}};
            ChainWakeTestAccess::FailReadbackOnce(*world);
            const auto failed=world->ApplyImpulseBatch(commands);
            if(!Check(!failed&&SamePhysicalState(beforeA,*world->ReadBody(a))&&SamePhysicalState(beforeB,*world->ReadBody(b)),"existing atomic impulse rollback"))return false;
            commands.back().linear.x=std::numeric_limits<float>::quiet_NaN();
            if(!Check(!world->ApplyImpulseBatch(commands)&&SamePhysicalState(beforeA,*world->ReadBody(a))&&SamePhysicalState(beforeB,*world->ReadBody(b)),"invalid last impulse atomic rejection"))return false;
            if(!Advance(system,"impulse",2))return false;
            std::println("RESISTANCE_IMPULSE_PASS ticks=2 consecutive_legacy=1 atomic_readback_rollback=1 invalid_last=1");return true;
        }
        struct G8Frame
        {
            std::array<PhysicsBodyState,10> body{};
            std::array<double,8> error{};
        };
        inline double Angle(const Quat& a,const Quat& b)
        {
            const auto qa=glm::normalize(glm::dquat(a)),qb=glm::normalize(glm::dquat(b));
            return 2*std::acos((std::min)(1.,std::abs(glm::dot(qa,qb))));
        }
        inline bool G8()
        {
            PhysicsSystem system;auto* world=new PhysicsWorld;
            if(!Check(bool(world->ConfigureBackend(PhysicsBackend::Jolt)),"G8 Jolt backend")){delete world;return false;}
            system.SetPhysicsWorld(world);world->SetGravity({0,-12,0});
            std::array<PhysicsBodyHandle,10> bodies;std::array<PhysicsJointHandle,8> joints;
            for(unsigned i=0;i<=8;++i)if(!AddSphere(*world,{2.25f*i,1.5f,0},bodies[i],i?BodyType::Dynamic:BodyType::Static))return false;
            auto floor=CreateCollisionShape(BoxShapeDesc{{50,.5f,50},{0,0,0}});
            if(!Check(bool(floor),"G8 authored floor"))return false;
            PhysicsBodyDesc ground;ground.type=BodyType::Static;ground.material={.5f,.5f,.05f,.05f};
            const auto groundHandle=world->CreateBody(ground,*floor);
            if(!Check(bool(groundHandle),"G8 floor body"))return false;bodies[9]=*groundHandle;
            for(unsigned i=1;i<=8;++i)
            {const auto joint=world->CreateDistanceJoint({bodies[i-1],bodies[i],{},{},2.25f,false});if(!Check(bool(joint),"G8 authored joint"))return false;joints[i-1]=*joint;}
            if(!Check(bool(world->EnableJoltDiagnostics(true)),"G8 contact-domain observation"))return false;
            Observer rows;G8ResistanceObservation::SetObserver(&rows);
            struct ClearObserver{~ClearObserver(){G8ResistanceObservation::SetObserver(nullptr);G8ResistanceObservation::End();}} clear;
            auto frames=std::make_unique<std::array<G8Frame,421>>();
            std::array<unsigned,8> episode{},confirmation{};
            double work=0,initialEnergy=144,maxHorizontal=0,maxError=0,maxPenetration=0,maxExcess=0;
            std::uint64_t heapCalls=0,heapBytes=0,updateNs=0,roots=0;std::size_t peak=0,totalRows=0;
            unsigned initialSleep=0,returnSleep=0,quietTicks=0;
            const auto capture=[&](unsigned tick)
            {
                auto& frame=(*frames)[tick];
                for(unsigned i=0;i<10;++i){const auto value=world->ReadBody(bodies[i]);if(!value)return false;frame.body[i]=*value;}
                for(unsigned i=0;i<8;++i){const auto joint=world->ReadDistanceJoint(joints[i]);if(!joint)return false;frame.error[i]=double(joint->error);}
                return true;
            };
            if(!Check(capture(0),"G8 initial readback"))return false;
            for(unsigned tick=1;tick<=420;++tick)
            {
                if(tick==301)
                {
                    const Vec3f impulse(0,0,.5f);const auto before=(*frames)[300];
                    work=glm::dot(glm::dvec3(impulse),glm::dvec3(before.body[8].linearVelocity))+.5*glm::dot(glm::dvec3(impulse),glm::dvec3(impulse));
                    if(!Check(bool(world->ApplyLinearImpulse(bodies[8],impulse))&&system.GetCompletedSteps()==300,"G8 exact authored kick and paused boundary"))return false;
                    for(unsigned i=1;i<=8;++i)
                    {
                        const auto now=world->ReadBody(bodies[i]);if(!Check(now&&!now->sleeping,"G8 connected activation"))return false;
                        if(i<8&&!Check(SamePhysicalState(before.body[i],*now),"no neighbor physical impulse"))return false;
                    }
                }
                rows.Reset();G8ResistanceObservation::Begin();const bool stepped=Advance(system,"G8",tick);G8ResistanceObservation::End();
                if(!stepped||!Check(capture(tick),"G8 authoritative complete readback")||!rows.Validate("G8",tick))return false;
                heapCalls+=G8ResistanceObservation::HeapCalls();heapBytes+=G8ResistanceObservation::HeapBytes();updateNs+=G8ResistanceObservation::UpdateNanoseconds();
                peak=(std::max)(peak,static_cast<std::size_t>(G8ResistanceObservation::TempPeakBytes()));roots+=rows.roots;totalRows+=rows.count.load();
                if(!Check(G8ResistanceObservation::UpdateCalls()==1&&peak<=32*1024*1024,"G8 one SDK update / unchanged arena"))return false;
                const auto& frame=(*frames)[tick];double raw=0,fidelity=0,kinetic=0,errorSum=0,tickError=0,tickPenetration=0,horizontal=0;unsigned asleep=0;bool allQuiet=true;
                for(unsigned i=1;i<=8;++i)
                {
                    const auto& body=frame.body[i];const auto v=glm::dvec3(body.linearVelocity),w=glm::dvec3(body.angularVelocity);
                    const double k=.5*glm::dot(v,v)+.2*glm::dot(w,w);kinetic+=k;raw+=k+12*double(body.position.y);fidelity+=k+12*(std::max)(double(body.position.y)-1.5,0.);
                    allQuiet=allQuiet&&(body.sleeping||(glm::length(v)<=.05&&glm::length(w)<=.02));
                    asleep+=unsigned(body.sleeping);tickError=(std::max)(tickError,std::abs(frame.error[i-1]));errorSum+=std::abs(frame.error[i-1]);
                    const auto local=glm::abs(glm::dvec3(body.position))-glm::dvec3(50,.5,50);
                    const double signedDistance=glm::length(glm::max(local,glm::dvec3(0)))+(std::min)((std::max)({local.x,local.y,local.z}),0.);
                    const double penetration=(std::max)(0.,1-signedDistance);tickPenetration=(std::max)(tickPenetration,penetration);
                    if(penetration>.02)
                    {if(!episode[i-1])episode[i-1]=tick;confirmation[i-1]=0;if(!Check(tick<episode[i-1]+4,"G8 penetration recovery deadline"))return false;}
                    else if(episode[i-1])
                    {if(!confirmation[i-1])confirmation[i-1]=tick;else if(tick>=confirmation[i-1]+2){episode[i-1]=confirmation[i-1]=0;}}
                    if(tick<=300&&!Check(glm::length(glm::dvec3(body.position)-glm::dvec3((*frames)[0].body[i].position))<=.020,"G8 initial displacement gate"))return false;
                    if(tick>300){const auto delta=glm::dvec3(body.position)-glm::dvec3((*frames)[300].body[i].position);horizontal=(std::max)(horizontal,std::hypot(delta.x,delta.z));}
                    if(tick>=303&&!Check(std::abs((*frames)[tick-1].error[i-1]-.5*((*frames)[tick-2].error[i-1]+frame.error[i-1]))<=.00225,"G8 loaded joint HF residual"))return false;
                }
                const auto contacts=world->ReadJoltDiagnostics();if(!Check(contacts&&contacts->overflow==0,"G8 complete contact identity capture"))return false;
                for(const auto& contact:contacts->contacts)
                    if(!Check(contact.bodyA==bodies[9]||contact.bodyB==bodies[9],"G8 zero nonground eligible contacts"))return false;
                for(unsigned i=0;i<=8;++i)for(unsigned j=i+2;j<=8;++j)
                    if(!Check(glm::length(glm::dvec3(frame.body[i].position)-glm::dvec3(frame.body[j].position))>2.,"G8 independent nonadjacent clearance"))return false;
                quietTicks=allQuiet?quietTicks+1:0;
                const bool settled=tick<=300||tick>=420||quietTicks>=30;
                const bool contactSettled=(tick>=121&&tick<=300)||tick>=420||quietTicks>=30;
                const double allowance=tick<=300?.00125:.0128125,applied=tick>300?work:0;
                std::println("RESISTANCE_G8 tick={} raw={:.17g} fidelity={:.17g} kinetic={:.17g} work={:.17g} max_error={:.17g} sum_error={:.17g} penetration={:.17g} horizontal={:.17g} asleep={} contacts={} rows={} arena={}",tick,raw,fidelity,kinetic,applied,tickError,errorSum,tickPenetration,horizontal,asleep,contacts->contacts.size(),rows.count.load(),G8ResistanceObservation::TempPeakBytes());
                for(unsigned i=1;i<=8;++i){const auto& b=frame.body[i];std::println("RESISTANCE_G8_BODY tick={} ordinal={} x={:.9g} y={:.9g} z={:.9g} qw={:.9g} qx={:.9g} qy={:.9g} qz={:.9g} vx={:.9g} vy={:.9g} vz={:.9g} wx={:.9g} wy={:.9g} wz={:.9g} asleep={}",tick,i,b.position.x,b.position.y,b.position.z,b.orientation.w,b.orientation.x,b.orientation.y,b.orientation.z,b.linearVelocity.x,b.linearVelocity.y,b.linearVelocity.z,b.angularVelocity.x,b.angularVelocity.y,b.angularVelocity.z,int(b.sleeping));}
                if(!Check(tickError<=(settled?.01125:.0225)&&errorSum<=(settled?.09:.18),"G8 accepted joint bounds")||
                    !Check(tickPenetration<=.04&&(!contactSettled||tickPenetration<=.02),"G8 accepted actual geometric penetration")||
                    !Check(raw-initialEnergy-applied<=allowance&&fidelity-applied<=allowance,"G8 raw and E-star whole-step energy")||
                    !Check(horizontal<=.250,"G8 accepted loaded displacement")||
                    !Check(!((tick>=120&&tick<=300)||tick==420)||asleep==8,"G8 sleep deadlines")||
                    !Check(tick!=301||asleep==0,"G8 all awake tick301"))return false;
                if(asleep==8&&tick<=300&&!initialSleep)initialSleep=tick;if(asleep==8&&tick>300&&!returnSleep)returnSleep=tick;
                if(tick>=180&&tick<=300)
                    for(unsigned i=1;i<=8;++i)
                    {
                        for(unsigned a=tick-59;a<=tick;++a)for(unsigned b=a+1;b<=tick;++b)
                            if(!Check(glm::length(glm::dvec3((*frames)[a].body[i].position)-glm::dvec3((*frames)[b].body[i].position))<=.002&&Angle((*frames)[a].body[i].orientation,(*frames)[b].body[i].orientation)<=.002,"G8 quiet60 position/orientation spans"))return false;
                        for(unsigned a=tick-58;a<tick;++a)
                        {
                            const auto residual=glm::dvec3((*frames)[a].body[i].position)-.5*(glm::dvec3((*frames)[a-1].body[i].position)+glm::dvec3((*frames)[a+1].body[i].position));
                            const auto mid=glm::slerp((*frames)[a-1].body[i].orientation,(*frames)[a+1].body[i].orientation,.5f);
                            if(!Check(glm::length(residual)<=.001&&Angle((*frames)[a].body[i].orientation,mid)<=.001,"G8 quiet HF residuals"))return false;
                        }
                    }
                maxHorizontal=(std::max)(maxHorizontal,horizontal);maxError=(std::max)(maxError,tickError);maxPenetration=(std::max)(maxPenetration,tickPenetration);maxExcess=(std::max)(maxExcess,raw-initialEnergy-applied);
            }
            for(auto value:episode)if(!Check(value==0,"G8 no unfinished contact recovery episode"))return false;
            std::println("CHAIN_WAKE_301_PASS case=G8 dynamic=8 commands=1 ticks=301 awake=8 extra_neighbor_impulses=0");
            std::println("RESISTANCE_G8_PREFIX_PASS ticks=420 initial_sleep={} returned_sleep={} horizontal={:.17g} max_error={:.17g} penetration={:.17g} raw_excess={:.17g} update_ns={} sdk_heap_calls={} sdk_heap_bytes={} arena_peak={} rows={} root_iterations={} full1200=NOT_RUN",initialSleep,returnSleep,maxHorizontal,maxError,maxPenetration,maxExcess,updateNs,heapCalls,heapBytes,peak,totalRows,roots);
            G8ResistanceObservation::SetObserver(nullptr);return Check(bool(world->Close()),"G8 clean world retirement");
        }
#endif
#endif
    }
    inline int RunResistanceChecks()
    {
#if defined(JPH_GENGINE_PHASE26_OBSERVATION)
#if defined(GENGINE_PHASE26_CONTACT_REGRESSION)
        return ResistanceChecks::ContactRegression();
#else
        return ResistanceChecks::ObservationCorrectness();
#endif
#elif defined(GENGINE_G8_CPU_ATTRIBUTION_CHECKS)
        return ResistanceChecks::CpuAttribution();
#else
        Log::Initialize();CompletedCheckTicks=0;
        struct TickReport {~TickReport(){std::println("RESISTANCE_PROCESS_END ticks={}",CompletedCheckTicks);}} tickReport;
        std::println("RESISTANCE_LAYOUT max_constraint_bytes={} contact_capacity=16384 temp_capacity=33554432",JPH::ContactConstraintManager::cMaxConstraintSize);
#if !defined(GENGINE_G8_RESISTANCE_PRISTINE)
        constexpr auto growth=JPH::ContactConstraintManager::cMaxConstraintSize-480u;
        constexpr auto added=std::uint64_t(growth)*16384+(sizeof(JPH::ContactSettings)-64u)*4096;
        if(!ResistanceChecks::Check(JPH::ContactConstraintManager::cMaxConstraintSize>=480u&&sizeof(JPH::ContactSettings)>=64u&&added<=5*1024*1024,"additional discrete+CCD contact arena occupancy <=5MiB"))return 1;
        std::println("RESISTANCE_ARENA_ADMISSION original_bytes=480 actual_bytes={} contact_settings_bytes={} original_settings_bytes=64 additional_arena={} total_limit=33554432",JPH::ContactConstraintManager::cMaxConstraintSize,sizeof(JPH::ContactSettings),added);
        if(!ResistanceChecks::Numerical())return 1;
#endif
        if(!ResistanceChecks::LoadExpectedZero()||!ResistanceChecks::ZeroLaw())return 1;
#if defined(GENGINE_G8_RESISTANCE_PRISTINE)
        std::println("RESISTANCE_PRISTINE_PASS ticks={}",CompletedCheckTicks);return 0;
#else
        if(!ResistanceChecks::DirectChecks()||!MassInertia()||!IndependentCapacity()||!ResistanceChecks::ImpulseRegression())return 1;
        for(unsigned links:{4u,8u,16u})if(!ChainAt301(links,false))return 1;
        if(!ResistanceChecks::G8()||!ResistanceChecks::Check(CompletedCheckTicks<=1600,"exact aggregate tick cap"))return 1;
        std::println("RESISTANCE_CHECKS_PASS ticks={} zero=168 direct=24 full_qualification=NOT_RUN",CompletedCheckTicks);return 0;
#endif
#endif
    }
}
