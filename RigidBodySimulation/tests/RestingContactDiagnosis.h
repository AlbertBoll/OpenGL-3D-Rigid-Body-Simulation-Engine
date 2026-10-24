#pragma once

// Phase 19's opt-in RBS fixture and recorder. The ordinary app creates no session.
#include "RbsLaunchConfig.h"
#include "RbsState.h"
#include "Physics/PhysicsProfile.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeBox.h"
#include "Physics/ShapeConvex.h"
#include "Physics/Broadphase.h"
#include <cstdlib>
#include "Scene/_Scene.h"
#include "Core/Log.h"
#include <bit>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <new>
#include <utility>
#include <vector>

namespace Rbs::RestDiagnosis
{
    using namespace ::GEngine;
    namespace PD=::GEngine::PhysicsDiagnostic;
    inline constexpr std::array<unsigned,9> Durations{1800,600,600,240,240,240,240,600,2400};
    inline constexpr std::array<const char*,9> Names{"lattice","ground","friction","impact-0.5",
        "impact-1.0","impact-1.5","off-center","spin","stack"};
    inline constexpr std::array<unsigned,11> ConditionalDurations{1800,300,300,120,120,120,120,300,1200,300,300};
    inline constexpr std::array<const char*,11> ConditionalNames{"lattice","ground","friction","impact-0.5",
        "impact-1.0","impact-1.5","off-center","spin","stack","free-rolling","convex-sliding"};
    struct BodyInput
    {
        int shape{}, type{};
        float inverseMass{}, elasticity{.5f}, friction{.5f}, radius{1};
        std::uint32_t layer{1}, mask{~std::uint32_t{0}};
        Vec3f position{}, linear{}, angular{};
        Quat orientation{1,0,0,0};
        std::vector<Vec3f> points;
    };
    inline void Vector(std::istream& in,Vec3f& v) { in>>v.x>>v.y>>v.z; }
    inline bool LoadLattice(const std::string& path,std::vector<BodyInput>& bodies)
    {
        std::ifstream in(path);
        unsigned count{}; in>>count;
        if (!in || count!=185) return false;
        bodies.resize(count);
        for (unsigned i=0;i<count;++i) {
            auto& b=bodies[i]; unsigned points{};
            in>>b.shape>>b.type>>b.inverseMass>>b.elasticity>>b.friction>>b.layer>>b.mask;
            Vector(in,b.position);
            in>>b.orientation.w>>b.orientation.x>>b.orientation.y>>b.orientation.z;
            Vector(in,b.linear); Vector(in,b.angular); in>>b.radius>>points;
            if (!in || points>36 || b.shape!=(i<5?1:0) || b.type!=(i<5?0:1) ||
                b.inverseMass!=(i<5?0.f:1.f) || b.elasticity!=.5f || b.friction!=.5f ||
                b.layer!=1 || b.mask!=~std::uint32_t{0} || b.linear!=Vec3f(0) || b.angular!=Vec3f(0) ||
                (i<5 ? points!=36 : (points!=0 || b.radius!=1.f))) return false;
            b.points.resize(points);
            for (auto& p:b.points) { Vector(in,p); if (!Math::IsFinite(p)) return false; }
            if (!in || !Math::IsFinite(b.position) || !Math::IsFinite(b.orientation)) return false;
        }
        in>>std::ws;
        return in.eof();
    }
    inline BodyInput Box(Vec3f position,Vec3f extent,bool dynamic)
    {
        BodyInput b; b.shape=1; b.type=dynamic?1:0; b.inverseMass=dynamic?1.f:0.f;
        b.position=position;
        for (int x:{-1,1}) for (int y:{-1,1}) for (int z:{-1,1})
            b.points.emplace_back(extent*Vec3f(float(x),float(y),float(z))*.5f);
        return b;
    }
    inline std::vector<BodyInput> Focused(unsigned episode)
    {
        std::vector<BodyInput> result{Box({}, {100,1,100},false)};
        BodyInput b; b.type=1; b.inverseMass=1; b.position={0,1.5f,0};
        if (episode==2) { b=Box({0,1.5f,0},{2,2,2},true); b.linear={.5f,0,0}; }
        if (episode>=3 && episode<=5) { b.position.y=1.501f; b.linear.y=-.5f*float(episode-2); }
        if (episode==6) {
            const float angle=glm::radians(15.f);
            b=Box({0,.501f+std::cos(angle)+std::sin(angle),0},{2,2,2},true);
            b.orientation=glm::angleAxis(angle,Vec3f(0,0,1)); b.linear={0,-1.5f,0};
        }
        if (episode==7) b.angular={0,1,0};
        if (episode==8) {
            for (unsigned i=0;i<4;++i) result.push_back(Box({0,1.5f+2.01f*float(i),0},{2,2,2},true));
        } else result.push_back(b);
        return result;
    }
    template<class T> inline void Binary(std::ofstream& out,const T& v)
    {
        static_assert(std::is_arithmetic_v<T>);
        out.write(reinterpret_cast<const char*>(&v),sizeof(v));
    }
    inline void BinaryState(std::ofstream& out,const PD::BodyState& s)
    {
        for (float v:{s.position.x,s.position.y,s.position.z,s.orientation.w,s.orientation.x,
             s.orientation.y,s.orientation.z,s.linear.x,s.linear.y,s.linear.z,s.angular.x,s.angular.y,s.angular.z}) Binary(out,v);
        Binary(out,static_cast<std::uint8_t>(s.sleeping)); Binary(out,s.inactive);
    }
    inline void CsvVector(std::ostream& out,const Vec3f& v) { out<<','<<v.x<<','<<v.y<<','<<v.z; }

    struct Session;
    extern volatile const std::uint64_t Phase21CompiledStorage[8];
    inline bool RunConditionalQueries(Session&);

    // Outward-rounded arithmetic for an independent disk-optimality certificate.
    // Inputs are exact binary32/binary64 observer operands. No solver routine is
    // called and no tolerance is added to the accepted energy allowance.
    struct AuditInterval
    {
        double lo{},hi{};
        AuditInterval()=default;
        AuditInterval(double x):lo(x),hi(x) {}
        AuditInterval(double a,double b):lo(a),hi(b) {}
        static double Down(double x) { return std::nextafter(x,-std::numeric_limits<double>::infinity()); }
        static double Up(double x) { return std::nextafter(x,std::numeric_limits<double>::infinity()); }
        friend AuditInterval operator+(AuditInterval a,AuditInterval b) { return {Down(a.lo+b.lo),Up(a.hi+b.hi)}; }
        friend AuditInterval operator-(AuditInterval a,AuditInterval b) { return {Down(a.lo-b.hi),Up(a.hi-b.lo)}; }
        friend AuditInterval operator*(AuditInterval a,AuditInterval b) {
            const double p[]{a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};
            return {Down(*std::min_element(p,p+4)),Up(*std::max_element(p,p+4))};
        }
        static AuditInterval Square(AuditInterval a) {
            const double low=a.lo<=0 && a.hi>=0?0:std::min(a.lo*a.lo,a.hi*a.hi);
            return {std::max(0.,Down(low)),Up(std::max(a.lo*a.lo,a.hi*a.hi))};
        }
        static AuditInterval Norm(AuditInterval a,AuditInterval b) {
            const auto s=Square(a)+Square(b);
            return {std::max(0.,Down(std::sqrt(std::max(0.,s.lo)))),Up(std::sqrt(s.hi))};
        }
        static double AbsUpper(AuditInterval a) { return std::max(std::abs(a.lo),std::abs(a.hi)); }
        bool Finite() const { return std::isfinite(lo) && std::isfinite(hi) && lo<=hi; }
    };

    // Refined mode only. One sequential NormalSliding -> [Spin] -> [Rolling]
    // link is sufficient: Solve finishes its normal observation before either
    // angular operation, and PreSolve resets both angular accumulators. Every
    // emitted angular update is independently certified; no constraint table.
    struct AngularAudit
    {
        using I=AuditInterval;
        struct Link {
            std::array<std::uint64_t,4> identity{};
            std::uintptr_t token{};
            int reason{},slot{};
            Vec3f normal{},localA{},localB{};
            double load{},duration{};
            unsigned seen{};
        } link;
        struct Summary {
            std::uint64_t events{},certified{},forced{},positive{},failures{};
            double positiveWork{},objectiveRatio{},gapRatio{};
        } spin,rolling;
        // Heap-owned fixed scratch, also borrowed by the bounded direct checks.
        // No Event/body copies are introduced on the nested solver stack.
        struct Problem {
            I h00{},h01{},h11{},g0{},g1{};
            glm::dvec3 u{},v{};
            double s{},t{},x{},y{},radius{},tau{},objective{},gap{},z{},w{};
            bool disk{},oldFeasible{},projectedFeasible{};
        } p;
        std::array<I,32> scratch{};
        static std::array<std::uint64_t,4> Identity(const PD::Event& e) {
            return {e.a->GetIdentity().GetSlot(),e.a->GetIdentity().GetGeneration(),
                    e.b->GetIdentity().GetSlot(),e.b->GetIdentity().GetGeneration()};
        }
        void Track(const PD::Event& e) {
            if (e.operation==PD::Operation::Spin || e.operation==PD::Operation::Rolling) return;
            link.token=0; link.seen=0;
            if (e.operation==PD::Operation::WarmStart) link.duration=e.duration;
            if (e.operation!=PD::Operation::NormalSliding || !e.a || !e.b) return;
            link.identity=Identity(e); link.token=e.constraintToken; link.reason=e.reason; link.slot=e.slot;
            link.normal=e.normal; link.localA=e.localA; link.localB=e.localB;
            link.load=e.accumulatedAfter[0]; link.seen=1;
        }
        bool Linked(const PD::Event& e) const {
            return e.a && e.b && link.token && link.token==e.constraintToken && link.reason==e.reason &&
                link.slot==e.slot && link.identity==Identity(e) && link.localA==e.localA && link.localB==e.localB;
        }
        static bool Near(double actual,double predicted) {
            return std::isfinite(actual) && std::isfinite(predicted) &&
                std::abs(actual-predicted)<=1e-5*std::max(1.,std::abs(predicted));
        }
        // Numerical adjacent value only: this diagnostic does not need the CRT's
        // incidental errno/FP-status/OS error reporting. Integer ordering also
        // keeps zero/subnormal handling independent of DAZ and rounding mode.
        __declspec(noinline) static constexpr double AdjacentBinary64(double x,double toward) noexcept {
            constexpr std::uint64_t sign=0x8000000000000000ull,mask=0x7fffffffffffffffull,
                infinity=0x7ff0000000000000ull;
            auto bits=std::bit_cast<std::uint64_t>(x);
            const auto target=std::bit_cast<std::uint64_t>(toward);
            if ((bits&mask)>infinity) return x;
            if ((target&mask)>infinity) return toward;
            if (bits==target || ((bits&mask)==0 && (target&mask)==0)) return toward;
            if ((bits&mask)==0) return std::bit_cast<double>((target&sign)|std::uint64_t(1));
            const auto order=(bits&sign)?~bits:(bits|sign);
            const auto targetOrder=(target&sign)?~target:(target|sign);
            if (order<targetOrder) bits=(bits&sign)?bits-1:bits+1;
            else bits=(bits&sign)?bits+1:bits-1;
            return std::bit_cast<double>(bits);
        }
        // Out-parameter interval primitives retain AuditInterval's exact operation
        // trees and outward rounding, without Debug by-value expression temporaries.
        // Inputs may alias output: both bounds are computed before either is stored.
        __declspec(noinline) static void Set(I& out,double value) { out.lo=value; out.hi=value; }
        __declspec(noinline) static void Add(I& out,const I& a,const I& b) {
            const double lo=AdjacentBinary64(a.lo+b.lo,-std::numeric_limits<double>::infinity());
            const double hi=AdjacentBinary64(a.hi+b.hi,std::numeric_limits<double>::infinity());
            out.lo=lo; out.hi=hi;
        }
        __declspec(noinline) static void Sub(I& out,const I& a,const I& b) {
            const double lo=AdjacentBinary64(a.lo-b.hi,-std::numeric_limits<double>::infinity());
            const double hi=AdjacentBinary64(a.hi-b.lo,std::numeric_limits<double>::infinity());
            out.lo=lo; out.hi=hi;
        }
        __declspec(noinline) static void Mul(I& out,const I& a,const I& b) {
            const double v0=a.lo*b.lo,v1=a.lo*b.hi,v2=a.hi*b.lo,v3=a.hi*b.hi;
            // Same first-element/tie/NaN comparison order as min/max_element.
            double lo=v0;
            if (v1<lo) lo=v1; if (v2<lo) lo=v2; if (v3<lo) lo=v3;
            const double lower=AdjacentBinary64(lo,-std::numeric_limits<double>::infinity());
            double hi=v0;
            if (hi<v1) hi=v1; if (hi<v2) hi=v2; if (hi<v3) hi=v3;
            const double upper=AdjacentBinary64(hi,std::numeric_limits<double>::infinity());
            out.lo=lower; out.hi=upper;
        }
        __declspec(noinline) static void Square(I& out,const I& a) {
            const double low=a.lo<=0 && a.hi>=0?0:std::min(a.lo*a.lo,a.hi*a.hi);
            const double lo=std::max(0.,AdjacentBinary64(low,-std::numeric_limits<double>::infinity()));
            const double hi=AdjacentBinary64(std::max(a.lo*a.lo,a.hi*a.hi),std::numeric_limits<double>::infinity());
            out.lo=lo; out.hi=hi;
        }
        // Slots30/31 belong only to Norm. All callers use other output/input slots.
        __declspec(noinline) void Norm(I& out,const I& a,const I& b) {
            Square(scratch[30],a); Square(scratch[31],b); Add(scratch[30],scratch[30],scratch[31]);
            out.lo=std::max(0.,AdjacentBinary64(std::sqrt(std::max(0.,scratch[30].lo)),-std::numeric_limits<double>::infinity()));
            out.hi=AdjacentBinary64(std::sqrt(scratch[30].hi),std::numeric_limits<double>::infinity());
        }
        __declspec(noinline) void Reset(bool disk,double kinetic) {
            Set(p.h00,0); Set(p.h01,0); Set(p.h11,0); Set(p.g0,0); Set(p.g1,0);
            p.u=glm::dvec3(0); p.v=glm::dvec3(0);
            p.s=p.t=p.x=p.y=p.radius=p.objective=p.gap=p.z=p.w=0;
            p.disk=disk; p.oldFeasible=p.projectedFeasible=false; p.tau=1e-5*std::max(1.,kinetic);
        }
        __declspec(noinline) bool Prepare(const PD::Event& e,double work,double kinetic) {
            if (!Linked(e) || !(link.seen==1 || (p.disk && link.seen==3)) ||
                !(link.duration>0 && std::isfinite(link.duration)) || !std::isfinite(link.load) || link.load< -1e-6 ||
                !std::isfinite(work) || !std::isfinite(kinetic) || !e.appliedImpulsesKnown) return false;
            link.seen|=p.disk?4u:2u;
            const double coefficient=p.disk?std::min(e.a->GetRollingResistanceLength(),e.b->GetRollingResistanceLength()):
                std::min(e.a->GetSpinResistanceLength(),e.b->GetSpinResistanceLength());
            p.radius=coefficient*std::max(0.,link.load);
            return coefficient>0 && std::isfinite(p.radius);
        }
        __declspec(noinline) bool Basis(const PD::Event& e) {
            const double length2=glm::dot(glm::dvec3(link.normal),glm::dvec3(link.normal));
            if (!(length2>0) || !std::isfinite(length2)) return false;
            const auto normal=glm::dvec3(link.normal)/std::sqrt(length2);
            if (e.normal!=Vec3f(normal)) return false;
            p.u=p.disk?normal:glm::dvec3(Vec3f(normal));
            return true;
        }
        __declspec(noinline) bool RollingBasis(const PD::Event& e) {
            // Preserve the normalized double normal in p.v while constructing u.
            p.v=p.u;
            const auto absolute=glm::abs(p.v);
            const auto seed=absolute.x<=absolute.y && absolute.x<=absolute.z?glm::dvec3(1,0,0):
                (absolute.y<=absolute.z?glm::dvec3(0,1,0):glm::dvec3(0,0,1));
            p.u=glm::normalize(glm::cross(p.v,seed)); p.v=glm::cross(p.v,p.u);
            return e.tangentU==Vec3f(p.u) && e.tangentV==Vec3f(p.v);
        }
        __declspec(noinline) bool Accumulators(const PD::Event& e,double work) {
            auto& summary=p.disk?rolling:spin;
            p.s=e.accumulatedBefore[0]; p.t=p.disk?e.accumulatedBefore[1]:0;
            p.x=e.accumulatedAfter[0]; p.y=p.disk?e.accumulatedAfter[1]:0;
            if (!std::isfinite(p.s) || !std::isfinite(p.t) || !std::isfinite(p.x) || !std::isfinite(p.y)) return false;
            const double oldLength=p.disk?std::hypot(p.s,p.t):std::abs(p.s),length=p.disk?std::hypot(p.x,p.y):std::abs(p.x);
            const double allowance=1e-5*std::max(1.,std::abs(link.load));
            p.oldFeasible=oldLength<=p.radius+allowance;
            if (!p.oldFeasible) {
                ++summary.forced;
                if (work>0) { ++summary.positive; summary.positiveWork+=work; }
            }
            return std::isfinite(summary.positiveWork) && length<=p.radius+allowance &&
                e.linearImpulseA==Vec3f(0) && e.linearImpulseB==Vec3f(0) && e.angularImpulseA==-e.angularImpulseB;
        }
        __declspec(noinline) bool Impulses(const PD::Event& e) const {
            const auto delta=p.u*(p.x-p.s)+p.v*(p.y-p.t);
            for (unsigned i=0;i<3;++i) if (!Near(e.angularImpulseB[i],delta[i])) return false;
            return !p.disk || (Near(p.x-p.s,glm::dot(p.u,glm::dvec3(e.angularImpulseB))) &&
                              Near(p.y-p.t,glm::dot(p.v,glm::dvec3(e.angularImpulseB))));
        }
        // Physical quadratic, preserving A/B then i/j order. Slots0..5 are local
        // work; no production optimizer or body/Event copy is used.
        __declspec(noinline) bool Body(const RigidBody3D& body,const PD::BodyState& before,bool second) {
            const bool moving=body.GetInverseMass()>0;
            if (!moving) return body.Type!=Component::BodyType::Kinematic || before.sleeping ||
                (before.angular==Vec3f(0) && before.linear==Vec3f(0));
            if (before.sleeping && before.angular!=Vec3f(0)) return false;
            // Synchronous source has already populated this exact pose's cache.
            const auto inverse=body.GetInverseInertiaTensorWorldSpace();
            for (unsigned i=0;i<3;++i) {
                Set(scratch[0],(second?1.:-1.)*double(before.angular[i]));
                Set(scratch[1],p.u[i]); Mul(scratch[2],scratch[1],scratch[0]); Add(p.g0,p.g0,scratch[2]);
                if (p.disk) { Set(scratch[1],p.v[i]); Mul(scratch[2],scratch[1],scratch[0]); Add(p.g1,p.g1,scratch[2]); }
                for (unsigned j=0;j<3;++j) {
                    Set(scratch[0],inverse[j][i]); Set(scratch[1],p.u[i]);
                    Mul(scratch[2],scratch[1],scratch[0]); Set(scratch[3],p.u[j]);
                    Mul(scratch[2],scratch[2],scratch[3]); Add(p.h00,p.h00,scratch[2]);
                    if (p.disk) {
                        Set(scratch[1],p.u[i]); Mul(scratch[2],scratch[1],scratch[0]); Set(scratch[3],p.v[j]); Mul(scratch[2],scratch[2],scratch[3]);
                        Set(scratch[1],p.v[i]); Mul(scratch[4],scratch[1],scratch[0]); Set(scratch[3],p.u[j]); Mul(scratch[4],scratch[4],scratch[3]);
                        Add(scratch[2],scratch[2],scratch[4]); Set(scratch[5],.5); Mul(scratch[2],scratch[2],scratch[5]); Add(p.h01,p.h01,scratch[2]);
                        Set(scratch[1],p.v[i]); Mul(scratch[2],scratch[1],scratch[0]); Set(scratch[3],p.v[j]);
                        Mul(scratch[2],scratch[2],scratch[3]); Add(p.h11,p.h11,scratch[2]);
                    }
                }
            }
            return true;
        }
        __declspec(noinline) void Shift() {
            Set(scratch[0],p.s); Mul(scratch[1],p.h00,scratch[0]); Sub(p.g0,p.g0,scratch[1]);
            Set(scratch[0],p.t); Mul(scratch[1],p.h01,scratch[0]); Sub(p.g0,p.g0,scratch[1]);
            if (p.disk) {
                Set(scratch[0],p.s); Mul(scratch[1],p.h01,scratch[0]); Sub(p.g1,p.g1,scratch[1]);
                Set(scratch[0],p.t); Mul(scratch[1],p.h11,scratch[0]); Sub(p.g1,p.g1,scratch[1]);
            }
        }
        // Slots0..8 are temporaries; outputs24/25/26 retain actual/prior/projected.
        __declspec(noinline) void Objective(I& out,double x,double y) {
            Set(scratch[0],x); Mul(scratch[2],p.g0,scratch[0]);
            if (!p.disk) {
                Square(scratch[5],scratch[0]); Mul(scratch[6],p.h00,scratch[5]);
                Set(scratch[7],.5); Mul(scratch[6],scratch[6],scratch[7]); Add(out,scratch[2],scratch[6]); return;
            }
            Set(scratch[1],y); Mul(scratch[3],p.g1,scratch[1]); Add(scratch[4],scratch[2],scratch[3]);
            Square(scratch[5],scratch[0]); Mul(scratch[6],p.h00,scratch[5]);
            Set(scratch[7],2); Mul(scratch[8],scratch[7],p.h01); Mul(scratch[8],scratch[8],scratch[0]); Mul(scratch[8],scratch[8],scratch[1]);
            Add(scratch[6],scratch[6],scratch[8]); Square(scratch[5],scratch[1]); Mul(scratch[8],p.h11,scratch[5]); Add(scratch[6],scratch[6],scratch[8]);
            Set(scratch[7],.5); Mul(scratch[6],scratch[6],scratch[7]); Add(out,scratch[4],scratch[6]);
        }
        __declspec(noinline) bool QuadraticValid() {
            if (!p.h00.Finite() || !p.g0.Finite() || p.h00.lo<=0 || !scratch[24].Finite() || !scratch[25].Finite()) return false;
            if (p.disk) {
                Mul(scratch[0],p.h00,p.h11); Square(scratch[1],p.h01); Sub(scratch[2],scratch[0],scratch[1]);
                if (!p.h01.Finite() || !p.h11.Finite() || !p.g1.Finite() || p.h11.lo<=0 ||
                    !scratch[2].Finite() || scratch[2].lo<0) return false;
            }
            return true;
        }
        __declspec(noinline) void Projection() {
            p.z=p.x; p.w=p.y;
            if (p.radius==0) p.z=p.w=0;
            else if (!p.disk) p.z=std::clamp(p.z,-p.radius,p.radius);
            else {
                Set(scratch[1],p.z); Set(scratch[2],p.w); Norm(scratch[0],scratch[1],scratch[2]);
                const double lengthUpper=scratch[0].hi;
                if (std::isfinite(lengthUpper) && lengthUpper>p.radius) {
                    const double scale=AdjacentBinary64(p.radius/lengthUpper,0.);
                    p.z=AdjacentBinary64(p.z*scale,0.); p.w=AdjacentBinary64(p.w*scale,0.);
                    for (unsigned refinement=0;refinement<8;++refinement) {
                        Set(scratch[1],p.z); Set(scratch[2],p.w); Norm(scratch[0],scratch[1],scratch[2]);
                        if (!(scratch[0].hi>p.radius)) break;
                        p.z=AdjacentBinary64(p.z,0.); p.w=AdjacentBinary64(p.w,0.);
                    }
                }
            }
            if (p.disk && !(p.z==0 && p.w==0)) {
                Set(scratch[1],p.z); Set(scratch[2],p.w); Norm(scratch[0],scratch[1],scratch[2]);
            }
            p.projectedFeasible=p.disk?((p.z==0 && p.w==0) || scratch[0].hi<=p.radius):std::abs(p.z)<=p.radius;
        }
        __declspec(noinline) void GradientGap() {
            Set(scratch[0],p.z); Mul(scratch[1],p.h00,scratch[0]); Add(scratch[27],p.g0,scratch[1]);
            Set(scratch[0],p.w); Mul(scratch[1],p.h01,scratch[0]); Add(scratch[27],scratch[27],scratch[1]);
            if (p.disk) {
                Set(scratch[0],p.z); Mul(scratch[1],p.h01,scratch[0]); Add(scratch[28],p.g1,scratch[1]);
                Set(scratch[0],p.w); Mul(scratch[1],p.h11,scratch[0]); Add(scratch[28],scratch[28],scratch[1]);
                Set(scratch[3],p.z); Mul(scratch[4],scratch[27],scratch[3]);
                Set(scratch[3],p.w); Mul(scratch[5],scratch[28],scratch[3]); Add(scratch[4],scratch[4],scratch[5]);
                Norm(scratch[6],scratch[27],scratch[28]); Set(scratch[3],p.radius); Mul(scratch[5],scratch[3],scratch[6]); Add(scratch[29],scratch[4],scratch[5]);
            } else {
                Set(scratch[3],p.z); Mul(scratch[4],scratch[27],scratch[3]);
                Set(scratch[3],p.radius); Set(scratch[6],I::AbsUpper(scratch[27])); Mul(scratch[5],scratch[3],scratch[6]); Add(scratch[29],scratch[4],scratch[5]);
            }
        }
        __declspec(noinline) bool CertificateResult(double work) {
            p.objective=scratch[24].hi;
            Sub(scratch[0],scratch[24],scratch[26]);
            p.gap=AdjacentBinary64(std::max(0.,scratch[29].hi)+I::AbsUpper(scratch[0]),std::numeric_limits<double>::infinity());
            Sub(scratch[0],scratch[24],scratch[25]); Set(scratch[1],work); Sub(scratch[0],scratch[0],scratch[1]);
            return p.projectedFeasible && scratch[26].Finite() && scratch[29].Finite() && std::isfinite(p.gap) && scratch[0].Finite() &&
                I::AbsUpper(scratch[0])<=p.tau && scratch[24].hi<=p.tau && p.gap<=p.tau;
        }
        __declspec(noinline) bool Finish(bool valid) {
            auto& summary=p.disk?rolling:spin;
            if (std::isfinite(p.objective)) summary.objectiveRatio=std::max(summary.objectiveRatio,std::max(0.,p.objective/p.tau));
            if (std::isfinite(p.gap)) summary.gapRatio=std::max(summary.gapRatio,std::max(0.,p.gap/p.tau));
            if (valid) ++summary.certified; else ++summary.failures;
            return valid;
        }
        __declspec(noinline) bool Check(const PD::Event& e,double work,double kinetic) {
            const bool disk=e.operation==PD::Operation::Rolling;
            ++(disk?rolling:spin).events; Reset(disk,kinetic);
            if (!Prepare(e,work,kinetic) || !Basis(e) || (disk && !RollingBasis(e)) ||
                !Accumulators(e,work) || !Impulses(e)) return Finish(false);
            if (!Body(*e.a,e.beforeA,false) || !Body(*e.b,e.beforeB,true)) return Finish(false);
            Shift();
            Objective(scratch[24],p.x,p.y); Objective(scratch[25],p.s,p.t);
            bool certificate=false;
            if (QuadraticValid()) {
                Projection(); Objective(scratch[26],p.z,p.w); GradientGap();
                certificate=CertificateResult(work);
            }
            return Finish(certificate && (!p.oldFeasible || work<=p.tau));
        }
        static void Header(std::ostream& out) {
            for (const auto* prefix:{"spin_","rolling_"})
                for (const auto* field:{"events","certified","forced_retractions","positive_retractions","positive_work","max_objective_ratio","max_gap_ratio","failures"}) out<<','<<prefix<<field;
        }
        void Csv(std::ostream& out) const {
            for (const auto* s:{&spin,&rolling}) out<<','<<s->events<<','<<s->certified<<','<<s->forced<<','<<s->positive<<','
                <<s->positiveWork<<','<<s->objectiveRatio<<','<<s->gapRatio<<','<<s->failures;
        }
        void Json(std::ostream& out) const {
            unsigned i=0; for (const auto* s:{&spin,&rolling}) {
                const char* prefix=i++?"rolling_":"spin_";
                const auto value=[&](const char* key,auto v) { out<<",\""<<prefix<<key<<"\":"<<v; };
                value("events",s->events); value("certified",s->certified); value("forced_retractions",s->forced);
                value("positive_retractions",s->positive); value("positive_work",s->positiveWork);
                value("max_objective_ratio",s->objectiveRatio); value("max_gap_ratio",s->gapRatio); value("failures",s->failures);
            }
        }
    };
    static_assert(sizeof(AngularAudit)<=1024,"bounded angular certificate scratch and summaries");
    static_assert(sizeof(double)==8 && sizeof(std::uint64_t)==8 && std::endian::native==std::endian::little &&
        std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::radix==2 &&
        std::numeric_limits<double>::digits==53 && std::numeric_limits<double>::min_exponent== -1021 &&
        std::numeric_limits<double>::max_exponent==1024 &&
        std::bit_cast<std::uint64_t>(std::numeric_limits<double>::denorm_min())==1ull &&
        std::bit_cast<std::uint64_t>(1.)==0x3ff0000000000000ull &&
        std::bit_cast<std::uint64_t>(-0.)==0x8000000000000000ull &&
        std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity())==0x7ff0000000000000ull,
        "pinned IEEE binary64 adjacent-value representation");
    static_assert([] {
        // Independent literal [input,target,expected] encodings. No runtime test
        // process, physical event or storage is added by these compile-time cases.
        constexpr std::array<std::array<std::uint64_t,3>,34> cases{{
            {0x0000000000000000ull,0x7ff0000000000000ull,0x0000000000000001ull},
            {0x8000000000000000ull,0x7ff0000000000000ull,0x0000000000000001ull},
            {0x0000000000000000ull,0xfff0000000000000ull,0x8000000000000001ull},
            {0x8000000000000000ull,0xfff0000000000000ull,0x8000000000000001ull},
            {0x0000000000000000ull,0x8000000000000000ull,0x8000000000000000ull},
            {0x8000000000000000ull,0x0000000000000000ull,0x0000000000000000ull},
            {0x0000000000000000ull,0x0000000000000000ull,0x0000000000000000ull},
            {0x8000000000000000ull,0x8000000000000000ull,0x8000000000000000ull},
            {0x0000000000000001ull,0x0000000000000000ull,0x0000000000000000ull},
            {0x8000000000000001ull,0x0000000000000000ull,0x8000000000000000ull},
            {0x0000000000000001ull,0xfff0000000000000ull,0x0000000000000000ull},
            {0x8000000000000001ull,0x7ff0000000000000ull,0x8000000000000000ull},
            {0x000fffffffffffffull,0x7ff0000000000000ull,0x0010000000000000ull},
            {0x0010000000000000ull,0x0000000000000000ull,0x000fffffffffffffull},
            {0x800fffffffffffffull,0xfff0000000000000ull,0x8010000000000000ull},
            {0x8010000000000000ull,0x0000000000000000ull,0x800fffffffffffffull},
            {0x3ff0000000000000ull,0x7ff0000000000000ull,0x3ff0000000000001ull},
            {0x3ff0000000000000ull,0x0000000000000000ull,0x3fefffffffffffffull},
            {0xbff0000000000000ull,0x7ff0000000000000ull,0xbfefffffffffffffull},
            {0xbff0000000000000ull,0xfff0000000000000ull,0xbff0000000000001ull},
            {0x7fefffffffffffffull,0x7ff0000000000000ull,0x7ff0000000000000ull},
            {0xffefffffffffffffull,0xfff0000000000000ull,0xfff0000000000000ull},
            {0x7ff0000000000000ull,0x0000000000000000ull,0x7fefffffffffffffull},
            {0xfff0000000000000ull,0x0000000000000000ull,0xffefffffffffffffull},
            {0x7ff0000000000000ull,0xfff0000000000000ull,0x7fefffffffffffffull},
            {0xfff0000000000000ull,0x7ff0000000000000ull,0xffefffffffffffffull},
            {0x7ff0000000000000ull,0x7ff0000000000000ull,0x7ff0000000000000ull},
            {0xfff0000000000000ull,0xfff0000000000000ull,0xfff0000000000000ull},
            {0x3ff0000000000000ull,0x3ff0000000000000ull,0x3ff0000000000000ull},
            {0xbff0000000000000ull,0xbff0000000000000ull,0xbff0000000000000ull},
            {0x7ff8000000000001ull,0x7ff0000000000000ull,0x7ff8000000000001ull},
            {0xfff8000000000001ull,0x0000000000000000ull,0xfff8000000000001ull},
            {0x3ff0000000000000ull,0x7ff8000000000001ull,0x7ff8000000000001ull},
            {0x7ff8000000000001ull,0xfff8000000000001ull,0x7ff8000000000001ull}
        }};
        for (const auto& c:cases)
            if (std::bit_cast<std::uint64_t>(AngularAudit::AdjacentBinary64(std::bit_cast<double>(c[0]),
                    std::bit_cast<double>(c[1])))!=c[2]) return false;
        return true;
    }(),"independent binary64 adjacent boundary vectors");

    struct PostCandidateAudit
    {
        struct EventRecord { PD::Event event{}; unsigned episode{},tick{}; std::uint64_t sequence{};
            std::array<std::uint64_t,4> identity{}; };
        struct StageRecord { PD::BodyState state{}; unsigned episode{},tick{},stage{}; std::uint64_t slot{},generation{}; };
        struct Cache { const RigidBody3D* a{}; const RigidBody3D* b{}; Vec3f localA{},localB{};
            std::array<double,3> lambda{}; std::uintptr_t token{}; bool present{},projected{},warm{},reduced{}; };
        struct Tick { unsigned episode{},tick{},sliding{},legacyPositive{},infeasible{},explained{};
            double maxF{},maxGap{},maxIncrement{},maxIntermediateCone{},beforeK{},afterK{},work{},balance{}; };
        std::array<EventRecord,1024> events{};
        std::array<StageRecord,288> stages{};
        std::array<Tick,128> pending{};
        std::array<Cache,4> cache{};
        std::array<PD::BodyState,2> responsePose{};
        Tick current{};
        unsigned eventCount{},stageCount{},eventWritten{},stageWritten{},pendingCount{},ticks{},sliding{},legacyPositive{},explained{};
        std::uint64_t sequence{};
        double responseWorkStart{};
        bool responseOpen{},responseComplete{};
    };
    static_assert(sizeof(PostCandidateAudit)<=1024*1024,"accepted one MiB additional diagnostic storage");

    // P21_BEGIN: opt-in fixture storage; no ordinary application or Physics policy.
    struct Phase21Capture
    {
        struct Case { unsigned kind{},hz{120},passes{1},ticks{}; bool sleep{true}; const char* name{}; };
        std::array<Case,3> cases{};
        unsigned count{},used{};
        bool pending{},impulsePending{},impulseApplied{},observed{};
        struct Row {
            unsigned tick{},count{},contacts{},islands{},quiet{},sleeping{},wakes{},sleeps{};
            std::uint64_t hash{14695981039346656037ull},capacity{},growth{},callbackNs{},eventStart{},eventNs{};
            std::array<std::uint64_t,25> cpu{};
            std::array<std::uint64_t,7> sleepCpu{};
            std::array<std::uint64_t,24> operations{};
            std::array<double,24> kineticChange{},potentialChange{};
            double energy{},kinetic{},potential{},penetration{},linear{},angular{},drift{},normalImpulse{},frictionImpulse{},restitution{};
            std::array<PD::BodyState,185> body{};
            std::array<std::uint64_t,185> slots{},generations{};
            std::array<unsigned,185> bodyContacts{};
            std::array<double,185> bodyPenetration{};
            PhysicsProfileSnapshot before{};
        };
        std::array<Row,32> rows{};
        // Accepted P21-PEN-01: current-tick values only, no Physics mutations.
        struct PenContact {
            unsigned a{},b{},manifold{},slot{},featureA{},featureB{};
            Vec3f cachedA{},cachedB{},localA{},localB{},normal{},currentA{},currentB{};
            float separation{},toi{}; double depth{};
        };
        struct PenStage {
            unsigned stage{},tick{},count{},contacts{};
            std::array<PD::BodyState,185> body{};
            std::array<std::uint64_t,185> slots{},generations{};
            std::array<PenContact,1024> contact{};
        };
        struct PenPosition {
            unsigned a{},b{},slot{};
            Vec3f beforeA{},afterA{},beforeB{},afterB{},normal{},anchorA{},anchorB{},localA{},localB{};
            double depth{};
        };
        struct PenPacket {
            std::array<PenStage,4> stages{};
            std::array<PenPosition,1024> positions{};
            unsigned positionCount{},source{},a{},b{},manifold{},slot{};
            double ground{};
            bool correcting{},written{};
        } pen;
        static_assert(sizeof(PenPacket)<=1024*1024,"Phase21 first-failure packet storage");
        // Amendment02: fixed-fixture, End-state accounting only. Pair keys never
        // depend on manifold slots; pinned full identities are checked each tick.
        struct PairState {
            double anchor{},peak{};
            unsigned seen{},first{},safeStart{},run{},exposure{},episodes{},totalExposure{},firstUnsafe{},lastUnsafe{},lastObserved{};
            std::uint16_t a{},b{};
            bool hasAnchor{},durationViolation{};
        };
        static_assert(sizeof(PairState)==64,"frozen pair-state capacity");
        struct PairStorage {
            struct Difference { double maximum{}; unsigned samples{},first{},last{}; };
            std::array<PairState,17020> state{}; // 185 choose 2; ten static/static entries unused
            std::array<Difference,17020> differences{};
            std::array<std::uint16_t,17020> work{},pending{};
            std::array<std::uint64_t,185> slots{},generations{};
            std::array<unsigned,185> quietTicks{};
            std::array<bool,185> quiet{},priorQuiet{};
            std::array<char,4096> traceBuffer{},eventBuffer{};
            unsigned workCount{},pendingCount{};
            bool closed{};
        } pairs;
        static_assert(sizeof(PairStorage)+2*sizeof(std::ofstream)+32*185*sizeof(double)<=2*1024*1024,"bounded incremental pair and penetration storage");
        inline static constexpr std::array<std::pair<unsigned,unsigned>,21> HistoricalPairs{{
            {143,179},{144,180},{145,181},{146,182},{141,177},{131,167},{132,168},
            {133,169},{136,172},{125,161},{126,162},{127,163},{128,164},{130,166},
            {120,156},{121,157},{122,158},{124,160},{115,151},{116,152},{118,154}}};
        std::ofstream pairTrace,pairEvents;
        // Amendment03: bounded attribution, using existing value-only callbacks.
        struct Attribution {
            struct Origin {
                Vec3f localA{},localB{},normal{},anchorA{},anchorB{},positionA{},positionB{};
                Quat rotationA{1,0,0,0},rotationB{1,0,0,0};
                unsigned a{},b{},born{},seen{};
                Vec3f localNormal{};
            };
            std::array<Origin,1024> origins{};
            std::array<std::uint16_t,2048> originIndex{};
            struct Example {
                Origin origin{};
                unsigned episode{},tick{},category{};
                std::array<std::uint64_t,4> identity{};
                Vec3f positionA{},positionB{},normal{};
                Vec3f currentAnchorA{},currentAnchorB{};
                Quat rotationA{1,0,0,0},rotationB{1,0,0,0};
                double raw{},geometry{},projection{},witness{},arithmetic{},clamping{};
            };
            std::array<Example,10> sample{};
            struct EnergyExample {
                PD::Event event{};
                unsigned episode{},tick{},a{},b{};
                std::array<std::uint64_t,4> identity{};
                double expected{},measured{},kinetic{};
            };
            std::array<EnergyExample,3> energySample{};
            std::array<unsigned char,17020> pairClass{};
            std::array<std::uint64_t,5> contacts{},differences{};
            std::array<double,5> worst{};
            std::array<double,24> expected{},measured{};
            std::array<double,12> stageEnergy{};
            std::array<Mat3,185> beginRotation{};
            unsigned originsUsed{},stageMask{};
            double beginEnergy{},beginKinetic{},impulseError{},workError{};
            std::uint64_t checks{},failures{},rows{},examples{};
            std::ofstream ledger,packets,energyPackets;
        } audit;
        static_assert(sizeof(Attribution)<=262144,"bounded attribution within existing pair allocation");
        static_assert(sizeof(PairStorage)+sizeof(Attribution)+2*sizeof(std::ofstream)+32*185*sizeof(double)<=2*1024*1024,"amendment03 pair and attribution capacity");
        struct CausalCapture {
            struct Warm {
                unsigned a{},b{},slot{},born{};
                std::uint64_t featureA{},featureB{},token{};
                Vec3f localA{},localB{},normal{};
            };
            struct Ordered { unsigned a{},b{},slot{}; int warm{-1}; };
            struct Contact { unsigned a{},b{},slot{}; std::uint64_t featureA{},featureB{}; Vec3f localA{},localB{},normal{}; };
            std::array<Warm,64> warm{};
            std::array<Contact,64> contacts{};
            std::array<Ordered,1024> order{};
            std::array<PD::BodyState,185> shadow{};
            unsigned contactUsed{},warmUsed{},orderUsed{},cursor{},stage{},sequence{},events{},positions{},errors{},completedWindows{};
            std::array<unsigned,2> stageMasks{};
            std::ofstream operations,corrections,stages;
        } causal;
        static_assert(sizeof(CausalCapture)<=131072,"bounded two-tick causal scratch");
        static_assert(sizeof(PairStorage)+sizeof(Attribution)+sizeof(CausalCapture)+2*sizeof(std::ofstream)+32*185*sizeof(double)<=2*1024*1024,"causal capture within original incremental memory limit");
        struct Refinement {
            struct Work {
                std::array<double,8> graph{};
                std::array<double,5> rejected{};
                std::array<double,3> bodiesPointsManifolds{},components{},attempts{},inner{},final{};
                std::array<std::uint64_t,3> operations{};
                std::array<double,3> expected{},measured{};
                std::uint64_t mask{},flags{},scratch{},capacity{},constraint{},rollingBisections{},blocks{};
                double blockMasks{},preflights{},upward{};
                std::uint64_t orderHash{14695981039346656037ull},orderedEvents{},graphNs{};
                unsigned localDirection{};
                AngularAudit angular;
            } current;
            std::ofstream work,direct;
            std::uint64_t directChecks{},directFailures{},summaryTicks{},eligibleTicks{},extraEvents{};
            double minY{},maxY{},restEnergy{};
        } refinement;
        bool amended{},longTail{},recovery03{},causalMode{},refined{},renderedSupport{};
        std::uint64_t durationViolations{},durationViolationSamples{},pendingAtEnd{};
        std::uint64_t pairBytes{4096}; // also reserves bounded capacity metadata
        std::string run;
        std::uint64_t totalTicks{},exportBytes{},packetBytes{},caseMetadataBytes{};
        double discardedWallSeconds{};
    };
    static_assert(sizeof(Phase21Capture)<=8*1024*1024,"Phase21 additional observer storage");
    // P21_END

    struct Session
    {
        RestingContactDiagnostic config;
        PD::Observer sink{};
        _Scene* scene{};
        SceneAssets assets;
        std::vector<BodyInput> lattice, inputs;
        std::vector<_Entity> entities;
        std::vector<PD::BodyState> previous;
        std::ofstream bodies, stages, contacts, timing, metrics, events, frames, initial, islandRows;
        unsigned episode{}, tick{}, completedEpisodes{};
        std::uint64_t sequence{}, eventCount{}, wakeCount{}, sleepCount{};
        std::uint64_t stageClock{}, stepClock{}, recorderNs{}, callbackNs{}, detailedNs{};
        std::size_t lastStorage{}, storageGrowth{};
        std::size_t debugLineCapacity{32768};
        bool failed{}, episodeDone{}, complete{};
        // Explicit Phase20 request only. These flags select fixture assertions,
        // never a production algorithm or numerical policy.
        bool conditional{}, candidate{}, queriesComplete{},postPolicy{},postFailureLogged{};
        std::unique_ptr<PostCandidateAudit> postAudit;
        std::ofstream postEvents,postStages,postTicks;
        unsigned collisionCalls{}, directCalls{}, caCalls{}, projectionCalls{}, physicalFailures{};
        std::uint64_t completedTicks{};
        std::uint64_t queryOperandExportBytes{};
        std::uint64_t profiledTicks{},physicsWorldNs{};
        bool overflow{};
        _Scene* applicationScene{};
        std::unique_ptr<_Scene> sixtyHzScene;
        std::vector<std::unique_ptr<PhysicalShape>> ownedShapes;
        std::ofstream queryRows, queryInputs, helperRows, cpuRows, qualificationRows;
        inline static constexpr std::array<const char*,21> GateNames{"normal_nonnegative","coulomb","friction_energy",
            "zero_resistance","impulse_equation","sphere_energy_residual","rest_penetration","rest_height_range",
            "rest_vertical_speed","upward_correction","quiet_window","unexplained_wake","rest_energy",
            "missed_penetrating_contact","impact_response","accumulated_friction_energy","friction_optimality",
            "friction_cache_continuity","persistent_response_energy","persistent_response_balance","friction_certificate"};
        std::array<std::uint64_t,GateNames.size()> gateChecks{},gateFailures{};
        struct FailedOperand { unsigned episode{},tick{},gate{}; double value{},limit{}; PD::Event event{};
            std::array<std::uint64_t,4> identity{}; };
        std::array<FailedOperand,GateNames.size()*2> failureExamples{};
        std::size_t failureExampleCount{},failureExampleWritten{};
        struct ConditionalTick {
            unsigned episode{},tick{},bodies{},contacts{},active{},sleeping{},quiet{},wakes{},sleeps{},eligible{},missed{};
            double energy{},kinetic{},potential{},penetration{},maxLinear{},maxAngular{},y{},vy{},upwardCorrection{};
            double gravityWork{},responseWork{},responseMeasured{},integrationPotential{},integrationKinetic{},positionPotential{},sleepWork{},residual{};
            double minNormal{},maxCoulombExcess{},maxFrictionWork{},maxImpulseError{};
            std::uint64_t events{},observerNs{},capacityBytes{},capacityGrowth{},boundaryNs{},eventSinkNs{},eventObservationNs{};
        } currentTick{},pendingTick{};
        PhysicsProfileSnapshot profileBefore{};
        std::uint64_t eventObservationBefore{};
        bool tickPending{};
        double previousEnergy{},previousKinetic{},restWindowEnergy{},restMinY{},restMaxY{};
        double toiNormalImpulse{};
        const RigidBody3D* toiBodyA{};
        const RigidBody3D* toiBodyB{};
        unsigned impactResponses{};
        std::string failure;
        // P21_BEGIN
        std::unique_ptr<Phase21Capture> phase21;

        bool OpenPhase21(const char* request)
        {
            const auto expected=(std::filesystem::path(config.outputDirectory).parent_path()/"phase21-request.txt").lexically_normal();
            const auto path=std::filesystem::path(request);
            std::ifstream in(path); std::string magic,run,end;
            if (!path.is_absolute() || path.lexically_normal()!=expected || !(in>>magic>>run>>end) ||
                (magic!="P21_GAME_ENGINE_01" && magic!="P21_GAME_ENGINE_02" && magic!="P21_GAME_ENGINE_03" && magic!="P21_LONG_TAIL_01" && magic!="P21_L60_CAUSAL_01" && magic!="P21_COUPLED_REFINEMENT_01") || end!="END") { Fail("invalid Phase21 request"); return false; }
            in>>std::ws; if (!in.eof()) { Fail("Phase21 trailing request fields"); return false; }
            phase21.reset(new(std::nothrow) Phase21Capture);
            if (!phase21) { Fail("Phase21 observer allocation"); return false; }
            auto& p=*phase21; p.run=run; p.observed=config.observation==Observation::Observed; p.amended=magic=="P21_GAME_ENGINE_02";
            p.refined=magic=="P21_COUPLED_REFINEMENT_01"; p.renderedSupport=p.refined && run=="spherebox-support";
            p.longTail=magic=="P21_LONG_TAIL_01"; p.recovery03=magic=="P21_GAME_ENGINE_03" || (p.refined && run!="prefix");
            p.causalMode=magic=="P21_L60_CAUSAL_01"; p.recovery03=p.recovery03 || p.causalMode;
            if (p.causalMode!=(run=="L60-causal")) { Fail("Phase21 causal request identity"); return false; }
            if (p.longTail!=(run=="long-tail")) { Fail("Phase21 diagnostic request identity"); return false; }
            p.amended=p.amended || p.longTail || p.recovery03 || p.refined;
            if (p.amended && run=="penetration") { Fail("consumed diagnostic cannot use qualification authority"); return false; }
            using C=Phase21Capture::Case;
            if (run=="debug") { p.count=3; p.cases={C{0,120,1,1800,true,"debug-lattice"},C{1,60,1,1200,true,"stack60"},C{2,60,1,600,true,"wake60"}}; }
            else if (p.renderedSupport) { p.count=2; p.cases[0]=C{3,60,1,1200,true,"SphereBox-support"}; p.cases[1]=C{4,60,1,1200,true,"SphereBoxStack-support"}; }
            else if (run=="L60-causal") { p.count=1; p.cases[0]=C{0,60,1,73,true,"L60-causal-diagnostic"}; }
            else if (run=="penetration") { p.count=1; p.cases[0]=C{0,120,1,1800,true,"penetration-diagnostic"}; }
            else if (run=="small") { p.count=3; p.cases={C{1,60,1,1200,true,"stack60"},C{1,120,1,2400,true,"stack120"},C{2,60,1,600,true,"wake60"}}; }
            else if (run=="prefix" || run=="L120" || run=="L60" || run=="LA" || run=="LP" || run=="long-tail") {
                const unsigned hz=run=="L60"?60u:120u; p.count=1;
                p.cases[0]={0,hz,run=="LP"?2u:1u,(run=="prefix"?15u:120u)*hz,run!="LA",nullptr};
                p.cases[0].name=p.run.c_str();
            } else { Fail("unknown Phase21 case"); return false; }
            if ((run=="prefix")==p.observed || !LoadLattice(config.latticePath,lattice)) { Fail("Phase21 mode/input mismatch"); return false; }
            const auto dir=std::filesystem::path(config.outputDirectory); std::error_code ec;
            if (!std::filesystem::is_directory(dir,ec) || ec) return false;
            for (const char* name:{"bodies.bin","metrics.csv","frames.csv","initial.csv","episodes.csv","impulse.csv","result.json","first-failure.bin","pair-trace.bin","pair-events.bin","pair-capacity.json","attribution.csv","anchor-examples.csv","energy-examples.csv","refinement-work.csv","direct-refinement.jsonl"})
                if (std::filesystem::exists(dir/name,ec) || ec) { Fail("Phase21 output exists"); return false; }
            bodies.open(dir/"bodies.bin",std::ios::binary); metrics.open(dir/"metrics.csv"); frames.open(dir/"frames.csv");
            initial.open(dir/"initial.csv"); events.open(dir/"episodes.csv"); timing.open(dir/"impulse.csv");
            for (auto* out:{&metrics,&frames,&initial,&events,&timing}) *out<<std::setprecision(17);
            metrics<<"episode,tick,bodies,contacts,islands,quiet,sleeping,wakes,sleeps,energy,kinetic,potential,penetration,max_linear,max_angular,drift,state_hash,capacity,growth,callback_ns,event_ns,normal_impulse,friction_impulse,restitution_sum";
            for (const auto& f:ProfileFields) metrics<<','<<f.first;
            for (unsigned i=0;i<7;++i) metrics<<",sleep_ns_"<<i;
            for (unsigned i=0;i<24;++i) metrics<<",op_count_"<<i<<",op_kinetic_"<<i<<",op_potential_"<<i;
            metrics<<'\n'; frames<<"episode,tick,elapsed,pending,discarded,total_steps,steps,alpha\n";
            initial<<"episode,index,slot,generation,shape,type,inverse_mass,elasticity,friction,layer,mask,radius,px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,spin_length,rolling_length,linear_sleep,angular_sleep,dwell,ixx,iyy,izz\n";
            events<<"episode,name,hz,passes,sleep,ticks,initial_energy,bodies\n";
            timing<<"episode,after_tick,sleeping_before,awake_after,energy_before,energy_after,impulse_x\n";
            failure.reserve(256);
            if (p.amended) {
                p.pairTrace.rdbuf()->pubsetbuf(p.pairs.traceBuffer.data(),p.pairs.traceBuffer.size());
                p.pairEvents.rdbuf()->pubsetbuf(p.pairs.eventBuffer.data(),p.pairs.eventBuffer.size());
                p.pairTrace.open(dir/"pair-trace.bin",std::ios::binary); p.pairEvents.open(dir/"pair-events.bin",std::ios::binary);
                std::ofstream capacity(dir/"pair-capacity.json");
                capacity<<"{\"policy\":\""<<(p.recovery03?"AMENDMENT_03":"AMENDMENT_02")<<"\",\"pair_state_bytes\":"<<sizeof(Phase21Capture::PairState)
                    <<",\"incremental_bytes\":"<<sizeof(Phase21Capture::PairStorage)+sizeof(Phase21Capture::Attribution)+sizeof(Phase21Capture::CausalCapture)+sizeof(Phase21Capture::Refinement)+2*sizeof(std::ofstream)+32*185*sizeof(double)
                    <<",\"observer_bytes\":"<<sizeof(Phase21Capture)<<",\"session_bytes\":"<<sizeof(Session)
                    <<",\"attribution_bytes\":"<<sizeof(Phase21Capture::Attribution)
                    <<",\"trace_record_bytes\":"<<(p.longTail?72:192)<<",\"body_record_bytes\":"<<((p.longTail || p.recovery03)?101:93)
                    <<",\"diagnostic_only\":"<<((p.longTail || p.causalMode)?"true":"false")<<",\"event_record_bytes\":64,\"pair_export_limit\":8388608}\n";
                if (!p.pairTrace || !p.pairEvents || !capacity) { Fail("Phase21 pair streams INCONCLUSIVE"); return false; }
            }
            sink.context=this; sink.boundary=&Boundary; sink.event=&Phase21Event;
            if (p.recovery03) {
                p.audit.ledger.open(dir/"attribution.csv"); p.audit.packets.open(dir/"anchor-examples.csv");
                p.audit.energyPackets.open(dir/"energy-examples.csv"); p.audit.energyPackets<<std::setprecision(17);
                p.audit.ledger<<std::setprecision(17); p.audit.packets<<std::setprecision(17);
                p.audit.ledger<<"episode,tick,begin_energy,end_energy,residual,allowance,impulse_error,work_error,checks,failures";
                for (unsigned i=0;i<24;++i) p.audit.ledger<<",expected_"<<i<<",measured_"<<i;
                for (unsigned i=0;i<12;++i) p.audit.ledger<<",stage_"<<i;
                for (unsigned i=1;i<5;++i) p.audit.ledger<<",class_contacts_"<<i<<",class_differences_"<<i<<",class_max_"<<i;
                p.audit.ledger<<'\n';
                if (!p.audit.ledger || !p.audit.packets || !p.audit.energyPackets) { Fail("Phase21 attribution streams INCONCLUSIVE"); return false; }
            }
            if (p.refined) {
                if (Phase21CompiledStorage[0]!=0x50323153544f5231ull || Phase21CompiledStorage[1]!=sizeof(Session) ||
                    Phase21CompiledStorage[2]!=sizeof(Phase21Capture) || Phase21CompiledStorage[3]!=sizeof(PostCandidateAudit)) {
                    Fail("Phase21 compiled storage certificate"); return false;
                }
                p.refinement.work.open(dir/"refinement-work.csv"); p.refinement.work<<std::setprecision(17);
                p.refinement.work<<"episode,tick,summary_mask,flags,graph_scratch_bytes,manifold_capacity,constraint_bytes,bodies,points,manifolds,components,eligible_components,eligible_points,unsupported_components,mixed_components,kinematic_components,sleeping_bodies,prepared_rows,pointer_comparisons,union_hops,compression_hops,extra_visits,normal_attempts,tangent_attempts,tangent_bisections,spin_attempts,rolling_attempts,rolling_bisections,block_attempts,block_masks,finite_preflights,finite_failure,normal_rejected,tangent_rejected,spin_rejected,rolling_rejected,block_rejected,ordinary_impulse_events,reverse_impulse_events,forward_impulse_events,ordinary_expected_work,reverse_expected_work,forward_expected_work,ordinary_measured_work,reverse_measured_work,forward_measured_work,upward_correction,ordered_impulse_events,operation_order_hash,graph_planning_ns";
                AngularAudit::Header(p.refinement.work); p.refinement.work<<'\n';
                if (!p.refinement.work) { Fail("Phase21 refinement evidence open"); return false; }
                if (run=="debug") {
                    p.refinement.direct.open(dir/"direct-refinement.jsonl");
                    if (!p.refinement.direct || !RunRefinementDirect()) return false;
                }
            }
            sink.detailed=p.observed || p.recovery03; sink.targeted=sink.detailed;
            if (p.causalMode && !CausalOpen(dir)) return false;
            return bool(bodies && metrics && frames && initial && events && timing);
        }
        // Bounded direct algebra/graph checks run inside C1 before its first
        // Scene tick. They never step a world, install a second Physics system,
        // mutate from an observer, or add a native process.
        struct RefinementDirectTrace {
            enum Failure : unsigned { MissingBodies=1, SummaryState=2, PositionTargets=4,
                PositionVelocityOrCap=8, ImpulsePose=16, LinearEquation=32, AngularEquation=64,
                NonfiniteWork=128, WorkBalance=256, NormalBound=512, CoulombBound=1024, ResistanceWork=2048 };
            unsigned graphs{},works{},finals{},warm{},reverse{},forward{},normal{},spin{},rolling{},blocks{},positions{};
            unsigned graphComponents{},eligibleComponents{},eligiblePoints{},visits{},flags{},finiteFailure{};
            bool valid{true};
            double expected{},measured{};
            // Direct checks precede scene observation. Reuse one existing heap packet;
            // never put an Event copy on the nested solver/callback stack.
            Phase21Capture::Attribution::EnergyExample* first{};
            unsigned failureMask{},firstMask{},sequence{};
            AngularAudit* angular{};
            bool failedNormalKnown{};
            double failedNormalLoad{},failedDuration{},failedSpinLength{},failedRollingLength{},failedFriction{};
            double failedLinearError{},failedAngularError{},failedInverseMassA{},failedInverseMassB{};
            static double Energy(const RigidBody3D& body,const PD::BodyState& state) {
                if (!(body.GetInverseMass()>0)) return 0;
                const auto rotation=glm::dmat3(glm::toMat3(state.orientation));
                const auto inertia=rotation*glm::dmat3(body.m_Shape->InertiaTensor())*glm::transpose(rotation)/double(body.GetInverseMass());
                return .5*glm::dot(glm::dvec3(state.linear),glm::dvec3(state.linear))/body.GetInverseMass()+
                    .5*glm::dot(glm::dvec3(state.angular),inertia*glm::dvec3(state.angular));
            }
            void CaptureFailure(const PD::Event& e,unsigned mask,double work,double observed,double kinetic,
                                double linearError,double angularError) noexcept {
                if (!mask) return;
                valid=false; failureMask|=mask;
                if (!first || first->tick) return;
                firstMask=mask; first->tick=sequence; first->episode=first->a=first->b=0;
                first->event=e; first->expected=work; first->measured=observed; first->kinetic=kinetic;
                // Borrowed pointers are consumed only here; the saved packet is value-only.
                first->identity={e.a?e.a->GetIdentity().GetSlot():0,e.a?e.a->GetIdentity().GetGeneration():0,
                    e.b?e.b->GetIdentity().GetSlot():0,e.b?e.b->GetIdentity().GetGeneration():0};
                first->event.a=first->event.b=nullptr;
                failedLinearError=linearError; failedAngularError=angularError; failedDuration=angular?angular->link.duration:0;
                const bool normalEvent=e.operation==PD::Operation::WarmStart || e.operation==PD::Operation::NormalSliding || e.operation==PD::Operation::BlockNormal;
                failedNormalKnown=normalEvent || (angular && angular->Linked(e));
                failedNormalLoad=normalEvent?e.accumulatedAfter[0]:(angular?angular->link.load:0);
                if (e.a && e.b) {
                    failedInverseMassA=e.a->GetInverseMass(); failedInverseMassB=e.b->GetInverseMass();
                    failedFriction=double(e.a->m_Friction)*e.b->m_Friction;
                    // Exact PreSolve pair policy, including its positive finite timestep guard.
                    failedSpinLength=failedDuration>0 && std::isfinite(failedDuration)?
                        std::min(e.a->GetSpinResistanceLength(),e.b->GetSpinResistanceLength()):0;
                    failedRollingLength=failedDuration>0 && std::isfinite(failedDuration)?
                        std::min(e.a->GetRollingResistanceLength(),e.b->GetRollingResistanceLength()):0;
                }
            }
            static void Observe(void* context,const PD::Event& e) noexcept {
                auto& t=*static_cast<RefinementDirectTrace*>(context); ++t.sequence;
                unsigned failed=0;
                const auto check=[&](bool pass,unsigned bit) { if (!pass) failed|=bit; };
                if (!e.a || !e.b) { t.CaptureFailure(e,MissingBodies,0,0,0,0,0); return; }
                if (t.angular) t.angular->Track(e);
                if (RefinementSummary(e)) {
                    check(PD::SameState(e.beforeA,e.afterA) && PD::SameState(e.beforeB,e.afterB),SummaryState);
                    if (e.reason==2101) { ++t.graphs; t.flags=unsigned(e.featureA); t.graphComponents=unsigned(e.accumulatedAfter[0]); t.eligibleComponents=unsigned(e.accumulatedAfter[1]); t.eligiblePoints=unsigned(e.accumulatedAfter[2]); }
                    if (e.reason==2102) { ++t.works; t.visits=unsigned(e.accumulatedBefore[0]); }
                    if (e.reason==2103) { ++t.finals; t.finiteFailure=unsigned(e.accumulatedAfter[2]); }
                    t.CaptureFailure(e,failed,0,0,0,0,0); return;
                }
                const auto op=e.operation;
                t.warm+=op==PD::Operation::WarmStart; t.normal+=op==PD::Operation::NormalSliding;
                t.spin+=op==PD::Operation::Spin; t.rolling+=op==PD::Operation::Rolling; t.blocks+=op==PD::Operation::BlockNormal;
                t.reverse+=e.reason==2111; t.forward+=e.reason==2112;
                if (op==PD::Operation::Position) {
                    ++t.positions;
                    const double ia=e.a->GetInverseMass(),ib=e.b->GetInverseMass(),sum=ia+ib;
                    const float correction=std::min(.2f,.25f*(float(e.penetration)-.02f));
                    const auto pa=e.beforeA.position-e.normal*(correction*float(ia/sum));
                    const auto pb=e.beforeB.position+e.normal*(correction*float(ib/sum));
                    check(e.afterA.position==(ia>0?pa:e.beforeA.position) && e.afterB.position==(ib>0?pb:e.beforeB.position),PositionTargets);
                    check(e.beforeA.linear==e.afterA.linear && e.beforeA.angular==e.afterA.angular &&
                        e.beforeB.linear==e.afterB.linear && e.beforeB.angular==e.afterB.angular &&
                        glm::length(glm::dvec3(e.afterA.position)-glm::dvec3(e.beforeA.position))<=.200001 &&
                        glm::length(glm::dvec3(e.afterB.position)-glm::dvec3(e.beforeB.position))<=.200001,PositionVelocityOrCap);
                }
                if (!e.appliedImpulsesKnown) { t.CaptureFailure(e,failed,0,0,0,0,0); return; }
                double expected=0,measured=0,kinetic=0,linearError=0,angularError=0;
                for (unsigned role=0;role<2;++role) {
                    const auto& body=*(role?e.b:e.a); if (!(body.GetInverseMass()>0)) continue;
                    const auto& before=role?e.beforeB:e.beforeA; const auto& after=role?e.afterB:e.afterA;
                    const glm::dvec3 j(role?e.linearImpulseB:e.linearImpulseA),l(role?e.angularImpulseB:e.angularImpulseA);
                    const auto inverse=glm::dmat3(body.GetInverseInertiaTensorWorldSpace());
                    const auto dv=double(body.GetInverseMass())*j,dw=inverse*l;
                    expected+=glm::dot(glm::dvec3(before.linear),j)+glm::dot(glm::dvec3(before.angular),l)+.5*(glm::dot(j,dv)+glm::dot(l,dw));
                    const double kb=Energy(body,before); kinetic+=kb; measured+=Energy(body,after)-kb;
                    check(before.position==after.position && before.orientation==after.orientation,ImpulsePose);
                    for (unsigned axis=0;axis<3;++axis) {
                        const double le=std::abs(double(after.linear[axis])-before.linear[axis]-dv[axis]);
                        const double ae=std::abs(double(after.angular[axis])-before.angular[axis]-dw[axis]);
                        check(le<=1e-5*std::max(1.,std::abs(dv[axis])),LinearEquation);
                        check(ae<=1e-5*std::max(1.,std::abs(dw[axis])),AngularEquation);
                        linearError=std::isfinite(le)?std::max(linearError,le):le;
                        angularError=std::isfinite(ae)?std::max(angularError,ae):ae;
                    }
                }
                check(std::isfinite(expected) && std::isfinite(measured),NonfiniteWork);
                check(std::abs(expected-measured)<=1e-5*std::max(1.,kinetic),WorkBalance);
                if (op==PD::Operation::NormalSliding || op==PD::Operation::WarmStart || op==PD::Operation::BlockNormal) {
                    check(e.accumulatedAfter[0]>=-1e-6,NormalBound);
                    if (op!=PD::Operation::BlockNormal) check(std::hypot(e.accumulatedAfter[1],e.accumulatedAfter[2])<=
                        double(e.a->m_Friction)*e.b->m_Friction*e.accumulatedAfter[0]+1e-5*std::max(1.,std::abs(e.accumulatedAfter[0])),CoulombBound);
                }
                if (op==PD::Operation::Spin || op==PD::Operation::Rolling)
                    check(t.angular && t.angular->Check(e,expected,kinetic),ResistanceWork);
                if (e.reason==2111 || e.reason==2112) { t.expected+=expected; t.measured+=measured; }
                t.CaptureFailure(e,failed,expected,measured,kinetic,linearError,angularError);
            }
            static void Number(std::ostream& out,double v) {
                if (std::isfinite(v)) out<<v;
                else out<<std::quoted(std::isnan(v)?"NaN":(v>0?"Infinity":"-Infinity"));
            }
            static void Vector(std::ostream& out,const Vec3f& v) {
                out<<'['; Number(out,v.x); out<<','; Number(out,v.y); out<<','; Number(out,v.z); out<<']';
            }
            static void State(std::ostream& out,const PD::BodyState& s) {
                out<<"{\"position\":"; Vector(out,s.position); out<<",\"linear\":"; Vector(out,s.linear);
                out<<",\"angular\":"; Vector(out,s.angular); out<<",\"orientation_wxyz\":[";
                Number(out,s.orientation.w); out<<','; Number(out,s.orientation.x); out<<',';
                Number(out,s.orientation.y); out<<','; Number(out,s.orientation.z);
                out<<"],\"inactive\":"; Number(out,s.inactive); out<<",\"sleeping\":"<<(s.sleeping?"true":"false")<<'}';
            }
            void WriteFailure(std::ostream& out) const {
                out<<",\"failure_mask\":"<<failureMask<<",\"first_failure\":";
                if (!first || !first->tick) { out<<"null"; return; }
                const auto& s=*first; const auto& e=s.event;
                out<<"{\"mask\":"<<firstMask<<",\"event_sequence\":"<<s.tick
                    <<",\"mask_bits\":{\"missing_bodies\":1,\"summary_state\":2,\"position_targets\":4,\"position_velocity_or_cap\":8,\"impulse_pose\":16,\"linear_equation\":32,\"angular_equation\":64,\"nonfinite_work\":128,\"work_balance\":256,\"normal_bound\":512,\"coulomb_bound\":1024,\"resistance_work\":2048}"
                    <<",\"operation\":"<<unsigned(e.operation)<<",\"reason\":"<<e.reason<<",\"contact_slot\":"<<e.slot
                    <<",\"constraint_token\":"<<e.constraintToken<<",\"identity_type\":\"RigidBodyIdentity(slot,generation)\",\"body_a\":["
                    <<s.identity[0]<<','<<s.identity[1]<<"],\"body_b\":["<<s.identity[2]<<','<<s.identity[3]<<']';
                out<<",\"before_a\":"; State(out,e.beforeA); out<<",\"after_a\":"; State(out,e.afterA);
                out<<",\"before_b\":"; State(out,e.beforeB); out<<",\"after_b\":"; State(out,e.afterB);
                out<<",\"normal\":"; Vector(out,e.normal); out<<",\"tangent_u\":"; Vector(out,e.tangentU); out<<",\"tangent_v\":"; Vector(out,e.tangentV);
                out<<",\"anchor_a\":"; Vector(out,e.anchorA); out<<",\"anchor_b\":"; Vector(out,e.anchorB);
                out<<",\"local_a\":"; Vector(out,e.localA); out<<",\"local_b\":"; Vector(out,e.localB);
                out<<",\"linear_impulse_a\":"; Vector(out,e.linearImpulseA); out<<",\"linear_impulse_b\":"; Vector(out,e.linearImpulseB);
                out<<",\"angular_impulse_a\":"; Vector(out,e.angularImpulseA); out<<",\"angular_impulse_b\":"; Vector(out,e.angularImpulseB);
                const auto values=[&](const char* name,const auto& row) {
                    out<<",\""<<name<<"\":["; bool comma=false;
                    for (double v:row) { if (comma) out<<','; Number(out,v); comma=true; } out<<']';
                };
                values("accumulated_before",e.accumulatedBefore); values("accumulated_after",e.accumulatedAfter);
                values("normal_row",e.normalRow); values("tangent_row_u",e.tangentRowU); values("tangent_row_v",e.tangentRowV);
                const auto number=[&](const char* name,double v) { out<<",\""<<name<<"\":"; Number(out,v); };
                number("expected_work",s.expected); number("measured_work",s.measured); number("pair_kinetic_before",s.kinetic);
                number("work_tolerance",1e-5*std::max(1.,s.kinetic)); number("linear_max_abs_error",failedLinearError); number("angular_max_abs_error",failedAngularError);
                number("inverse_mass_a",failedInverseMassA); number("inverse_mass_b",failedInverseMassB); number("prepared_duration",failedDuration);
                number("combined_friction",failedFriction); number("combined_spin_length",failedSpinLength); number("combined_rolling_length",failedRollingLength);
                out<<",\"normal_load_known\":"<<(failedNormalKnown?"true":"false")<<",\"normal_load\":";
                if (failedNormalKnown) Number(out,failedNormalLoad); else out<<"null";
                out<<",\"resistance_limit\":";
                if (failedNormalKnown && (e.operation==PD::Operation::Spin || e.operation==PD::Operation::Rolling))
                    Number(out,(e.operation==PD::Operation::Spin?failedSpinLength:failedRollingLength)*std::max(0.,failedNormalLoad));
                else out<<"null";
                out<<",\"applied_impulses_known\":"<<(e.appliedImpulsesKnown?"true":"false")<<'}';
            }
        };
        static_assert(sizeof(RefinementDirectTrace)<=256,"bounded direct diagnostic scalar metadata");
        struct RefinementDirectFixture {
            ShapeSphere shape;
            PhysicsWorld world;
            ManifoldCollector collector;
            std::vector<RigidBody3D*> body;
            explicit RefinementDirectFixture(unsigned count) {
                body.reserve(count+1);
                for (unsigned i=0;i<=count;++i) {
                    auto* b=world.CreateRigidBody3D(); b->m_Shape=&shape;
                    b->SetBodyTypeAndInverseMass(i?Component::BodyType::Dynamic:Component::BodyType::Static,i?1.f:0.f);
                    b->m_Position={0,float(i)*2,0}; b->m_Friction=.5f; b->m_Elasticity=.5f;
                    if (i) b->m_LinearVelocity={.125f,-1,0};
                    b->SetSpinResistanceLength(.05f); b->SetRollingResistanceLength(.05f); body.push_back(b);
                }
            }
            void Add(unsigned a,unsigned b,unsigned points=1) {
                std::array<contact_t,4> contacts{};
                const auto axis=Math::NormalizeOr(body[a]->m_Position-body[b]->m_Position);
                Vec3f u,v; Math::GetOrtho(axis,u,v);
                for (unsigned i=0;i<points;++i) {
                    auto& c=contacts[i]; c.m_BodyA=body[a]; c.m_BodyB=body[b]; c.normal=axis;
                    auto center=(body[a]->m_Position+body[b]->m_Position)*.5f;
                    if (points>1) center+=u*(i&1?.3f:-.3f)+v*(i&2?.3f:-.3f);
                    c.ptOnA_WorldSpace=center-axis*.02f; c.ptOnB_WorldSpace=center+axis*.02f;
                    c.ptOnA_LocalSpace=body[a]->WorldSpaceToBodySpace(c.ptOnA_WorldSpace);
                    c.ptOnB_LocalSpace=body[b]->WorldSpaceToBodySpace(c.ptOnB_WorldSpace);
                    c.separationDistance=-.04f; c.timeOfImpact=0; c.featureA=c.featureB=i+1;
                }
                collector.AddContacts(contacts.data(),int(points));
            }
            bool Finite() const { for (const auto* b:body) if (!b->HasFiniteState() || !world.IsBodyIdentityValid(b->GetIdentity())) return false; return true; }
        };
        bool RunRefinementDirect()
        {
            auto& r=phase21->refinement;
            auto& firstFailure=phase21->audit.energySample[0];
            struct Restore {
                PD::Observer* prior; Phase21Capture::Attribution::EnergyExample* sample;
                ~Restore() { sample->tick=0; sample->event.a=sample->event.b=nullptr; PD::observer=prior; }
            } restore{PD::observer,&firstFailure};
            RefinementDirectTrace trace;
            PD::Observer observer{}; observer.context=&trace; observer.detailed=observer.targeted=true; observer.event=&RefinementDirectTrace::Observe;
            auto attach=[&] { firstFailure.tick=0; firstFailure.event.a=firstFailure.event.b=nullptr;
                trace={}; trace.first=&firstFailure; r.current.angular={}; trace.angular=&r.current.angular; PD::observer=&observer; };
            auto record=[&](const char* name,bool pass) {
                ++r.directChecks; if (!pass) { ++r.directFailures; Fail("Phase21 direct refinement invariant FAIL"); }
                r.direct<<std::setprecision(std::numeric_limits<double>::max_digits10)
                    <<"{\"schema\":1,\"name\":"<<std::quoted(name)<<",\"pass\":"<<(pass?"true":"false")
                    <<",\"graphs\":"<<trace.graphs<<",\"eligible_components\":"<<trace.eligibleComponents<<",\"eligible_points\":"<<trace.eligiblePoints
                    <<",\"visits\":"<<trace.visits<<",\"flags\":"<<trace.flags<<",\"finite_failure\":"<<trace.finiteFailure
                    <<",\"warm\":"<<trace.warm<<",\"reverse_events\":"<<trace.reverse<<",\"forward_events\":"<<trace.forward
                    <<",\"expected_extra_work\":"<<trace.expected<<",\"measured_extra_work\":"<<trace.measured;
                r.direct<<",\"spin_operations\":"<<trace.spin<<",\"rolling_operations\":"<<trace.rolling;
                r.current.angular.Json(r.direct); trace.WriteFailure(r.direct); r.direct<<"}\n";
            };
            {
                RefinementDirectFixture f(4); f.body[4]->m_Position={2,2,0};
                f.Add(0,1); f.Add(1,2); f.Add(2,3); f.Add(1,4);
                attach(); f.collector.PreSolve(1.f/60); const auto warm=trace.warm; f.collector.Solve();
                record("full-component-lateral",trace.valid && f.Finite() && trace.graphs==1 && trace.graphComponents==1 && trace.eligibleComponents==1 && trace.eligiblePoints==4 && trace.visits==8 && trace.reverse && trace.forward);
                record("no-repeated-warmstart",trace.warm==warm && warm==4);
                const auto reverse=trace.reverse,forward=trace.forward,graphs=trace.graphs;
                f.collector.Solve();
                record("LP-once-per-step",trace.valid && trace.graphs==graphs && trace.reverse==reverse && trace.forward==forward && trace.warm==warm);
                f.collector.PostSolve();
                record("PostSolve-single-translation",trace.valid && trace.positions==4 && f.Finite());
                const auto identity=f.body[4]->GetIdentity(); f.collector.Clear(); f.world.RemoveRigidBody3D(f.body[4]); f.body.pop_back();
                record("identity-lifecycle",!f.world.IsBodyIdentityValid(identity) && f.collector.GetContactCount()==0 && f.Finite());
            }
            {
                RefinementDirectFixture f(4); f.Add(0,1); f.Add(1,2); f.Add(0,3); f.Add(3,4);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("shared-static-separates-components",trace.valid && trace.graphComponents==2 && trace.eligibleComponents==2 && trace.eligiblePoints==4 && trace.visits==8);
            }
            {
                RefinementDirectFixture f(3); f.Add(1,2); f.Add(2,3);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("unsupported-component",trace.valid && trace.graphs==1 && trace.eligibleComponents==0 && trace.visits==0 && !trace.reverse && !trace.forward);
            }
            {
                RefinementDirectFixture f(2); f.body[1]->m_LinearVelocity=f.body[2]->m_LinearVelocity={}; f.Add(0,1); f.Add(1,2);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("unloaded-static-boundary",trace.valid && trace.graphs==1 && !trace.eligibleComponents && !trace.visits);
            }
            {
                RefinementDirectFixture a(2),b(2); a.Add(0,1); a.Add(1,2); b.Add(0,1); b.Add(1,2);
                attach(); a.collector.PreSolve(1.f/60); a.collector.Solve();
                const bool firstValid=trace.valid; attach(); b.collector.PreSolve(1.f/60); b.collector.Solve();
                bool same=firstValid && trace.valid;
                for (unsigned i=0;i<3;++i) same=same && PD::SameState(PD::Capture(*a.body[i]),PD::Capture(*b.body[i]));
                record("first-occurrence-determinism",same && trace.visits==4);
            }
            {
                RefinementDirectFixture f(2); f.body[0]->SetBodyTypeAndInverseMass(Component::BodyType::Kinematic,0); f.Add(0,1); f.Add(1,2);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("kinematic-boundary",trace.valid && trace.eligibleComponents==0 && trace.visits==0 && f.Finite());
            }
            {
                RefinementDirectFixture f(2); f.body[2]->m_Position={2,2,0}; f.body[1]->m_LinearVelocity={0,-1,0}; f.body[1]->m_Friction=f.body[2]->m_Friction=0; f.body[2]->m_LinearVelocity={}; f.body[2]->UpdateSleepTimer(.5); const bool slept=f.body[2]->TrySleep(); f.Add(0,1); f.Add(1,2);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("mixed-sleeping-component",slept && trace.valid && trace.eligibleComponents==0 && trace.visits==0 && f.body[2]->IsSleeping());
                for (unsigned i=1;i<=2;++i) { f.body[i]->m_LinearVelocity={}; f.body[i]->m_AngularVelocity={}; f.body[i]->UpdateSleepTimer(.5); f.body[i]->TrySleep(); }
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("sleeping-preparation-reset",f.body[1]->IsSleeping() && f.body[2]->IsSleeping() && trace.graphs==0 && !trace.reverse && !trace.forward);
            }
            {
                RefinementDirectFixture f(2); f.Add(0,1); f.Add(1,2);
                attach(); f.collector.PreSolve(0); f.collector.Solve();
                record("zero-step-no-opportunity",trace.valid && trace.graphs==0 && !trace.reverse && !trace.forward);
            }
            {
                RefinementDirectFixture f(180); unsigned points=0;
                for (unsigned i=0;i<180;++i,++points) f.Add(i,i+1);
                for (unsigned a=0;a<181 && points<480;++a) for (unsigned b=a+2;b<181 && points<480;++b,++points) f.Add(a,b);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("exact-180-body-480-contact-capacity",points==480 && trace.valid && !trace.flags && trace.eligibleComponents==1 && trace.eligiblePoints==480 && trace.visits==960 && f.Finite());
            }
            {
                RefinementDirectFixture f(181); for (unsigned i=0;i<181;++i) f.Add(i,i+1);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("whole-collector-body-overflow",trace.valid && (trace.flags&1) && !trace.visits && !trace.reverse && !trace.forward && f.Finite());
            }
            {
                RefinementDirectFixture f(32); unsigned pairs=0;
                for (unsigned a=0;a<33 && pairs<481;++a) for (unsigned b=a+1;b<33 && pairs<481;++b,++pairs) f.Add(a,b);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("whole-collector-manifold-overflow",pairs==481 && trace.valid && (trace.flags&1) && !trace.visits && f.Finite());
            }
            {
                RefinementDirectFixture f(16); unsigned pairs=0;
                for (unsigned a=0;a<17 && pairs<121;++a) for (unsigned b=a+1;b<17 && pairs<121;++b,++pairs) f.Add(a,b,4);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("whole-collector-point-overflow",f.collector.GetContactCount()==484 && trace.valid && (trace.flags&1) && !trace.visits && f.Finite());
            }
            {
                RefinementDirectFixture f(2); for (unsigned i=1;i<=2;++i) f.body[i]->m_AngularVelocity={.2f,1,.3f}; f.Add(0,1,4); f.Add(1,2,4);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("coherent-block-normal-friction-resistance",trace.valid && trace.blocks>0 && trace.normal>0 && trace.spin>0 && trace.rolling>0 && trace.visits==16 && f.Finite());
                record("independent-extra-impulse-energy",trace.reverse && trace.forward && trace.valid && std::abs(trace.expected-trace.measured)<=1e-4);
            }
            {
                RefinementDirectFixture f(2); f.body[1]->m_Elasticity=1; f.body[2]->m_Elasticity=.25f; f.Add(0,1); f.Add(1,2);
                attach(); f.collector.PreSolve(1.f/60); f.collector.Solve();
                record("material-restitution-preserved",trace.valid && f.body[0]->m_Elasticity==.5f && f.body[1]->m_Elasticity==1 && f.body[2]->m_Elasticity==.25f && f.body[1]->m_Friction==.5f && f.Finite());
            }
            {
                RefinementDirectFixture f(2); f.Add(0,1); f.Add(1,2);
                attach(); f.collector.PreSolve(1.f/60);
                // Public operand fault injection between preparation and solving:
                // ordinary connector rejects its nonfinite effective mass; the
                // loaded finite boundary remains. The extra pass must report its
                // technical failure before applying an invalid impulse.
                f.body[2]->m_InvMass=std::numeric_limits<float>::max();
                f.collector.Solve();
                record("finite-arithmetic-abort",trace.finiteFailure==1 && trace.graphs==1 && trace.eligibleComponents==1 && trace.forward==0 && f.Finite());
            }
            {
                RefinementDirectFixture f(0); auto* a=f.body[0]; auto* b=f.world.CreateRigidBody3D(); b->m_Shape=&f.shape;
                ConstraintPenetration c; c.m_bodyA=a; c.m_bodyB=b; c.m_anchorA=c.m_anchorB={}; c.m_Normal={0,1,0};
                attach(); c.PreSolve(1.f/60); const auto beforeA=PD::Capture(*a),beforeB=PD::Capture(*b); c.Solve();
                record("zero-effective-denominator",PD::SameState(beforeA,PD::Capture(*a)) && PD::SameState(beforeB,PD::Capture(*b)) && Math::IsFinite(c.m_CachedLambda[0]));
            }
            PD::observer=nullptr; r.direct.flush();
            if (!r.direct) Fail("Phase21 direct evidence flush"); return !failed;
        }

        static bool RefinementSummary(const PD::Event& e) noexcept
        { return e.operation==PD::Operation::ContactSnapshot && e.reason>=2101 && e.reason<=2103; }
        void Phase21RefinementSummary(const PD::Event& e)
        {
            auto& r=phase21->refinement.current; const auto bit=1ull<<unsigned(e.reason-2101);
            if ((r.mask&bit) || !PD::SameState(e.beforeA,e.afterA) || !PD::SameState(e.beforeB,e.afterB)) { Fail("Phase21 refinement summary integrity"); return; }
            r.mask|=bit;
            if (e.reason==2101) {
                if (!std::isfinite(e.time) || e.time<0 || e.time>=std::ldexp(1.,64) || std::floor(e.time)!=e.time) {
                    Fail("Phase21 graph planning duration integrity"); return;
                }
                r.graphNs=static_cast<std::uint64_t>(e.time);
                r.bodiesPointsManifolds=e.accumulatedBefore; r.components=e.accumulatedAfter;
                r.flags=e.featureA; r.scratch=e.featureB; r.capacity=std::uint64_t(e.duration); r.constraint=std::uint64_t(e.bias);
                std::copy_n(e.normalRow.begin(),8,r.graph.begin());
            } else if (e.reason==2102) {
                r.attempts=e.accumulatedBefore; r.inner=e.accumulatedAfter; r.rollingBisections=e.featureA; r.blocks=e.featureB;
                std::copy_n(e.normalRow.begin(),5,r.rejected.begin());
            } else { r.blockMasks=e.accumulatedBefore[0]; r.preflights=e.accumulatedBefore[1]; r.final=e.accumulatedAfter; }
        }
        void Phase21RefinementEnd(const Phase21Capture::Row& row,ManifoldCollector& manifolds)
        {
            auto& p=*phase21; auto& r=p.refinement.current;
            // Prefix deliberately retains minimal observation; C4 checks the same
            // physical prefix while carrying the complete work and energy audit.
            if (p.observed) {
                bool activeContact=false;
                for (auto& manifold:manifolds.m_Manifolds) if (manifold.GetNumContacts()>0) {
                    const auto c=manifold.GetContact(0);
                    activeContact=activeContact || (c.m_BodyA->GetInverseMass()>0 && !c.m_BodyA->IsSleeping()) ||
                        (c.m_BodyB->GetInverseMass()>0 && !c.m_BodyB->IsSleeping());
                }
                // The graph was dispatched before sleep decisions: End may have
                // put its last awake member to sleep, so a present triple remains
                // valid even when this conservative End check has no active row.
                if ((activeContact && r.mask!=7) || (r.mask!=0 && r.mask!=7) || (r.flags&1) || r.final[2]!=0) Fail("Phase21 refinement capacity/finite/evidence gate");
                if (r.mask) {
                    ++p.refinement.summaryTicks; p.refinement.eligibleTicks+=r.components[1]>0;
                    if (r.scratch>3072 || r.capacity!=manifolds.m_Manifolds.capacity() || r.constraint!=sizeof(ConstraintPenetration) ||
                        r.bodiesPointsManifolds[0]>180 || r.bodiesPointsManifolds[1]>480 || r.bodiesPointsManifolds[2]>480 ||
                        r.attempts[0]>960 || r.attempts[1]>2880 || r.attempts[2]>2880 || r.inner[0]>92160 ||
                        r.inner[1]>960 || r.inner[2]>960 || r.rollingBisections>46080 || r.blocks>480 || r.blockMasks>3840 ||
                        r.graph[5]>172800 || r.graph[6]>171840 || r.graph[7]>32220 || r.attempts[0]!=2*r.components[2])
                        Fail("Phase21 refinement work/storage bound");
                }
                p.refinement.extraEvents+=r.operations[1]+r.operations[2];
                if (p.cases[episode].kind==3) {
                    const auto& b=row.body[1];
                    if (tick>60 && r.upward>.005) Fail("SphereBox-support upward correction FAIL");
                    if (tick==181) { p.refinement.minY=p.refinement.maxY=b.position.y; p.refinement.restEnergy=row.energy; }
                    if (tick>=181) {
                        p.refinement.minY=std::min(p.refinement.minY,double(b.position.y)); p.refinement.maxY=std::max(p.refinement.maxY,double(b.position.y));
                        if (row.penetration>.035 || p.refinement.maxY-p.refinement.minY>.005 || std::abs(b.linear.y)>.01 || row.energy-p.refinement.restEnergy>.001)
                            Fail("SphereBox-support grounded predicate FAIL");
                    }
                    if (tick>TickLimit()-60 && (row.quiet!=1 || row.wakes || row.penetration>.035)) Fail("SphereBox-support terminal quiet predicate FAIL");
                }
                if (p.cases[episode].kind==4) {
                    for (unsigned i=5;i<row.count;++i) if ((row.body[i].sleeping ||
                        (glm::length(glm::dvec3(row.body[i].linear))<=.05 && glm::length(glm::dvec3(row.body[i].angular))<=.02)) &&
                        row.bodyContacts[i] && row.bodyPenetration[i]>.05) Fail("SphereBoxStack-support supported rest depth FAIL");
                }
            }
            auto& out=p.refinement.work;
            out<<episode<<','<<tick<<','<<r.mask<<','<<r.flags<<','<<r.scratch<<','<<r.capacity<<','<<r.constraint;
            for (auto v:r.bodiesPointsManifolds) out<<','<<v; for (auto v:r.components) out<<','<<v; for (auto v:r.graph) out<<','<<v;
            for (auto v:r.attempts) out<<','<<v; for (auto v:r.inner) out<<','<<v;
            out<<','<<r.rollingBisections<<','<<r.blocks<<','<<r.blockMasks<<','<<r.preflights<<','<<r.final[2];
            for (auto v:r.rejected) out<<','<<v; for (auto v:r.operations) out<<','<<v;
            for (auto v:r.expected) out<<','<<v; for (auto v:r.measured) out<<','<<v;
            out<<','<<r.upward<<','<<r.orderedEvents<<','<<r.orderHash<<','<<r.graphNs;
            r.angular.Csv(out); out<<'\n'; if (!out) Fail("Phase21 refinement work write");
        }

        bool CausalCheck(bool ok,const char* why)
        { if (!ok) { ++phase21->causal.errors; Fail(why); } return ok; }
        static bool CausalIncident(unsigned a,unsigned b) { return a==137 || a==173 || b==137 || b==173; }
        bool CausalWindow() const { return phase21->causalMode && tick>=72 && tick<=73; }
        static void CausalVectorHeader(std::ostream& out,const char* name)
        { for (char c:std::string("xyz")) out<<','<<name<<'_'<<c; }
        bool CausalOpen(const std::filesystem::path& dir)
        {
            auto& c=phase21->causal;
            for (const char* name:{"causal-operations.csv","causal-corrections.csv","causal-stages.csv","causal-result.json"})
                if (!CausalCheck(!std::filesystem::exists(dir/name),"causal output already exists")) return false;
            c.operations.open(dir/"causal-operations.csv"); c.corrections.open(dir/"causal-corrections.csv"); c.stages.open(dir/"causal-stages.csv");
            for (auto* out:{&c.operations,&c.corrections,&c.stages}) *out<<std::setprecision(17);
            auto& o=c.operations;
            o<<"tick,sequence,operation,a,b,a_slot,a_generation,b_slot,b_generation,contact_slot,previous_slot,reason,feature_a,feature_b,token,origin_tick,impulses_known,contact_snapshot_known";
            for (const char* n:{"before_a","before_b","after_a","after_b"}) PostStateHeader(o,n);
            for (const char* n:{"normal","anchor_a","anchor_b","local_a","local_b","linear_a","linear_b","angular_a","angular_b","tangent_u","tangent_v","contact_normal"}) CausalVectorHeader(o,n);
            for (const char* n:{"normal_row","tangent_row_u","tangent_row_v"}) for (unsigned i=0;i<12;++i) o<<','<<n<<'_'<<i;
            for (const char* n:{"lambda_before","lambda_after"}) for (unsigned i=0;i<3;++i) o<<','<<n<<'_'<<i;
            o<<",restitution,closing_speed,penetration,time,duration,bias\n";
            auto& p=c.corrections;
            p<<"tick,order,a,b,a_slot,a_generation,b_slot,b_generation,contact_slot,feature_a,feature_b,token,origin_tick,applied,source_reconstructed_reason,depth,tangent_squared,correction";
            for (const char* n:{"normal","anchor_a","anchor_b","local_a","local_b","before_a","before_b","predicted_a","predicted_b","observed_a","observed_b"}) CausalVectorHeader(p,n);
            p<<'\n'; c.stages<<"tick,stage,group,count,min_geometry,max_geometry,max_anchor,max_depth,over_depth,anchor_witnesses";
            PostStateHeader(c.stages,"target_a"); PostStateHeader(c.stages,"target_b"); c.stages<<'\n';
            return CausalCheck(bool(o && p && c.stages),"causal streams unavailable");
        }
        bool CausalContact(const PD::Event& e,Phase21Capture::CausalCapture::Contact& contact)
        {
            auto& c=phase21->causal;
            if (e.slot<0 || e.operation==PD::Operation::ContactRemove) return false;
            const unsigned a=Index(e.a),b=Index(e.b);
            for (unsigned i=0;i<c.contactUsed;++i) {
                const auto& v=c.contacts[i];
                if (v.a!=a || v.b!=b) continue;
                if (e.operation==PD::Operation::WarmStart && v.localA==e.localA && v.localB==e.localB) { contact=v; return true; }
                for (unsigned k=0;k<c.warmUsed;++k) { const auto& w=c.warm[k];
                    if (w.a==a && w.b==b && w.slot==unsigned(e.slot) && w.localA==v.localA && w.localB==v.localB) { contact=v; return true; }
                }
            }
            return false;
        }
        void CausalBudget()
        {
            auto& p=*phase21; auto& c=p.causal; std::uint64_t bytes=0,packets=0,total=p.pairBytes+p.packetBytes+p.caseMetadataBytes;
            for (auto* out:{&c.operations,&c.corrections,&c.stages}) { const auto n=out->tellp(); CausalCheck(n>=0,"causal stream position"); if(n>=0) bytes+=std::uint64_t(n); }
            for (auto* out:{&p.audit.packets,&p.audit.energyPackets}) { const auto n=out->tellp(); CausalCheck(n>=0,"causal packet position"); if(n>=0) packets+=std::uint64_t(n); }
            for (auto* out:{&bodies,&metrics,&frames,&initial,&events,&timing,&p.audit.ledger}) { const auto n=out->tellp(); CausalCheck(n>=0,"causal regular stream position"); if(n>=0) total+=std::uint64_t(n); }
            CausalCheck(bytes<=2*1024*1024 && p.pairBytes+p.packetBytes+packets+bytes+4096<=8*1024*1024 && total+packets+bytes+4096<=64*1024*1024,"causal combined export capacity");
            p.exportBytes=total+packets+bytes+4096; // reserves result metadata
        }
        void CausalCorrection(unsigned index,const PD::Event* event)
        {
            auto& c=phase21->causal; const auto& entry=c.order[index];
            if (entry.warm<0) return;
            const auto& w=c.warm[entry.warm];
            const auto& all=scene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies();
            RigidBody3D a=*all[w.a],b=*all[w.b];
            a.m_Position=c.shadow[w.a].position; a.m_Orientation=c.shadow[w.a].orientation;
            b.m_Position=c.shadow[w.b].position; b.m_Orientation=c.shadow[w.b].orientation;
            const auto pa=a.BodySpaceToWorldSpace(w.localA),pb=b.BodySpaceToWorldSpace(w.localB);
            Vec3f n=a.GetBodyToWorldRotation()*w.normal;
            const float n2=glm::length2(n); const double inverseA=a.GetInverseMass(),inverseB=b.GetInverseMass(),sum=inverseA+inverseB;
            unsigned reason=0; float depth=0,tangent2=0,correction=0; Vec3f expectedA=a.m_Position,expectedB=b.m_Position;
            // SOURCE_RECONSTRUCTED: exact PostSolve predicates, no Physics invocation.
            // A successful positive-step WarmStart proves the private enable flag.
            if (!(sum>0)) reason=2;
            else if (!Math::IsFinite(pa) || !Math::IsFinite(pb) || !Math::IsFinite(n2) || n2<=Math::NumericalEpsilon*Math::NumericalEpsilon) reason=3;
            else {
                n/=std::sqrt(n2); const Vec3f separation=pb-pa; depth=-glm::dot(separation,n);
                const Vec3f tangent=separation+n*depth; tangent2=glm::length2(tangent);
                if (!Math::IsFinite(depth) || !Math::IsFinite(tangent)) reason=4;
                else if (tangent2>=0.02f*0.02f) reason=5;
                else if (depth<=0.02f) reason=6;
                else {
                    correction=std::min(0.2f,0.25f*(depth-0.02f));
                    expectedA=a.m_Position-n*(correction*static_cast<float>(inverseA/sum));
                    expectedB=b.m_Position+n*(correction*static_cast<float>(inverseB/sum));
                    if (!Math::IsFinite(expectedA) || !Math::IsFinite(expectedB)) reason=7;
                }
            }
            if (event) {
                CausalCheck(reason==0 && PD::SameState(event->beforeA,c.shadow[w.a]) && PD::SameState(event->beforeB,c.shadow[w.b]),"causal applied prestate/guard mismatch");
                CausalCheck(event->normal==n && event->anchorA==pa && event->anchorB==pb && event->localA==w.localA && event->localB==w.localB && event->penetration==double(depth),"causal applied operand mismatch");
                CausalCheck(event->afterA.position==expectedA && event->afterB.position==expectedB,"causal applied translation mismatch");
            } else CausalCheck(reason!=0,"causal unexplained skipped correction");
            if (++c.positions>1024) { CausalCheck(false,"causal correction row capacity"); return; }
            auto& out=c.corrections;
            out<<tick<<','<<index<<','<<w.a<<','<<w.b<<','<<all[w.a]->GetIdentity().GetSlot()<<','<<all[w.a]->GetIdentity().GetGeneration()<<','<<all[w.b]->GetIdentity().GetSlot()<<','<<all[w.b]->GetIdentity().GetGeneration()
               <<','<<w.slot<<','<<w.featureA<<','<<w.featureB<<','<<w.token<<','<<w.born<<','<<bool(event)<<','<<reason<<','<<depth<<','<<tangent2<<','<<correction;
            for (const auto& v:{n,pa,pb,w.localA,w.localB,a.m_Position,b.m_Position,expectedA,expectedB,event?event->afterA.position:a.m_Position,event?event->afterB.position:b.m_Position}) CsvVector(out,v);
            out<<'\n'; CausalCheck(bool(out),"causal correction write"); CausalBudget();
        }
        void CausalEvent(const PD::Event& e)
        {
            if (!CausalWindow()) return;
            auto& c=phase21->causal; ++c.sequence;
            const unsigned a=Index(e.a),b=Index(e.b);
            if (!CausalCheck(a<185 && b<185,"causal operation identity")) return;
            const bool incident=CausalIncident(a,b); Phase21Capture::CausalCapture::Contact contact{}; const bool known=incident && CausalContact(e,contact);
            if (incident && e.operation<=PD::Operation::BlockNormal) CausalCheck(known,"causal response contact metadata missing");
            unsigned born=0;
            if (incident && e.operation==PD::Operation::WarmStart) {
                if (!CausalCheck(known && e.constraintToken && c.warmUsed<c.warm.size() && e.duration==double(float(1.0/60.0)),"causal warm provenance unavailable")) return;
                // Copy public values only during the engine's synchronous callback.
                const auto* source=reinterpret_cast<const ConstraintPenetration*>(e.constraintToken);
                if (!CausalCheck(source->m_bodyA==e.a && source->m_bodyB==e.b && source->m_anchorA==e.localA && source->m_anchorB==e.localB,"causal warm constraint identity")) return;
                auto& audit=phase21->audit;
                const unsigned origin=audit.originIndex[Phase21OriginBucket(a,b,e.localA,e.localB)];
                if (!CausalCheck(origin!=0,"causal warm origin missing")) return;
                born=audit.origins[origin-1].born;
                c.warm[c.warmUsed++]={a,b,unsigned(e.slot),born,contact.featureA,contact.featureB,std::uint64_t(e.constraintToken),e.localA,e.localB,source->m_Normal};
            }
            if (known && !born) {
                const auto origin=phase21->audit.originIndex[Phase21OriginBucket(a,b,contact.localA,contact.localB)];
                if (CausalCheck(origin!=0,"causal contact origin missing")) born=phase21->audit.origins[origin-1].born;
            }
            if (e.operation==PD::Operation::Position) {
                if (!CausalCheck(c.stage==unsigned(PD::Stage::Integration),"causal correction stage mismatch")) return;
                while (c.cursor<c.orderUsed) {
                    const auto& r=c.order[c.cursor];
                    if (r.a==a && r.b==b && r.slot==unsigned(e.slot)) break;
                    CausalCorrection(c.cursor,nullptr); ++c.cursor;
                }
                if (!CausalCheck(c.cursor<c.orderUsed,"causal correction traversal mismatch")) return;
                CausalCorrection(c.cursor,&e); ++c.cursor;
                c.shadow[a]=e.afterA; c.shadow[b]=e.afterB;
            }
            if (!incident) return;
            if (++c.events>1024) { CausalCheck(false,"causal operation row capacity"); return; }
            auto& out=c.operations;
            out<<tick<<','<<c.sequence<<','<<unsigned(e.operation)<<','<<a<<','<<b<<','<<e.a->GetIdentity().GetSlot()<<','<<e.a->GetIdentity().GetGeneration()<<','<<e.b->GetIdentity().GetSlot()<<','<<e.b->GetIdentity().GetGeneration()
               <<','<<e.slot<<','<<e.previousSlot<<','<<e.reason<<','<<(known?contact.featureA:e.featureA)<<','<<(known?contact.featureB:e.featureB)<<','<<std::uint64_t(e.constraintToken)<<','<<born<<','<<e.appliedImpulsesKnown<<','<<known;
            for (const auto& s:{e.beforeA,e.beforeB,e.afterA,e.afterB}) PostStateRow(out,s);
            for (const auto& v:{e.normal,e.anchorA,e.anchorB,e.localA,e.localB,e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB,e.tangentU,e.tangentV,known?contact.normal:Vec3f(0)}) CsvVector(out,v);
            for (const auto& r:{e.normalRow,e.tangentRowU,e.tangentRowV}) for (double v:r) out<<','<<v;
            for (const auto& r:{e.accumulatedBefore,e.accumulatedAfter}) for (double v:r) out<<','<<v;
            out<<','<<e.restitution<<','<<e.closingSpeed<<','<<e.penetration<<','<<e.time<<','<<e.duration<<','<<e.bias<<'\n';
            CausalCheck(bool(out),"causal operation write"); CausalBudget();
        }
        void CausalStage(PD::Stage stage,const PhysicsWorld& world,ManifoldCollector& manifolds)
        {
            if (!CausalWindow()) return;
            auto& c=phase21->causal;
            const auto& all=world.GetPhysicsBodies();
            if (!CausalCheck(all.size()==185 && all[4]->m_Position==inputs[4].position && all[4]->m_Orientation==inputs[4].orientation,"causal fixture geometry identity")) return;
            if (stage==PD::Stage::Begin) { c.contactUsed=c.warmUsed=c.cursor=c.orderUsed=c.sequence=0; }
            if (stage==PD::Stage::Narrowphase) {
                for (auto& m:manifolds.m_Manifolds) for (int slot=0;slot<m.GetNumContacts();++slot) {
                    const auto v=m.GetContact(slot); const unsigned a=Index(v.m_BodyA),b=Index(v.m_BodyB);
                    if (!CausalIncident(a,b)) continue;
                    if (!CausalCheck(c.contactUsed<c.contacts.size(),"causal contact value capacity")) return;
                    c.contacts[c.contactUsed++]={a,b,unsigned(slot),v.featureA,v.featureB,v.ptOnA_LocalSpace,v.ptOnB_LocalSpace,v.normal};
                }
            }
            if (stage==PD::Stage::Correction) {
                while (c.cursor<c.orderUsed) { CausalCorrection(c.cursor,nullptr); ++c.cursor; }
                for (unsigned i=0;i<185;++i) CausalCheck(PD::SameState(c.shadow[i],PD::Capture(*all[i])),"causal PostSolve shadow parity");
                ++c.completedWindows;
            }
            c.stage=unsigned(stage); c.stageMasks[tick-72]|=1u<<c.stage;
            if (stage==PD::Stage::Integration) {
                for (unsigned i=0;i<185;++i) c.shadow[i]=PD::Capture(*all[i]);
                for (auto& m:manifolds.m_Manifolds) for (int slot=0;slot<m.GetNumContacts();++slot) {
                    if (!CausalCheck(c.orderUsed<c.order.size(),"causal ordered contact capacity")) return;
                    const auto contact=m.GetContact(slot); const unsigned a=Index(contact.m_BodyA),b=Index(contact.m_BodyB); int found=-1;
                    if (CausalIncident(a,b)) {
                        for (unsigned k=0;k<c.warmUsed;++k) { const auto& w=c.warm[k];
                            if (w.a==a && w.b==b && w.slot==unsigned(slot) && w.localA==contact.ptOnA_LocalSpace && w.localB==contact.ptOnB_LocalSpace && w.featureA==contact.featureA && w.featureB==contact.featureB) { CausalCheck(found<0,"causal ambiguous contact provenance"); found=int(k); }
                        }
                        CausalCheck(found>=0,"causal missing positive-step warmed contact");
                    }
                    c.order[c.orderUsed++]={a,b,unsigned(slot),found};
                }
            }
            std::array<double,36> layerAnchor{},floorAnchor{}; std::array<bool,36> layerHas{},floorHas{};
            for (auto& m:manifolds.m_Manifolds) for (int j=0;j<m.GetNumContacts();++j) {
                const auto v=m.GetContact(j); unsigned a=Index(v.m_BodyA),b=Index(v.m_BodyB); if (a>b) std::swap(a,b);
                const bool layer=a>=113 && a<=148 && b==a+36,floor=a==4 && b>=149 && b<=184;
                if (!layer && !floor) continue;
                const RigidBody3D ca=*v.m_BodyA,cb=*v.m_BodyB;
                const double depth=std::max(0.,-double(glm::dot(ca.BodySpaceToWorldSpace(v.ptOnA_LocalSpace)-cb.BodySpaceToWorldSpace(v.ptOnB_LocalSpace),v.normal)));
                const unsigned k=layer?a-113:b-149;
                if (layer) { layerAnchor[k]=std::max(layerAnchor[k],depth); layerHas[k]=true; }
                else { floorAnchor[k]=std::max(floorAnchor[k],depth); floorHas[k]=true; }
            }
            for (unsigned group=0;group<3;++group) {
                double lo=1e9,hi=0,anchor=0,peak=0; unsigned violations=0,witnesses=0,count=group?36u:1u;
                for (unsigned n=0;n<count;++n) {
                    const unsigned k=group?n:24u; //137-113
                    const double g=group==2?std::max(0.,1.5-double(all[k+149]->m_Position.y)):std::max(0.,2.-glm::length(glm::dvec3(all[k+113]->m_Position)-glm::dvec3(all[k+149]->m_Position)));
                    const double a=group==2?floorAnchor[k]:layerAnchor[k],depth=std::max(g,a);
                    lo=std::min(lo,g); hi=std::max(hi,g); anchor=std::max(anchor,a); peak=std::max(peak,depth);
                    violations+=depth>(group==2?.1:.2); witnesses+=group==2?floorHas[k]:layerHas[k];
                }
                c.stages<<tick<<','<<unsigned(stage)<<','<<group<<','<<count<<','<<lo<<','<<hi<<','<<anchor<<','<<peak<<','<<violations<<','<<witnesses;
                PostStateRow(c.stages,group?PD::BodyState{}:PD::Capture(*all[137])); PostStateRow(c.stages,group?PD::BodyState{}:PD::Capture(*all[173])); c.stages<<'\n';
            }
            CausalCheck(bool(c.stages),"causal stage summary write"); CausalBudget();
        }
        void CausalClose()
        {
            auto& p=*phase21; auto& c=p.causal;
            Phase21AuditPackets();
            CausalBudget();
            std::uint64_t bytes=0;
            for (auto* out:{&c.operations,&c.corrections,&c.stages}) { const auto n=out->tellp(); CausalCheck(n>=0,"causal stream position"); if(n>=0) bytes+=std::uint64_t(n); }
            for (auto* out:{&c.operations,&c.corrections,&c.stages,&bodies,&metrics,&frames,&initial,&events,&timing,&p.audit.ledger,&p.audit.packets,&p.audit.energyPackets}) { out->flush(); CausalCheck(bool(*out),"causal final durable flush"); }
            const bool expected=failed && failure=="Phase21 dynamic penetration depth FAIL";
            const bool complete=expected && tick==73 && c.stageMasks[0]==4095 && c.stageMasks[1]==4095 && c.completedWindows==2 && c.errors==0;
            std::ofstream out(std::filesystem::path(config.outputDirectory)/"causal-result.json");
            out<<"{\"diagnostic_only\":true,\"capture_complete\":"<<(complete?"true":"false")<<",\"original_P1_depth_FAIL\":"<<(expected?"true":"false")
               <<",\"last_tick\":"<<tick<<",\"completed_windows\":"<<c.completedWindows<<",\"errors\":"<<c.errors<<",\"operation_rows\":"<<c.events<<",\"correction_rows\":"<<c.positions
               <<",\"causal_bytes\":"<<bytes<<",\"causal_storage_bytes\":"<<sizeof(Phase21Capture::CausalCapture)<<",\"export_bytes_upper_bound\":"<<p.exportBytes
               <<",\"profiling_enabled\":"<<(IsPhysicsProfilingEnabled()?"true":"false")<<",\"application_shutdown_observed\":false,\"skip_reason_source\":\"SOURCE_RECONSTRUCTED_WITH_EXACT_APPLIED_AND_SHADOW_PARITY\"}\n";
            out.flush(); CausalCheck(bool(out) && out.tellp()<=4096,"causal result write/capacity");
        }
        // Explicit fixed-size binary records: 192-byte trace and 64-byte event.
        bool Phase21PairReserve(std::uint64_t bytes)
        {
            auto& p=*phase21;
            if (p.pairBytes+p.packetBytes+bytes>8*1024*1024) { overflow=true; Fail("Phase21 pair export capacity INCONCLUSIVE"); return false; }
            p.pairBytes+=bytes; return true;
        }
        unsigned Phase21PairTouch(unsigned a,unsigned b)
        {
            if (a>b) std::swap(a,b);
            if (a==b || b>=185 || b<5) { Fail("Phase21 unsupported pair identity"); return 0; }
            auto& p=phase21->pairs; const unsigned key=b*(b-1)/2+a;
            auto& v=p.state[key];
            if (v.seen!=tick) {
                if (p.workCount==p.work.size()) { Fail("Phase21 pair work capacity INCONCLUSIVE"); return key; }
                v.seen=tick; v.anchor=0; v.hasAnchor=false; v.a=std::uint16_t(a); v.b=std::uint16_t(b);
                p.work[p.workCount++]=std::uint16_t(key);
            }
            return key;
        }
        double Phase21Geometry(unsigned a,unsigned b,const Phase21Capture::Row& row,double& dimension)
        {
            const auto& ga=inputs[a]; const auto& gb=inputs[b];
            const auto& sa=row.body[a]; const auto& sb=row.body[b];
            if (ga.shape==0 && gb.shape==0) {
                dimension=2*std::min(double(ga.radius),double(gb.radius));
                return std::max(0.,double(ga.radius)+gb.radius-glm::length(glm::dvec3(sa.position)-glm::dvec3(sb.position)));
            }
            if (ga.shape!=1 || ga.inverseMass!=0 || gb.shape!=0 || ga.points.empty()) { Fail("Phase21 unsupported geometric pair INCONCLUSIVE"); return 0; }
            glm::dvec3 low(ga.points[0]),high(low);
            for (auto v:ga.points) { low=glm::min(low,glm::dvec3(v)); high=glm::max(high,glm::dvec3(v)); }
            const auto extent=(high-low)*.5; dimension=std::min(2*double(gb.radius),2*std::min({extent.x,extent.y,extent.z}));
            const glm::dquat q(sa.orientation); const auto rotation=glm::mat3_cast(q);
            if (std::abs(glm::dot(q,q)-1)>1e-5 || dimension<=0) { Fail("Phase21 ambiguous box geometry INCONCLUSIVE"); return 0; }
            std::array<glm::dvec3,3> axis{}; glm::dvec3 half;
            for (unsigned j=0;j<3;++j) { const double scale=glm::length(rotation[j]); axis[j]=rotation[j]/scale; half[j]=extent[j]*scale; }
            if (std::abs(glm::dot(axis[0],axis[1]))>1e-12 || std::abs(glm::dot(axis[0],axis[2]))>1e-12 || std::abs(glm::dot(axis[1],axis[2]))>1e-12) { Fail("Phase21 nonorthogonal box INCONCLUSIVE"); return 0; }
            const auto delta=glm::dvec3(sb.position)-(glm::dvec3(sa.position)+rotation*((low+high)*.5));
            glm::dvec3 distance; for (unsigned j=0;j<3;++j) distance[j]=std::abs(glm::dot(delta,axis[j]))-half[j];
            const double signedDistance=glm::length(glm::max(distance,glm::dvec3(0)))+std::min(std::max({distance.x,distance.y,distance.z}),0.);
            return std::max(0.,double(gb.radius)-signedDistance);
        }
        void Phase21PairEvent(const Phase21Capture::PairState& v,unsigned reason,double depth)
        {
            if (!Phase21PairReserve(64)) return;
            auto& out=phase21->pairEvents;
            for (unsigned x:{episode,unsigned(v.a),unsigned(v.b),reason,v.first,tick,v.exposure,v.episodes,v.totalExposure,v.firstUnsafe,v.lastUnsafe,v.run}) Binary(out,x);
            Binary(out,v.peak); Binary(out,depth);
            if (!out) Fail("Phase21 pair event write INCONCLUSIVE");
        }
        void Phase21PairTrace(const Phase21Capture::PairState& v,const Phase21Capture::Row& row,double geometry,double dimension,unsigned flags)
        {
            if (!Phase21PairReserve(phase21->longTail?72:192)) return;
            auto& out=phase21->pairTrace;
            for (unsigned x:{episode,tick,unsigned(v.a),unsigned(v.b)}) Binary(out,x);
            if (phase21->longTail) {
                // Exact double difference of the authoritative float positions:
                // sufficient independent geometry for spheres and fixed boxes.
                const auto delta=glm::dvec3(row.body[v.b].position)-glm::dvec3(row.body[v.a].position);
                for (double x:{delta.x,delta.y,delta.z,v.anchor,geometry}) Binary(out,x);
                for (unsigned x:{flags,v.first,v.run,v.safeStart}) Binary(out,x);
                if (!out) Fail("Phase21 compact pair trace write INCONCLUSIVE");
                return;
            }
            for (unsigned i:{unsigned(v.a),unsigned(v.b)}) { Binary(out,row.slots[i]); Binary(out,row.generations[i]); }
            for (unsigned i:{unsigned(v.a),unsigned(v.b)}) {
                const auto& s=row.body[i];
                for (float x:{s.position.x,s.position.y,s.position.z,s.orientation.w,s.orientation.x,s.orientation.y,s.orientation.z,s.linear.x,s.linear.y,s.linear.z,s.angular.x,s.angular.y,s.angular.z}) Binary(out,x);
            }
            Binary(out,v.anchor); Binary(out,geometry); Binary(out,dimension);
            for (unsigned x:{flags,v.first,v.run,v.safeStart}) Binary(out,x);
            if (!out) Fail("Phase21 pair trace write INCONCLUSIVE");
        }
        bool Phase21DurationViolation(Phase21Capture::PairState& v,double depth,const char* reason)
        {
            auto& p=*phase21;
            if (!p.longTail) { Fail(reason); return false; }
            ++p.durationViolationSamples;
            if (!v.durationViolation) {
                v.durationViolation=true; ++p.durationViolations; v.peak=std::max(v.peak,depth);
                Phase21PairEvent(v,5,depth); // P1 FAIL observed, diagnostic continues only.
            }
            return true;
        }
        void Phase21PairsEnd()
        {
            auto& p=*phase21;
            if (!p.amended || p.cases[episode].kind!=0 || p.pairs.closed) return;
            p.pairs.closed=true;
            for (const auto& v:p.pairs.state) if (v.episodes) {
                Phase21PairEvent(v,v.first?3u:4u,0);
                if (v.first) { ++p.pendingAtEnd; if (!p.longTail && !failed) Fail("Phase21 pending recovery at end INCONCLUSIVE"); }
            }
            if (p.longTail || p.recovery03) for (unsigned key=0;key<p.pairs.state.size();++key) {
                const auto& d=p.pairs.differences[key]; if (!d.samples) continue;
                const auto& v=p.pairs.state[key];
                if (!Phase21PairReserve(64)) break;
                for (unsigned x:{episode,unsigned(v.a),unsigned(v.b),7u,d.first,d.last,d.samples,0u,0u,0u,0u,0u}) Binary(p.pairEvents,x);
                Binary(p.pairEvents,d.maximum); Binary(p.pairEvents,0.);
            }
            p.pairTrace.flush(); p.pairEvents.flush();
            if (!p.pairTrace || !p.pairEvents) Fail("Phase21 pair flush INCONCLUSIVE");
        }
        void Phase21Pairs(Phase21Capture::Row& row)
        {
            auto& p=*phase21; auto& s=p.pairs;
            for (unsigned i=0;i<s.pendingCount;++i) { const auto& v=s.state[s.pending[i]]; Phase21PairTouch(v.a,v.b); }
            const bool historical=(p.run=="L120" || p.longTail) && tick<=2*p.cases[episode].hz;
            if (historical) for (const auto [a,b]:Phase21Capture::HistoricalPairs) Phase21PairTouch(a,b);
            s.pendingCount=0;
            const unsigned hz=p.cases[episode].hz,H=hz/4;
            for (unsigned wi=0;wi<s.workCount;++wi) {
                const unsigned key=s.work[wi]; auto& v=s.state[key]; double dimension{};
                const unsigned N=p.recovery03 && v.a>=5?hz/5:hz/10;
                const double geometry=Phase21Geometry(v.a,v.b,row,dimension),depth=std::max(v.anchor,geometry);
                row.penetration=std::max(row.penetration,depth);
                if (p.longTail || p.recovery03) {
                    row.bodyPenetration[v.a]=std::max(row.bodyPenetration[v.a],depth);
                    row.bodyPenetration[v.b]=std::max(row.bodyPenetration[v.b],depth);
                    const double difference=std::abs(v.anchor-geometry);
                    if (v.hasAnchor && difference>1e-5) {
                        auto& d=s.differences[key]; ++d.samples; if (!d.first) d.first=tick; d.last=tick; d.maximum=std::max(d.maximum,difference);
                        if (p.recovery03) {
                            const unsigned category=p.audit.pairClass[key];
                            if (category==0 || category>=5) Fail("P21-ANCHOR-01 unexplained discrepancy class");
                            else ++p.audit.differences[category];
                        }
                    }
                }
                const bool sleeping=(inputs[v.a].inverseMass>0 && row.body[v.a].sleeping) || (inputs[v.b].inverseMass>0 && row.body[v.b].sleeping);
                const bool settled=(inputs[v.a].inverseMass==0 || s.quietTicks[v.a]>=hz/2) && (inputs[v.b].inverseMass==0 || s.quietTicks[v.b]>=hz/2);
                const bool moving=(inputs[v.a].inverseMass>0 && (!s.quiet[v.a] || !s.priorQuiet[v.a])) || (inputs[v.b].inverseMass>0 && (!s.quiet[v.b] || !s.priorQuiet[v.b]));
                const bool wasPending=v.first!=0; bool problem=false;
                if (wasPending && v.lastObserved+1!=tick) { Fail("Phase21 missing pair sample INCONCLUSIVE"); problem=true; }
                if (!std::isfinite(depth) || !std::isfinite(geometry) || !std::isfinite(dimension)) { Fail("Phase21 nonfinite pair geometry"); problem=true; }
                if (depth>.05) {
                    if (!v.first) { v.first=tick; v.exposure=v.run=v.safeStart=0; v.peak=0; v.durationViolation=false; ++v.episodes; if (!v.firstUnsafe) v.firstUnsafe=tick; }
                    ++v.run; ++v.exposure; ++v.totalExposure; v.lastUnsafe=tick; v.safeStart=0;
                    if (sleeping || settled || (!wasPending && !moving)) { Fail("Phase21 settled or sleeping penetration FAIL"); problem=true; }
                    if (depth>std::min(.1*dimension,.20)) { Fail("Phase21 dynamic penetration depth FAIL"); problem=true; }
                    if (v.run>N || tick>=v.first+N) if (!Phase21DurationViolation(v,depth,"Phase21 dynamic recovery deadline FAIL")) problem=true;
                } else if (v.first) {
                    v.run=0; if (!v.safeStart) v.safeStart=tick;
                    if (v.safeStart>v.first+N || tick>v.first+N+H-1) if (!Phase21DurationViolation(v,depth,"Phase21 recovery confirmation deadline FAIL")) problem=true;
                }
                v.lastObserved=tick; v.peak=std::max(v.peak,depth);
                const bool known=historical && std::find(Phase21Capture::HistoricalPairs.begin(),Phase21Capture::HistoricalPairs.end(),std::pair<unsigned,unsigned>{v.a,v.b})!=Phase21Capture::HistoricalPairs.end();
                if (v.first || known || problem) {
                    unsigned flags=unsigned(row.body[v.a].sleeping)|(unsigned(row.body[v.b].sleeping)<<1)|(unsigned(s.quiet[v.a])<<2)|(unsigned(s.quiet[v.b])<<3)|(unsigned(s.priorQuiet[v.a])<<4)|(unsigned(s.priorQuiet[v.b])<<5)|(unsigned(settled)<<6)|(unsigned(v.hasAnchor)<<7)|(unsigned(known)<<8)|(unsigned(v.first!=0)<<9)|(unsigned(problem)<<10);
                    if (p.longTail && v.durationViolation) flags|=2048u;
                    Phase21PairTrace(v,row,geometry,dimension,flags);
                }
                if (problem) Phase21PairEvent(v,2,depth);
                if (v.first && v.safeStart && tick-v.safeStart+1>=H && !problem) { Phase21PairEvent(v,p.longTail && v.durationViolation?6u:1u,depth); v.first=v.safeStart=v.run=v.exposure=0; }
                if (v.first) s.pending[s.pendingCount++]=std::uint16_t(key);
            }
        }
        void Phase21AuditCheck(bool pass,const char* reason)
        {
            ++phase21->audit.checks;
            if (!pass) { ++phase21->audit.failures; Fail(reason); }
        }
        unsigned Phase21OriginBucket(unsigned ia,unsigned ib,const Vec3f& localA,const Vec3f& localB)
        {
            std::uint64_t hash=14695981039346656037ull;
            for (auto word:{ia,ib,std::bit_cast<unsigned>(localA.x),std::bit_cast<unsigned>(localA.y),std::bit_cast<unsigned>(localA.z),
                            std::bit_cast<unsigned>(localB.x),std::bit_cast<unsigned>(localB.y),std::bit_cast<unsigned>(localB.z)}) { hash^=word; hash*=1099511628211ull; }
            auto& a=phase21->audit;
            for (unsigned probe=0;probe<a.originIndex.size();++probe) {
                const unsigned bucket=unsigned(hash+probe)&2047u,entry=a.originIndex[bucket];
                if (!entry) return bucket;
                const auto& v=a.origins[entry-1];
                if (v.a==ia && v.b==ib && v.localA==localA && v.localB==localB) return bucket;
            }
            Fail("P21-ANCHOR-01 origin lookup capacity INCONCLUSIVE"); return 0;
        }
        void Phase21Origin(const PD::Event& e)
        {
            auto& p=*phase21; auto& a=p.audit;
            if (p.cases[episode].kind!=0 || (e.operation!=PD::Operation::ContactNew && e.operation!=PD::Operation::ContactRefresh)) return;
            const unsigned ia=Index(e.a),ib=Index(e.b);
            if (ia>=185 || ib>=185 || ia==ib) { Fail("P21-ANCHOR-01 origin identity"); return; }
            const unsigned bucket=Phase21OriginBucket(ia,ib,e.localA,e.localB);
            const unsigned slot=a.originIndex[bucket]?a.originIndex[bucket]-1:a.originsUsed;
            if (slot==a.origins.size()) { Fail("P21-ANCHOR-01 origin capacity INCONCLUSIVE"); return; }
            if (slot==a.originsUsed) ++a.originsUsed;
            a.originIndex[bucket]=std::uint16_t(slot+1);
            auto& v=a.origins[slot];
            v={e.localA,e.localB,-e.normal,e.anchorA,e.anchorB,e.beforeA.position,e.beforeB.position,
               e.beforeA.orientation,e.beforeB.orientation,ia,ib,tick,0};
            // ContactNew/Refresh is after source witness construction and before any
            // retained-pose correction. No query or cache-changing getter is invoked.
            const RigidBody3D ca=*e.a;
            v.localNormal=Math::NormalizeOr(ca.GetWorldToBodyRotation()*e.normal);
            // These are the actual WriteContact operands. Sphere query-local
            // Update(0) may normalize orientation; a live-body inverse transform
            // is not an exact-bit substitute for that construction. Preserve it.
            if (inputs[ia].shape==0 && inputs[ib].shape==0) {
                const auto n=Math::NormalizeOr(e.beforeB.position-e.beforeA.position);
                // Canonical pair reversal reverses the source normal and witnesses.
                Phase21AuditCheck(e.anchorA==e.beforeA.position+n*inputs[ia].radius &&
                                  e.anchorB==e.beforeB.position-n*inputs[ib].radius,
                                  "P21-ANCHOR-01 analytic origin mismatch");
            } else Phase21AuditCheck((inputs[ia].shape==1 && inputs[ib].shape==0) ||
                                     (inputs[ib].shape==1 && inputs[ia].shape==0),"P21-ANCHOR-01 unsupported origin class");
        }
        unsigned Phase21Anchor(const contact_t& c,unsigned ia,unsigned ib,const Vec3f& pa,const Vec3f& pb,double raw)
        {
            auto& a=phase21->audit;
            const unsigned entry=a.originIndex[Phase21OriginBucket(ia,ib,c.ptOnA_LocalSpace,c.ptOnB_LocalSpace)];
            if (!entry) { Fail("P21-ANCHOR-01 missing origin INCONCLUSIVE"); return 0; }
            const unsigned slot=entry-1;
            auto& o=a.origins[slot]; o.seen=tick;
            const auto expectedNormal=o.born==tick?o.normal:-(a.beginRotation[ia]*o.localNormal);
            Phase21AuditCheck(c.normal==expectedNormal,"P21-ANCHOR-01 unexplained normal provenance");
            const bool spheres=inputs[ia].shape==0 && inputs[ib].shape==0;
            const unsigned category=(spheres?1u:3u)+unsigned(o.born!=tick);
            const glm::dvec3 ca(c.m_BodyA->m_Position),cb(c.m_BodyB->m_Position),n(c.normal);
            glm::dvec3 idealA{},idealB{}; double geometry{};
            if (spheres) {
                const double length=glm::length(cb-ca);
                if (!(length>0)) { Fail("P21-ANCHOR-01 coincident centers INCONCLUSIVE"); return 0; }
                const auto axis=(cb-ca)/length;
                idealA=ca+axis*double(inputs[ia].radius); idealB=cb-axis*double(inputs[ib].radius);
                geometry=double(inputs[ia].radius)+inputs[ib].radius-length;
            } else {
                const unsigned box=inputs[ia].shape==1?ia:ib,sphere=box==ia?ib:ia;
                if (inputs[box].shape!=1 || inputs[box].inverseMass!=0 || inputs[sphere].shape!=0 || inputs[box].points.empty()) {
                    Fail("P21-ANCHOR-01 unsupported current geometry"); return 0;
                }
                const auto* body=box==ia?c.m_BodyA:c.m_BodyB;
                glm::dvec3 low(inputs[box].points[0]),high(low);
                for (auto point:inputs[box].points) { low=glm::min(low,glm::dvec3(point)); high=glm::max(high,glm::dvec3(point)); }
                const auto rot=glm::mat3_cast(glm::dquat(body->m_Orientation));
                const auto center=glm::dvec3(body->m_Position)+rot*((low+high)*.5);
                const auto sc=box==ia?cb:ca; glm::dvec3 local{},half{}; std::array<glm::dvec3,3> axes{};
                for (unsigned j=0;j<3;++j) { const double scale=glm::length(rot[j]); axes[j]=rot[j]/scale; half[j]=(high[j]-low[j])*.5*scale; local[j]=glm::dot(sc-center,axes[j]); }
                glm::dvec3 closest=glm::clamp(local,-half,half),delta=local-closest; double distance=glm::length(delta);
                glm::dvec3 outward{};
                if (distance>0) { for (unsigned j=0;j<3;++j) outward+=axes[j]*(delta[j]/distance); geometry=inputs[sphere].radius-distance; }
                else {
                    unsigned face=0; for (unsigned j=1;j<3;++j) if (half[j]-std::abs(local[j])<half[face]-std::abs(local[face])) face=j;
                    distance=half[face]-std::abs(local[face]); const double sign=local[face]>=0?1.:-1.;
                    closest[face]=sign*half[face]; outward=axes[face]*sign; geometry=inputs[sphere].radius+distance;
                }
                auto bp=center; for (unsigned j=0;j<3;++j) bp+=axes[j]*closest[j];
                const auto sp=sc-outward*double(inputs[sphere].radius);
                idealA=box==ia?bp:sp; idealB=box==ia?sp:bp;
            }
            // Exact decomposition of distinct measurements, not a new acceptance
            // tolerance: ideal support projection, retained witness displacement,
            // original float transform/projection, and zero clipping.
            const double ideal=-glm::dot(idealA-idealB,n),actual=-glm::dot(glm::dvec3(pa)-glm::dvec3(pb),n);
            const double projection=ideal-geometry,witness=actual-ideal,arithmetic=raw-actual;
            const double clamping=(std::max(0.,raw)-raw)-(std::max(0.,geometry)-geometry);
            ++a.contacts[category]; const double difference=std::abs(std::max(0.,raw)-std::max(0.,geometry));
            const bool first=a.contacts[category]==1,worst=difference>a.worst[category];
            if (first || worst) {
                Phase21Capture::Attribution::Example example{};
                example.origin=o; example.episode=episode; example.tick=tick; example.category=category;
                example.identity={c.m_BodyA->GetIdentity().GetSlot(),c.m_BodyA->GetIdentity().GetGeneration(),c.m_BodyB->GetIdentity().GetSlot(),c.m_BodyB->GetIdentity().GetGeneration()};
                example.positionA=c.m_BodyA->m_Position; example.positionB=c.m_BodyB->m_Position;
                example.currentAnchorA=pa; example.currentAnchorB=pb;
                example.rotationA=c.m_BodyA->m_Orientation; example.rotationB=c.m_BodyB->m_Orientation; example.normal=c.normal;
                example.raw=raw; example.geometry=geometry; example.projection=projection; example.witness=witness; example.arithmetic=arithmetic; example.clamping=clamping;
                if (first) a.sample[category*2]=example;
                if (first || worst) a.sample[category*2+1]=example;
                a.worst[category]=std::max(a.worst[category],difference);
            }
            return category;
        }
        void Phase21Energy(const PD::Event& e,const std::array<double,2>& kineticBefore,
            const std::array<double,2>& kineticAfter,const std::array<double,2>& potentialChange)
        {
            auto& a=phase21->audit; const auto op=e.operation; const unsigned oi=unsigned(op);
            if (!e.a || !e.b || oi>=a.expected.size()) { Fail("P21-ENERGY-01 invalid event operands"); return; }
            const auto priorFailures=a.failures;
            Phase21Origin(e);
            if (phase21->refined) phase21->refinement.current.angular.Track(e);
            const auto* world=scene->GetPhysicsSystem()->GetPhysicsWorld();
            Phase21AuditCheck(e.a && e.b && world->IsBodyIdentityValid(e.a->GetIdentity()) && world->IsBodyIdentityValid(e.b->GetIdentity()),"P21-ENERGY-01 identity");
            double expected=0,measured=0,pairKinetic=0;
            for (unsigned role=0;role<2;++role) {
                const auto* body=role?e.b:e.a; if (!body || (role && body==e.a) || body->GetInverseMass()<=0) continue;
                const auto& before=role?e.beforeB:e.beforeA; const auto& after=role?e.afterB:e.afterA;
                const double kb=kineticBefore[role],ka=kineticAfter[role],du=potentialChange[role];
                pairKinetic+=kb; measured+=ka-kb+du;
                if (e.appliedImpulsesKnown) {
                    const glm::dvec3 j(role?e.linearImpulseB:e.linearImpulseA),l(role?e.angularImpulseB:e.angularImpulseA);
                    const RigidBody3D copy=*body; const auto inverse=glm::dmat3(copy.GetInverseInertiaTensorWorldSpace());
                    const auto dv=double(body->GetInverseMass())*j,dw=inverse*l;
                    expected+=glm::dot(glm::dvec3(before.linear),j)+glm::dot(glm::dvec3(before.angular),l)+.5*(glm::dot(j,dv)+glm::dot(l,dw));
                    for (unsigned angular=0;angular<2;++angular) for (unsigned k=0;k<3;++k) {
                        const double predicted=angular?dw[k]:dv[k];
                        const double actual=angular?double(after.angular[k])-before.angular[k]:double(after.linear[k])-before.linear[k];
                        const double error=std::abs(actual-predicted); a.impulseError=std::max(a.impulseError,error);
                        Phase21AuditCheck(error<=1e-5*std::max(1.,std::abs(predicted)),"P21-ENERGY-01 impulse equation");
                    }
                    Phase21AuditCheck(before.position==after.position && before.orientation==after.orientation,"P21-ENERGY-01 impulse pose");
                } else if (op==PD::Operation::GravityImpulse) {
                    const auto dv=glm::dvec3(world->GetGravity())*e.duration;
                    expected+=(glm::dot(glm::dvec3(before.linear),dv)+.5*glm::dot(dv,dv))/body->GetInverseMass();
                } else if (op==PD::Operation::Integrate) expected+=ka-kb+du;
                else if (op==PD::Operation::Position || op==PD::Operation::ToiPosition) {
                    expected+=du; Phase21AuditCheck(before.linear==after.linear && before.angular==after.angular,"P21-ENERGY-01 position velocity");
                } else if (op==PD::Operation::SleepDecision) expected+=ka-kb;
                else Phase21AuditCheck(PD::SameState(before,after),"P21-ENERGY-01 unsupported state mutation");
            }
            Phase21AuditCheck(std::isfinite(expected) && std::isfinite(measured),"P21-ENERGY-01 nonfinite work");
            const double error=std::abs(expected-measured); a.workError=std::max(a.workError,error);
            Phase21AuditCheck(error<=1e-5*std::max(1.,pairKinetic),"P21-ENERGY-01 operation work balance");
            if (op==PD::Operation::WarmStart || op==PD::Operation::NormalSliding) {
                const double normal=e.accumulatedAfter[0],tangent=std::hypot(e.accumulatedAfter[1],e.accumulatedAfter[2]);
                const double friction=double(e.a->m_Friction)*e.b->m_Friction;
                Phase21AuditCheck(normal>=-1e-6 && tangent-friction*normal<=1e-5*std::max(1.,std::abs(normal)),"P21-ENERGY-01 normal/Coulomb invariant");
            }
            if (op==PD::Operation::BlockNormal) Phase21AuditCheck(e.accumulatedAfter[0]>=-1e-6,"P21-ENERGY-01 block normal invariant");
            if (phase21->refined && (op==PD::Operation::Spin || op==PD::Operation::Rolling))
                Phase21AuditCheck(phase21->refinement.current.angular.Check(e,expected,pairKinetic),"P21-ENERGY-01 angular constrained minimum");
            else if (op==PD::Operation::ToiNormal || op==PD::Operation::ToiFriction || op==PD::Operation::Spin || op==PD::Operation::Rolling)
                Phase21AuditCheck(expected<=1e-5*std::max(1.,pairKinetic),"P21-ENERGY-01 dissipative operation invariant");
            a.expected[oi]+=expected; a.measured[oi]+=measured;
            if (phase21->refined && e.appliedImpulsesKnown) {
                const unsigned pass=e.reason==2111?1u:e.reason==2112?2u:0u;
                auto& r=phase21->refinement.current; ++r.operations[pass]; r.expected[pass]+=expected; r.measured[pass]+=measured;
                ++r.orderedEvents;
                auto hash=[&](auto value) { for (auto byte:std::bit_cast<std::array<unsigned char,sizeof(value)>>(value)) { r.orderHash^=byte; r.orderHash*=1099511628211ull; } };
                for (auto value:{std::uint64_t(oi),std::uint64_t(e.reason),e.a->GetIdentity().GetSlot(),e.a->GetIdentity().GetGeneration(),e.b->GetIdentity().GetSlot(),e.b->GetIdentity().GetGeneration()}) hash(value);
                hash(e.slot);
                for (const auto v:{e.localA,e.localB,e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB}) { hash(v.x); hash(v.y); hash(v.z); }
                if (pass) {
                    if (pass<r.localDirection || (pass==2 && r.localDirection==0)) Fail("Phase21 refinement operation order");
                    r.localDirection=pass;
                }
                if (pass && (op!=PD::Operation::NormalSliding && op!=PD::Operation::BlockNormal && op!=PD::Operation::Spin && op!=PD::Operation::Rolling))
                    Fail("Phase21 unexpected refinement impulse operation");
            }
            Phase21Capture::Attribution::EnergyExample sample{};
            sample.event=e; sample.episode=episode; sample.tick=tick; sample.a=Index(e.a); sample.b=Index(e.b);
            sample.identity={e.a->GetIdentity().GetSlot(),e.a->GetIdentity().GetGeneration(),e.b->GetIdentity().GetSlot(),e.b->GetIdentity().GetGeneration()};
            sample.expected=expected; sample.measured=measured; sample.kinetic=pairKinetic;
            if (expected>0 && !a.energySample[0].tick) a.energySample[0]=sample;
            if (expected>a.energySample[1].expected) a.energySample[1]=sample;
            if (a.failures!=priorFailures && !a.energySample[2].tick) a.energySample[2]=sample;
        }
        void Phase21AuditEnd(const Phase21Capture::Row& row)
        {
            auto& a=phase21->audit; double sum=0; for (double v:phase21->refined?a.expected:a.measured) sum+=v;
            const double residual=row.energy-a.beginEnergy-sum;
            // Same approved ledger reconstruction tolerance, not an energy-growth allowance.
            const double allowance=1e-4*std::max({1.,a.beginKinetic,std::abs(row.potentialChange[21]+row.potentialChange[4]+row.potentialChange[8])});
            Phase21AuditCheck(std::abs(residual)<=allowance && a.stageMask==4095,"P21-ENERGY-01 incomplete tick ledger");
            auto& out=a.ledger;
            out<<episode<<','<<tick<<','<<a.beginEnergy<<','<<row.energy<<','<<residual<<','<<allowance<<','<<a.impulseError<<','<<a.workError<<','<<a.checks<<','<<a.failures;
            for (unsigned i=0;i<24;++i) out<<','<<a.expected[i]<<','<<a.measured[i];
            for (double v:a.stageEnergy) out<<','<<v;
            for (unsigned i=1;i<5;++i) out<<','<<a.contacts[i]<<','<<a.differences[i]<<','<<a.worst[i];
            out<<'\n'; ++a.rows;
            unsigned used=0; for (unsigned i=0;i<a.originsUsed;++i) if (a.origins[i].seen==tick) a.origins[used++]=a.origins[i];
            a.originsUsed=used;
            a.originIndex.fill(0);
            for (unsigned i=0;i<used;++i) { const auto& o=a.origins[i]; a.originIndex[Phase21OriginBucket(o.a,o.b,o.localA,o.localB)]=std::uint16_t(i+1); }
            if (!out) Fail("Phase21 attribution ledger write INCONCLUSIVE");
        }
        void Phase21AuditPackets()
        {
            auto& a=phase21->audit; if (!phase21->recovery03) return;
            for (unsigned k=2;k<a.sample.size();++k) {
                const auto& e=a.sample[k]; if (!e.tick) continue;
                auto& out=a.packets; const auto& o=e.origin;
                out<<e.episode<<','<<e.tick<<','<<e.category<<','<<(k%2)<<','<<o.a<<','<<o.b<<','<<o.born;
                for (auto id:e.identity) out<<','<<id;
                for (auto v:{o.localA,o.localB,o.normal,o.anchorA,o.anchorB,o.positionA,o.positionB,e.positionA,e.positionB,e.normal,o.localNormal}) CsvVector(out,v);
                for (auto v:{o.rotationA,o.rotationB,e.rotationA,e.rotationB}) out<<','<<v.w<<','<<v.x<<','<<v.y<<','<<v.z;
                CsvVector(out,e.currentAnchorA); CsvVector(out,e.currentAnchorB);
                for (double v:{e.raw,e.geometry,e.projection,e.witness,e.arithmetic,e.clamping}) out<<','<<v;
                out<<'\n'; ++a.examples;
            }
            a.sample={};
            for (unsigned i=0;i<a.energySample.size();++i) {
                const auto& sample=a.energySample[i]; if (!sample.tick) continue;
                const auto& e=sample.event; auto& out=a.energyPackets;
                out<<sample.episode<<','<<sample.tick<<','<<i<<','<<unsigned(e.operation)<<','<<sample.a<<','<<sample.b;
                for (auto id:sample.identity) out<<','<<id;
                out<<','<<sample.expected<<','<<sample.measured<<','<<sample.kinetic;
                for (const auto& state:{e.beforeA,e.beforeB,e.afterA,e.afterB}) PostStateRow(out,state);
                for (auto v:{e.normal,e.anchorA,e.anchorB,e.localA,e.localB,e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB}) CsvVector(out,v);
                for (const auto& values:{e.normalRow,e.tangentRowU,e.tangentRowV}) for (double v:values) out<<','<<v;
                for (const auto& values:{e.accumulatedBefore,e.accumulatedAfter}) for (double v:values) out<<','<<v;
                out<<','<<e.restitution<<','<<e.closingSpeed<<','<<e.duration<<'\n';
            }
            a.energySample={};
            if (!a.packets) Fail("Phase21 attribution packet write INCONCLUSIVE");
            if (!a.energyPackets) Fail("Phase21 energy packet write INCONCLUSIVE");
        }
        static void Phase21Event(void* context,const PD::Event& event) noexcept
        {
            auto& s=*static_cast<Session*>(context); auto& p=*s.phase21;
            if (!event.a || !event.b) { s.Fail("Phase21 missing operation body"); return; }
            if (p.refined && RefinementSummary(event)) {
                p.refinement.current.angular.Track(event); s.Phase21RefinementSummary(event); return;
            }
            if (p.refined && event.operation==PD::Operation::Position) {
                if (event.a->GetInverseMass()>0) p.refinement.current.upward+=std::max(0.,double(event.afterA.position.y-event.beforeA.position.y));
                if (event.b->GetInverseMass()>0) p.refinement.current.upward+=std::max(0.,double(event.afterB.position.y-event.beforeB.position.y));
            }
            // Reuse only identical state-energy operands. Predicted impulse work
            // remains independent, and original metric accumulation stays A then B.
            std::array<double,2> kineticBefore{},kineticAfter{},potentialChange{};
            for (unsigned role=0;role<2;++role) {
                const auto* body=role?event.b:event.a;
                if (role && body==event.a) continue;
                const auto& before=role?event.beforeB:event.beforeA;
                const auto& after=role?event.afterB:event.afterA;
                kineticBefore[role]=s.Kinetic(*body,before);
                kineticAfter[role]=s.Kinetic(*body,after);
                potentialChange[role]=s.Potential(*body,after)-s.Potential(*body,before);
            }
            if (p.recovery03) s.Phase21Energy(event,kineticBefore,kineticAfter,potentialChange);
            if (p.causalMode) s.CausalEvent(event);
            if (p.run=="penetration" && event.operation==PD::Operation::Position) {
                auto& d=p.pen;
                if (!d.correcting || d.positionCount==d.positions.size()) { s.Fail("Phase21 position packet stage/capacity"); return; }
                auto& v=d.positions[d.positionCount++];
                v.a=s.Index(event.a); v.b=s.Index(event.b); v.slot=unsigned(event.slot);
                v.beforeA=event.beforeA.position; v.afterA=event.afterA.position;
                v.beforeB=event.beforeB.position; v.afterB=event.afterB.position;
                v.normal=event.normal; v.anchorA=event.anchorA; v.anchorB=event.anchorB;
                v.localA=event.localA; v.localB=event.localB; v.depth=event.penetration;
            }
            if (p.used>=p.rows.size()) { s.Fail("Phase21 row overflow"); return; }
            auto& row=p.rows[p.used]; const unsigned op=unsigned(event.operation);
            if (op>=row.operations.size()) { s.Fail("Phase21 operation coverage"); return; }
            ++row.operations[op];
            row.kineticChange[op]+=kineticAfter[0]-kineticBefore[0];
            row.potentialChange[op]+=potentialChange[0];
            if (event.b!=event.a) {
                row.kineticChange[op]+=kineticAfter[1]-kineticBefore[1];
                row.potentialChange[op]+=potentialChange[1];
            }
            if (event.operation==PD::Operation::ToiNormal || event.operation==PD::Operation::BlockNormal)
                row.normalImpulse+=glm::length(glm::dvec3(event.linearImpulseB));
            if (event.operation==PD::Operation::ToiFriction || event.operation==PD::Operation::NormalSliding)
                row.frictionImpulse+=glm::length(glm::dvec3(event.linearImpulseB));
            if (event.operation==PD::Operation::ToiNormal) row.restitution+=event.restitution;
        }
        void Phase21FinalizeTick()
        {
            auto& p=*phase21; if (!p.pending || !p.used) return;
            auto& row=p.rows[p.used-1]; const auto after=GetPhysicsProfileSnapshot();
            for (unsigned i=0;i<ProfileFields.size();++i) row.cpu[i]=after.*ProfileFields[i].second-row.before.*ProfileFields[i].second;
            row.eventNs=sink.observationNs-row.eventStart;
            const auto t=scene->GetPhysicsSystem()->GetSleepTimings();
            row.sleepCpu={t.sourceCheckNs,t.wakeNs,t.depthNs,t.eligibilityNs,t.qualificationNs,t.sourceStoreNs,t.stepFlagsNs};
            p.pending=false;
        }
        void Phase21PenStage(PD::Stage stage,const PhysicsWorld& world,ManifoldCollector& manifolds)
        {
            auto& d=phase21->pen;
            const unsigned k=stage==PD::Stage::Narrowphase?0u:stage==PD::Stage::Integration?1u:
                stage==PD::Stage::Correction?2u:stage==PD::Stage::End?3u:4u;
            if (k==4) return;
            d.correcting=stage==PD::Stage::Integration;
            auto& s=d.stages[k]; const auto& all=world.GetPhysicsBodies();
            s.stage=unsigned(stage); s.tick=tick; s.count=unsigned(all.size()); s.contacts=0;
            if (all.size()!=185) { Fail("Phase21 packet membership"); return; }
            for (unsigned i=0;i<all.size();++i) {
                const auto& b=*all[i];
                if (!b.HasFiniteState() || !world.IsBodyIdentityValid(b.GetIdentity())) { Fail("Phase21 packet body identity/state"); return; }
                s.body[i]=PD::Capture(b); s.slots[i]=b.GetIdentity().GetSlot(); s.generations[i]=b.GetIdentity().GetGeneration();
            }
            unsigned mi=0;
            for (auto& m:manifolds.m_Manifolds) {
                for (int j=0;j<m.GetNumContacts();++j) {
                    if (s.contacts==s.contact.size()) { Fail("Phase21 packet contact capacity"); return; }
                    const auto c=m.GetContact(j); const auto ia=Index(c.m_BodyA),ib=Index(c.m_BodyB);
                    if (ia>=all.size() || ib>=all.size() || all[ia]!=c.m_BodyA || all[ib]!=c.m_BodyB ||
                        m.GetBodyAIdentity()!=c.m_BodyA->GetIdentity() || m.GetBodyBIdentity()!=c.m_BodyB->GetIdentity()) {
                        Fail("Phase21 packet contact identity"); return;
                    }
                    auto& v=s.contact[s.contacts++]; v.a=ia; v.b=ib; v.manifold=mi; v.slot=unsigned(j);
                    v.featureA=c.featureA; v.featureB=c.featureB; v.cachedA=c.ptOnA_WorldSpace; v.cachedB=c.ptOnB_WorldSpace;
                    v.localA=c.ptOnA_LocalSpace; v.localB=c.ptOnB_LocalSpace; v.normal=c.normal;
                    const RigidBody3D a=*c.m_BodyA,b=*c.m_BodyB; // Derived cache refresh stays on copies.
                    v.currentA=a.BodySpaceToWorldSpace(v.localA); v.currentB=b.BodySpaceToWorldSpace(v.localB);
                    v.separation=c.separationDistance; v.toi=c.timeOfImpact;
                    v.depth=-double(glm::dot(v.currentA-v.currentB,v.normal));
                }
                ++mi;
            }
        }
        void Phase21PenWrite(const Phase21Capture::Row& row)
        {
            auto& p=*phase21; auto& d=p.pen;
            if (d.written || failed) return;
            for (const auto& s:d.stages) if (s.tick!=tick || s.count!=185) { Fail("Phase21 incomplete packet"); return; }
            d.written=true;
            const std::uint64_t named=sizeof(Session)+sizeof(Phase21Capture)+row.capacity;
            if (named>64*1024*1024) { Fail("Phase21 named storage limit"); return; }
            std::ofstream out(std::filesystem::path(config.outputDirectory)/"first-failure.bin",std::ios::binary);
            out.write("P21PEN01",8);
            for (unsigned v:{1u,episode,tick,4u,d.positionCount,d.source,d.a,d.b,d.manifold,d.slot,p.cases[episode].passes,p.cases[episode].hz}) Binary(out,std::uint32_t(v));
            for (double v:{row.penetration,d.ground,row.energy,.05,30542.4}) Binary(out,v);
            for (std::uint64_t v:{std::uint64_t(sizeof(Phase21Capture::PenPacket)),std::uint64_t(sizeof(Phase21Capture)),std::uint64_t(sizeof(Session)),named}) Binary(out,v);
            auto vec=[&](Vec3f v) { Binary(out,v.x); Binary(out,v.y); Binary(out,v.z); };
            for (const auto& s:d.stages) {
                for (unsigned v:{s.stage,s.tick,s.count,s.contacts}) Binary(out,std::uint32_t(v));
                for (unsigned i=0;i<s.count;++i) {
                    const auto& b=s.body[i]; Binary(out,std::uint32_t(i)); Binary(out,s.slots[i]); Binary(out,s.generations[i]);
                    vec(b.position); for (float v:{b.orientation.w,b.orientation.x,b.orientation.y,b.orientation.z}) Binary(out,v);
                    vec(b.linear); vec(b.angular); Binary(out,b.inactive); Binary(out,std::uint8_t(b.sleeping));
                }
                for (unsigned i=0;i<s.contacts;++i) {
                    const auto& c=s.contact[i];
                    for (unsigned v:{c.a,c.b,c.manifold,c.slot,c.featureA,c.featureB}) Binary(out,std::uint32_t(v));
                    for (Vec3f v:{c.cachedA,c.cachedB,c.localA,c.localB,c.normal,c.currentA,c.currentB}) vec(v);
                    Binary(out,c.separation); Binary(out,c.toi); Binary(out,c.depth);
                }
            }
            for (unsigned i=0;i<d.positionCount;++i) {
                const auto& v=d.positions[i]; for (unsigned n:{v.a,v.b,v.slot}) Binary(out,std::uint32_t(n));
                for (Vec3f x:{v.beforeA,v.afterA,v.beforeB,v.afterB,v.normal,v.anchorA,v.anchorB,v.localA,v.localB}) vec(x);
                Binary(out,v.depth);
            }
            const auto size=out.tellp(); out.flush();
            if (!out || size<0 || size>1024*1024) Fail("Phase21 packet export limit/write");
            else p.packetBytes=std::uint64_t(size);
        }
        void Phase21Boundary(PD::Stage stage,const PhysicsWorld& world,ManifoldCollector& manifolds,
                             std::span<const ContactIsland> islands,std::size_t bytes,int passes)
        {
            auto& p=*phase21; const auto start=PD::Nanoseconds();
            if (stage==PD::Stage::Begin) {
                Phase21FinalizeTick();
                if (p.used==p.rows.size()) { Fail("Phase21 row overflow"); scene->SetPaused(true); return; }
                p.rows[p.used]=Phase21Capture::Row{}; auto& row=p.rows[p.used];
                row.tick=++tick; row.before=GetPhysicsProfileSnapshot(); row.eventStart=sink.observationNs;
                if (p.refined) p.refinement.current={};
                if (p.amended) p.pairs.workCount=0;
                if (p.recovery03) {
                    auto& a=p.audit; a.expected={}; a.measured={}; a.stageEnergy={}; a.stageMask=0;
                    a.beginEnergy=a.beginKinetic=a.impulseError=a.workError=0;
                    a.pairClass.fill(0);
                    unsigned i=0; for (const auto* body:world.GetPhysicsBodies()) {
                        const auto state=PD::Capture(*body); a.beginKinetic+=Kinetic(*body,state);
                        a.beginEnergy+=Kinetic(*body,state)+Potential(*body,state);
                        const RigidBody3D copy=*body;
                        if (i>=a.beginRotation.size()) { Fail("Phase21 attribution body capacity"); break; }
                        a.beginRotation[i++]=copy.GetBodyToWorldRotation();
                    }
                }
                if (p.run=="penetration") { p.pen.positionCount=0; p.pen.source=0; p.pen.ground=0; p.pen.correcting=false; for (auto& s:p.pen.stages) s.tick=0; }
                if (passes!=int(p.cases[episode].passes) || tick>TickLimit()) { Fail("Phase21 tick/pass identity"); scene->SetPaused(true); }
            }
            if (p.causalMode) CausalStage(stage,world,manifolds);
            if (p.used>=p.rows.size()) return;
            auto& row=p.rows[p.used];
            if (p.recovery03) {
                const unsigned si=unsigned(stage);
                if (si>=12 || (p.audit.stageMask&(1u<<si))) { Fail("P21-ENERGY-01 stage coverage"); scene->SetPaused(true); return; }
                p.audit.stageMask|=1u<<si;
                for (const auto* body:world.GetPhysicsBodies()) { const auto state=PD::Capture(*body); p.audit.stageEnergy[si]+=Kinetic(*body,state)+Potential(*body,state); }
            }
            if (p.run=="penetration") {
                Phase21PenStage(stage,world,manifolds);
                if (failed) { scene->SetPaused(true); return; }
            }
            if (stage==PD::Stage::End) {
                const auto& all=world.GetPhysicsBodies(); row.count=unsigned(all.size()); row.islands=unsigned(islands.size());
                if (all.size()!=inputs.size() || all.size()>row.body.size()) { Fail("Phase21 membership"); scene->SetPaused(true); return; }
                auto hash=[&](auto value) { const auto data=std::bit_cast<std::array<unsigned char,sizeof(value)>>(value); for (auto c:data) { row.hash^=c; row.hash*=1099511628211ull; } };
                for (unsigned i=0;i<all.size();++i) {
                    const auto& body=*all[i]; const auto state=PD::Capture(body);
                    if (!body.HasFiniteState() || !world.IsBodyIdentityValid(body.GetIdentity())) { Fail("Phase21 invalid body state/identity"); scene->SetPaused(true); return; }
                    row.body[i]=state; row.slots[i]=body.GetIdentity().GetSlot(); row.generations[i]=body.GetIdentity().GetGeneration();
                    if (p.amended && p.cases[episode].kind==0 && (row.slots[i]!=p.pairs.slots[i] || row.generations[i]!=p.pairs.generations[i])) { Fail("Phase21 pair identity changed INCONCLUSIVE"); scene->SetPaused(true); return; }
                    if (p.longTail && i<5 && (state.position!=inputs[i].position || state.orientation!=inputs[i].orientation)) { Fail("Phase21 static geometry changed INCONCLUSIVE"); scene->SetPaused(true); return; }
                    for (float f:{state.position.x,state.position.y,state.position.z,state.orientation.w,state.orientation.x,state.orientation.y,state.orientation.z,state.linear.x,state.linear.y,state.linear.z,state.angular.x,state.angular.y,state.angular.z}) hash(f);
                    hash(state.inactive); hash(std::uint8_t(state.sleeping));
                    if (body.GetInverseMass()>0) {
                        const double v=glm::length(glm::dvec3(state.linear)),w=glm::length(glm::dvec3(state.angular));
                        row.sleeping+=state.sleeping; const auto thresholds=body.GetSleepSettings();
                        row.quiet+=state.sleeping || (v<=thresholds.linearSpeedThreshold && w<=thresholds.angularSpeedThreshold);
                        if (p.amended && p.cases[episode].kind==0) {
                            auto& ps=p.pairs; ps.priorQuiet[i]=ps.quiet[i];
                            ps.quiet[i]=v<=thresholds.linearSpeedThreshold && w<=thresholds.angularSpeedThreshold;
                            ps.quietTicks[i]=ps.quiet[i]?ps.quietTicks[i]+1:0;
                        }
                        row.wakes+=previous[i].sleeping && !state.sleeping; row.sleeps+=!previous[i].sleeping && state.sleeping;
                        row.kinetic+=Kinetic(body,state); row.potential+=Potential(body,state);
                        row.linear=std::max(row.linear,v); row.angular=std::max(row.angular,w);
                        row.drift=std::max(row.drift,glm::length(glm::dvec3(state.position)-glm::dvec3(inputs[i].position)));
                        const auto& ground=inputs[p.cases[episode].kind==0 || p.cases[episode].kind==4?4:0];
                        double top=-std::numeric_limits<double>::max();
                        for (auto point:ground.points) top=std::max(top,double((ground.position+ground.orientation*point).y));
                        if (std::abs(state.position.x)<49 && std::abs(state.position.z)<49) {
                            double bottom=state.position.y;
                            if (inputs[i].shape==0) bottom-=inputs[i].radius;
                            else { bottom=std::numeric_limits<double>::max(); for (auto point:inputs[i].points) bottom=std::min(bottom,double((state.position+state.orientation*point).y)); }
                            const double depth=top-bottom;
                            if (p.run=="penetration") {
                                p.pen.ground=std::max(p.pen.ground,depth);
                                if (depth>row.penetration) { p.pen.source=1; p.pen.a=i; p.pen.b=p.cases[episode].kind==0?4u:0u; p.pen.manifold=p.pen.slot=~0u; }
                            }
                            row.penetration=std::max(row.penetration,depth); row.bodyPenetration[i]=std::max(row.bodyPenetration[i],depth);
                            // Preserve the existing ground observation as a real
                            // sphere/boundary pair, even without a manifold.
                            if (p.amended && p.cases[episode].kind==0 && depth>0) Phase21PairTouch(4,i);
                        }
                    }
                    previous[i]=state;
                }
                for (auto& manifold:manifolds.m_Manifolds) for (int j=0;j<manifold.GetNumContacts();++j) {
                    const auto c=manifold.GetContact(j);
                    if (!c.m_BodyA || !c.m_BodyB || !world.IsBodyIdentityValid(c.m_BodyA->GetIdentity()) || !world.IsBodyIdentityValid(c.m_BodyB->GetIdentity())) { Fail("Phase21 stale contact"); scene->SetPaused(true); return; }
                    const RigidBody3D a=*c.m_BodyA,b=*c.m_BodyB;
                    const auto pa=a.BodySpaceToWorldSpace(c.ptOnA_LocalSpace),pb=b.BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
                    const double depth=-double(glm::dot(pa-pb,c.normal));
                    if (!std::isfinite(depth)) Fail("Phase21 nonfinite contact");
                    if (p.run=="penetration" && depth>row.penetration) {
                        const auto& c=p.pen.stages[3].contact[row.contacts];
                        p.pen.source=2; p.pen.a=c.a; p.pen.b=c.b; p.pen.manifold=c.manifold; p.pen.slot=c.slot;
                    }
                    row.penetration=std::max(row.penetration,depth); ++row.contacts;
                    const auto ia=Index(c.m_BodyA),ib=Index(c.m_BodyB);
                    if (ia>=all.size() || ib>=all.size() || all[ia]!=c.m_BodyA || all[ib]!=c.m_BodyB) { Fail("Phase21 contact mapping"); scene->SetPaused(true); return; }
                    ++row.bodyContacts[ia]; ++row.bodyContacts[ib];
                    row.bodyPenetration[ia]=std::max(row.bodyPenetration[ia],depth); row.bodyPenetration[ib]=std::max(row.bodyPenetration[ib],depth);
                    if (p.amended && p.cases[episode].kind==0) {
                        const unsigned key=Phase21PairTouch(ia,ib); auto& pair=p.pairs.state[key];
                        if (p.recovery03) {
                            const unsigned category=Phase21Anchor(c,ia,ib,pa,pb,depth);
                            if (!pair.hasAnchor || depth>pair.anchor) p.audit.pairClass[key]=static_cast<unsigned char>(category);
                        }
                        pair.anchor=std::max(pair.anchor,depth); pair.hasAnchor=true;
                    }
                }
                if (p.amended && p.cases[episode].kind==0) Phase21Pairs(row);
                row.energy=row.kinetic+row.potential; row.capacity=bytes;
                if (p.recovery03) Phase21AuditEnd(row);
                if (p.refined) Phase21RefinementEnd(row,manifolds);
                if (bytes>lastStorage) storageGrowth+=bytes-lastStorage; lastStorage=bytes; row.growth=storageGrowth;
                debugLineCapacity=std::max(std::size_t{32768},all.size()*63+std::size_t(row.contacts)*50);
                if (!std::isfinite(row.energy)) Fail("Phase21 nonfinite energy");
                if (p.cases[episode].kind==0 && ((!p.amended && row.penetration>.05) || row.energy>30542.4)) {
                    if (p.run=="penetration") Phase21PenWrite(row);
                    Fail("Phase21 lattice safety bound");
                }
                if (p.amended && sizeof(Session)+sizeof(Phase21Capture)+row.capacity>64*1024*1024) { overflow=true; Fail("Phase21 named storage capacity INCONCLUSIVE"); }
                if (p.cases[episode].kind==2 && tick==300) { p.impulsePending=true; scene->SetPaused(true); }
                if (p.cases[episode].kind==2 && tick==301 && row.body[1].sleeping) Fail("Phase21 next-tick wake");
                row.callbackNs+=PD::Nanoseconds()-start; ++p.used; ++p.totalTicks; p.pending=true;
                if (failed || tick==TickLimit()) { episodeDone=true; scene->SetPaused(true); }
                return;
            }
            row.callbackNs+=PD::Nanoseconds()-start;
        }
        void Phase21Flush()
        {
            auto& p=*phase21; Phase21FinalizeTick();
            for (unsigned k=0;k<p.used;++k) {
                const auto& row=p.rows[k];
                metrics<<episode<<','<<row.tick<<','<<row.count<<','<<row.contacts<<','<<row.islands<<','<<row.quiet<<','<<row.sleeping<<','<<row.wakes<<','<<row.sleeps;
                for (double v:{row.energy,row.kinetic,row.potential,row.penetration,row.linear,row.angular,row.drift}) metrics<<','<<v;
                metrics<<','<<row.hash<<','<<row.capacity<<','<<row.growth<<','<<row.callbackNs<<','<<row.eventNs<<','<<row.normalImpulse<<','<<row.frictionImpulse<<','<<row.restitution;
                for (auto v:row.cpu) metrics<<','<<v;
                for (auto v:row.sleepCpu) metrics<<','<<v;
                for (unsigned i=0;i<24;++i) metrics<<','<<row.operations[i]<<','<<row.kineticChange[i]<<','<<row.potentialChange[i];
                metrics<<'\n';
                if (p.cases[episode].kind!=0 || row.tick>=110*p.cases[episode].hz) for (unsigned i=0;i<row.count;++i) {
                    Binary(bodies,episode); Binary(bodies,row.tick); Binary(bodies,i);
                    Binary(bodies,row.slots[i]); Binary(bodies,row.generations[i]); BinaryState(bodies,row.body[i]);
                    Binary(bodies,row.bodyContacts[i]);
                    if (p.longTail || p.recovery03) Binary(bodies,row.bodyPenetration[i]);
                }
            }
            p.used=0;
            if (!metrics || !bodies) Fail("Phase21 evidence write");
        }
        void Phase21CommitCase()
        {
            auto& p=*phase21;
            // Commit existing raw evidence before the next case can start. A case
            // capture marker never substitutes for numerical or shutdown PASS.
            const std::array<std::pair<const char*,std::ofstream*>,13> streams{{
                {"bodies.bin",&bodies},{"metrics.csv",&metrics},{"frames.csv",&frames},
                {"initial.csv",&initial},{"episodes.csv",&events},{"impulse.csv",&timing},
                {"pair-trace.bin",&p.pairTrace},{"pair-events.bin",&p.pairEvents},
                {"attribution.csv",&p.audit.ledger},{"anchor-examples.csv",&p.audit.packets},
                {"energy-examples.csv",&p.audit.energyPackets},{"refinement-work.csv",&p.refinement.work},{"direct-refinement.jsonl",&p.refinement.direct}}};
            for (const auto& [name,stream]:streams) {
                if (!stream->is_open()) continue;
                stream->flush();
                if (!*stream || stream->tellp()<0) { Fail("Phase21 completed-case flush INCONCLUSIVE"); return; }
            }
            const auto path=std::filesystem::path(config.outputDirectory)/("case-"+std::to_string(episode)+"-capture.json");
            if (std::filesystem::exists(path)) { Fail("Phase21 case evidence already exists"); return; }
            std::ofstream out(path);
            out<<"{\"schema\":1,\"episode\":"<<episode<<",\"ticks\":"<<tick<<",\"expected_ticks\":"<<TickLimit()
               <<",\"name\":"<<std::quoted(EpisodeName())<<",\"hz\":"<<p.cases[episode].hz<<",\"rendered_application_scene\":"<<(p.renderedSupport?"true":"false")
               <<",\"capture_complete\":true,\"qualification_pass\":false,\"application_shutdown_observed\":false,\"stream_end_offsets\":{";
            bool first=true;
            for (const auto& [name,stream]:streams) {
                if (!stream->is_open()) continue;
                if (!first) out<<','; first=false;
                out<<'"'<<name<<"\":"<<stream->tellp();
            }
            out<<"},\"attribution_checks\":"<<p.audit.checks<<",\"attribution_failures\":"<<p.audit.failures<<"}\n";
            const auto bytes=out.tellp(); out.flush();
            if (!out || bytes<0) { Fail("Phase21 case metadata write INCONCLUSIVE"); return; }
            out.close();
            if (!out) { Fail("Phase21 case metadata close INCONCLUSIVE"); return; }
            p.caseMetadataBytes+=std::uint64_t(bytes); p.exportBytes+=std::uint64_t(bytes);
            if (p.caseMetadataBytes>4096 || p.exportBytes>64*1024*1024) { overflow=true; Fail("Phase21 case metadata capacity INCONCLUSIVE"); }
        }
        bool Phase21Frame(double elapsed)
        {
            auto& p=*phase21;
            if (sixtyHzScene && !complete && !failed) scene->Update(Timestep(elapsed));
            Phase21Flush(); const auto t=scene->GetPhysicsTiming();
            frames<<episode<<','<<tick<<','<<elapsed<<','<<t.pendingSeconds<<','<<t.discardedSeconds<<','<<t.totalSteps<<','<<t.stepsLastUpdate<<','<<scene->GetRenderInterpolationAlpha()<<'\n';
            if (!std::isfinite(t.discardedSeconds) || t.discardedSeconds<0 || !frames) Fail("Phase21 invalid time/frame evidence");
            p.discardedWallSeconds+=t.discardedSeconds;
            if (p.impulsePending && !failed) {
                auto* body=scene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies()[1];
                const bool slept=body->IsSleeping(); const auto before=Kinetic(*body,PD::Capture(*body))+Potential(*body,PD::Capture(*body));
                if (!slept) Fail("Phase21 wake premise: sphere not sleeping at five seconds");
                else {
                    body->ApplyImpulseLinear({.5f,0,0});
                    const auto after=Kinetic(*body,PD::Capture(*body))+Potential(*body,PD::Capture(*body));
                    timing<<episode<<','<<tick<<','<<slept<<','<<!body->IsSleeping()<<','<<before<<','<<after<<",0.5\n";
                    p.impulseApplied=true; scene->SetPaused(false);
                }
                p.impulsePending=false;
            }
            if (episodeDone && !failed) {
                if (p.recovery03) Phase21AuditPackets();
                Phase21PairsEnd();
                if (t.totalSteps!=TickLimit() || scene->SetPhysicsStep(Seconds{StepSeconds()==1.0/60.0?1.0/120.0:1.0/60.0}) || scene->GetPhysicsStep().count()!=StepSeconds()) Fail("Phase21 clock/tick identity");
                else { ++completedEpisodes; completedTicks+=tick; if (completedEpisodes==p.count) complete=true; }
            }
            std::uint64_t size=p.packetBytes+(p.amended?p.pairBytes:0)+p.caseMetadataBytes;
            for (auto* out:{&bodies,&metrics,&frames,&initial,&events,&timing}) { const auto n=out->tellp(); if (n<0) Fail("Phase21 stream position"); else size+=std::uint64_t(n); }
            p.exportBytes=size;
            if (p.refined) for (auto* out:{&p.refinement.work,&p.refinement.direct}) if (out->is_open()) {
                const auto n=out->tellp(); if (n<0) Fail("Phase21 refinement stream position"); else p.exportBytes+=std::uint64_t(n);
            }
            size=p.exportBytes;
            if (p.recovery03) {
                for (auto* out:{&p.audit.ledger,&p.audit.packets,&p.audit.energyPackets}) {
                    const auto n=out->tellp(); if (n<0) Fail("Phase21 attribution stream position"); else p.exportBytes+=std::uint64_t(n);
                }
                if (p.audit.packets.tellp()>=0 && p.audit.energyPackets.tellp()>=0 && p.pairBytes+p.packetBytes+std::uint64_t(p.audit.packets.tellp())+std::uint64_t(p.audit.energyPackets.tellp())>8*1024*1024) { overflow=true; Fail("Phase21 attribution pair export limit"); }
                size=p.exportBytes;
            }
            if (size>64*1024*1024-4096) { overflow=true; Fail("Phase21 export limit"); }
            if (episodeDone && !failed) Phase21CommitCase();
            if (failed || complete) { Phase21PairsEnd(); if (p.causalMode) CausalClose(); RetireOwnedScene(); }
            return !failed;
        }
        bool Phase21Finish(bool clean)
        {
            PD::observer=nullptr;
            if (phase21->refined) for (auto* stream:{&phase21->refinement.work,&phase21->refinement.direct}) if (stream->is_open()) {
                stream->flush(); if (!*stream) Fail("Phase21 refinement final flush"); stream->close();
            }
            if (phase21->recovery03) {
                Phase21AuditPackets(); phase21->audit.ledger.flush(); phase21->audit.packets.flush(); phase21->audit.energyPackets.flush();
                if (!phase21->audit.ledger || !phase21->audit.packets || !phase21->audit.energyPackets) Fail("Phase21 final attribution flush");
                phase21->audit.ledger.close(); phase21->audit.packets.close(); phase21->audit.energyPackets.close();
            }
            if (phase21->amended) { Phase21PairsEnd(); phase21->pairTrace.close(); phase21->pairEvents.close(); }
            for (auto* out:{&bodies,&metrics,&frames,&initial,&events,&timing}) { out->flush(); if (!*out) Fail("Phase21 final flush"); out->close(); }
            const bool ok=clean && complete && !failed && completedEpisodes==phase21->count && !overflow;
            std::ofstream out(std::filesystem::path(config.outputDirectory)/"result.json");
            out<<"{\"schema\":1,\"phase\":21,\"complete\":"<<(ok?"true":"false")<<",\"shutdown\":"<<(clean?"true":"false")
               <<",\"episodes\":"<<completedEpisodes<<",\"ticks\":"<<phase21->totalTicks<<",\"observer_bytes\":"<<sizeof(Phase21Capture)
               <<",\"diagnostic_only\":"<<(phase21->longTail?"true":"false")<<",\"duration_violations\":"<<phase21->durationViolations
               <<",\"duration_violation_samples\":"<<phase21->durationViolationSamples<<",\"pending_recovery_episodes\":"<<phase21->pendingAtEnd
               <<",\"refinement_direct_checks\":"<<phase21->refinement.directChecks<<",\"refinement_direct_failures\":"<<phase21->refinement.directFailures
               <<",\"refinement_summary_ticks\":"<<phase21->refinement.summaryTicks<<",\"refinement_eligible_ticks\":"<<phase21->refinement.eligibleTicks<<",\"refinement_extra_events\":"<<phase21->refinement.extraEvents
               <<",\"session_bytes\":"<<sizeof(Session)<<",\"export_bytes\":"<<phase21->exportBytes<<",\"discarded_wall_seconds\":"<<std::setprecision(17)<<phase21->discardedWallSeconds<<",\"profiling_enabled\":"<<(IsPhysicsProfilingEnabled()?"true":"false")<<",\"failure\":\""<<failure<<"\"}\n";
            return ok && bool(out);
        }
        // P21_END

        inline static constexpr std::array<std::pair<const char*,std::uint64_t PhysicsProfileSnapshot::*>,25> ProfileFields{{
            {"physicsWorldTimeNs",&PhysicsProfileSnapshot::physicsWorldTimeNs},{"narrowphaseTimeNs",&PhysicsProfileSnapshot::narrowphaseTimeNs},
            {"gjkTimeNs",&PhysicsProfileSnapshot::gjkTimeNs},{"epaTimeNs",&PhysicsProfileSnapshot::epaTimeNs},
            {"manifoldTimeNs",&PhysicsProfileSnapshot::manifoldTimeNs},{"solverTimeNs",&PhysicsProfileSnapshot::solverTimeNs},
            {"contactResolutionTimeNs",&PhysicsProfileSnapshot::contactResolutionTimeNs},{"broadphaseTimeNs",&PhysicsProfileSnapshot::broadphaseTimeNs},
            {"candidatePairCount",&PhysicsProfileSnapshot::candidatePairCount},{"narrowphaseCallCount",&PhysicsProfileSnapshot::narrowphaseCallCount},
            {"gjkCallCount",&PhysicsProfileSnapshot::gjkCallCount},{"gjkIterationCount",&PhysicsProfileSnapshot::gjkIterationCount},
            {"epaCallCount",&PhysicsProfileSnapshot::epaCallCount},{"epaIterationCount",&PhysicsProfileSnapshot::epaIterationCount},
            {"epaFailureCount",&PhysicsProfileSnapshot::epaFailureCount},{"supportCallCount",&PhysicsProfileSnapshot::supportCallCount},
            {"generatedContactCount",&PhysicsProfileSnapshot::generatedContactCount},{"solverIterationCount",&PhysicsProfileSnapshot::solverIterationCount},
            {"integratedBodyCount",&PhysicsProfileSnapshot::integratedBodyCount},{"contactIslandCapacityGrowthCount",&PhysicsProfileSnapshot::contactIslandCapacityGrowthCount},
            {"gravityTimeNs",&PhysicsProfileSnapshot::gravityTimeNs},{"integrationTimeNs",&PhysicsProfileSnapshot::integrationTimeNs},
            {"spinResistanceImpulseCount",&PhysicsProfileSnapshot::spinResistanceImpulseCount},{"rollingResistanceImpulseCount",&PhysicsProfileSnapshot::rollingResistanceImpulseCount},
            {"stepCount",&PhysicsProfileSnapshot::stepCount}}};
        double Kinetic(const RigidBody3D& body,const PD::BodyState& state) const noexcept
        {
            if (body.GetInverseMass()<=0) return 0;
            const auto rotation=glm::dmat3(glm::mat3_cast(state.orientation));
            const auto w=glm::transpose(rotation)*glm::dvec3(state.angular);
            return .5/body.GetInverseMass()*(glm::dot(glm::dvec3(state.linear),glm::dvec3(state.linear))+
                glm::dot(w,glm::dmat3(body.m_Shape->InertiaTensor())*w));
        }
        double Potential(const RigidBody3D& body,const PD::BodyState& state) const noexcept
        { return body.GetInverseMass()>0?-glm::dot(glm::dvec3(scene->GetPhysicsSystem()->GetPhysicsWorld()->GetGravity()),glm::dvec3(state.position))/body.GetInverseMass():0; }
        void Gate(unsigned gate,bool pass,double value,double limit,const PD::Event* event=nullptr) noexcept
        {
            ++gateChecks[gate]; if (pass) return;
            ++gateFailures[gate]; ++physicalFailures;
            if (gateFailures[gate]<=2) {
                auto& example=failureExamples[failureExampleCount++]; example.episode=episode; example.tick=tick;
                example.gate=gate; example.value=value; example.limit=limit; if (event) example.event=*event;
                if (postPolicy && event && event->a && event->b)
                    example.identity={event->a->GetIdentity().GetSlot(),event->a->GetIdentity().GetGeneration(),event->b->GetIdentity().GetSlot(),event->b->GetIdentity().GetGeneration()};
            }
            if (candidate) Fail(GateNames[gate]);
        }
        void PostEvent(const PD::Event& e,double pairKinetic,double frictionWork) noexcept
        {
            auto& audit=*postAudit; const auto op=e.operation;
            const auto* world=scene->GetPhysicsSystem()->GetPhysicsWorld();
            const auto& all=world->GetPhysicsBodies();
            bool identity=true;
            for (const auto* body:{e.a,e.b}) identity=identity && body && Index(body)<all.size() &&
                all[Index(body)]==body && world->IsBodyIdentityValid(body->GetIdentity());
            Gate(17,identity,identity?0:1,0,&e);
            if (!identity) return;
            ++audit.sequence;
            if ((episode==2 || episode==9) && tick<=6) {
                if (audit.eventCount==audit.events.size()) { overflow=true; Fail("post-candidate event capacity"); return; }
                auto& row=audit.events[audit.eventCount++]; row.event=e; row.episode=episode; row.tick=tick; row.sequence=audit.sequence;
                row.identity={e.a->GetIdentity().GetSlot(),e.a->GetIdentity().GetGeneration(),e.b->GetIdentity().GetSlot(),e.b->GetIdentity().GetGeneration()};
                row.event.a=row.event.b=nullptr; // No retained body borrower.
            }
            if (audit.responseOpen && (op==PD::Operation::GravityImpulse || op==PD::Operation::Integrate ||
                op==PD::Operation::Position || op==PD::Operation::ToiPosition || op==PD::Operation::ToiNormal ||
                op==PD::Operation::ToiFriction || op==PD::Operation::SleepDecision)) Gate(17,false,int(op),-1,&e);
            if (audit.responseOpen && op==PD::Operation::ContactRemove) {
                const bool known=e.slot>=0 && e.slot<int(audit.cache.size()) && audit.cache[e.slot].present &&
                    audit.cache[e.slot].a==e.a && audit.cache[e.slot].b==e.b &&
                    audit.cache[e.slot].localA==e.localA && audit.cache[e.slot].localB==e.localB;
                Gate(17,known,known?0:1,0,&e);
                if (known) {
                    for (int i=e.slot;i<3;++i) audit.cache[i]=audit.cache[i+1];
                    audit.cache.back()={};
                }
            }
            if (audit.responseOpen && (op==PD::Operation::PairReset || op==PD::Operation::PairEnd)) audit.cache={};
            if (op==PD::Operation::ContactSnapshot && e.reason>=2101 && e.reason<=2103) {
                Gate(17,PD::SameState(e.beforeA,e.afterA) && PD::SameState(e.beforeB,e.afterB),e.reason,e.reason,&e);
                if (e.reason==2101) Gate(17,(e.featureA&1)==0,double(e.featureA&1),0,&e);
                if (e.reason==2103) Gate(17,e.accumulatedAfter[2]==0,e.accumulatedAfter[2],0,&e);
                return;
            }
            const bool cacheOp=op==PD::Operation::ContactSnapshot || op==PD::Operation::WarmStart ||
                op==PD::Operation::BlockNormal || op==PD::Operation::NormalSliding;
            if (!cacheOp) return;
            bool linked=audit.responseOpen && e.slot>=0 && e.slot<int(audit.cache.size());
            if (!linked) { Gate(17,false,e.slot,0,&e); return; }
            auto& cache=audit.cache[e.slot];
            linked=cache.present && cache.a==e.a && cache.b==e.b;
            if (op==PD::Operation::ContactSnapshot) {
                linked=linked && !cache.projected && PD::SameState(e.beforeA,e.afterA) && PD::SameState(e.beforeB,e.afterB);
                cache.lambda=e.accumulatedAfter; cache.projected=true;
                const double limit=double(e.a->m_Friction)*e.b->m_Friction*cache.lambda[0];
                Gate(1,std::hypot(cache.lambda[1],cache.lambda[2])<=limit+1e-5*std::max(1.,std::abs(cache.lambda[0])),
                    std::hypot(cache.lambda[1],cache.lambda[2])-limit,1e-5*std::max(1.,std::abs(cache.lambda[0])),&e);
            } else {
                linked=linked && cache.projected && cache.localA==e.localA && cache.localB==e.localB &&
                    cache.lambda[0]==e.accumulatedBefore[0];
                if (op!=PD::Operation::BlockNormal) linked=linked && cache.lambda==e.accumulatedBefore;
                if (op==PD::Operation::WarmStart) {
                    linked=linked && !cache.warm && e.constraintToken!=0 && e.accumulatedBefore==e.accumulatedAfter;
                    cache.token=e.constraintToken; cache.warm=true;
                } else linked=linked && cache.warm && (op==PD::Operation::BlockNormal || cache.token==e.constraintToken);
                if (op==PD::Operation::BlockNormal) {
                    cache.reduced=cache.reduced || e.accumulatedAfter[0]<e.accumulatedBefore[0];
                    cache.lambda[0]=e.accumulatedAfter[0];
                    // Tangents are absent in this event: retain the actual cache.
                    const double excess=std::hypot(cache.lambda[1],cache.lambda[2])-double(e.a->m_Friction)*e.b->m_Friction*cache.lambda[0];
                    audit.current.maxIntermediateCone=std::max(audit.current.maxIntermediateCone,excess);
                } else {
                    for (int i=0;i<12;++i) {
                        double expected=0;
                        for (int row=0;row<3;++row) {
                            const auto& j=row==0?e.normalRow:(row==1?e.tangentRowU:e.tangentRowV);
                            expected+=j[i]*(op==PD::Operation::WarmStart?e.accumulatedAfter[row]:e.accumulatedAfter[row]-e.accumulatedBefore[row]);
                        }
                        const Vec3f& actual=i<3?e.linearImpulseA:(i<6?e.angularImpulseA:(i<9?e.linearImpulseB:e.angularImpulseB));
                        const double error=std::abs(double(actual[i%3])-expected),limit=1e-5*std::max(1.,std::abs(expected));
                        Gate(4,e.appliedImpulsesKnown && error<=limit,error,limit,&e);
                    }
                }
            }
            Gate(17,linked,linked?0:1,0,&e);
            if (op!=PD::Operation::NormalSliding) return;
            ++audit.current.sliding; ++audit.sliding;
            const double tau=1e-5*std::max(1.,pairKinetic),mu=double(e.a->m_Friction)*e.b->m_Friction;
            const double radius=mu*e.accumulatedAfter[0];
            const double t0=e.accumulatedBefore[1],t1=e.accumulatedBefore[2];
            const double u=e.accumulatedAfter[1],v=e.accumulatedAfter[2];
            const bool feasible=std::hypot(t0,t1)<=radius+1e-5*std::max(1.,std::abs(e.accumulatedAfter[0]));
            audit.current.maxIncrement=std::max(audit.current.maxIncrement,frictionWork);
            if (frictionWork>tau) { ++audit.legacyPositive; ++audit.current.legacyPositive; }
            if (feasible) Gate(2,frictionWork<=tau,frictionWork,tau,&e);
            else {
                ++audit.current.infeasible;
                const bool explained=linked && cache.warm && (cache.reduced || e.accumulatedAfter[0]<e.accumulatedBefore[0]);
                Gate(17,explained,explained?0:1,0,&e);
                if (explained) { ++audit.explained; ++audit.current.explained; }
            }
            using I=AuditInterval;
            I h00{},h01{},h11{},g0{},g1{};
            const I dn=I(e.accumulatedAfter[0])-I(e.accumulatedBefore[0]);
            for (int role=0;role<2;++role) {
                const auto* body=role?e.b:e.a; if (body->GetInverseMass()<=0) continue;
                const auto& state=role?e.beforeB:e.beforeA;
                const RigidBody3D copy=*body;
                const auto inverse=copy.GetInverseInertiaTensorWorldSpace();
                I q0[6]{};
                for (int i=0;i<6;++i) {
                    q0[i]=I(i<3?state.linear[i]:state.angular[i-3]);
                    for (int j=0;j<6;++j) {
                        I w{};
                        if (i<3 && j==i) w=I(body->GetInverseMass());
                        else if (i>=3 && j>=3) w=I(inverse[j-3][i-3]);
                        const int column=role*6+j;
                        q0[i]=q0[i]+w*(I(e.normalRow[column])*dn-I(e.tangentRowU[column])*I(t0)-I(e.tangentRowV[column])*I(t1));
                        const I ui(e.tangentRowU[role*6+i]),vi(e.tangentRowV[role*6+i]);
                        h00=h00+ui*w*I(e.tangentRowU[column]);
                        // Only the symmetric part contributes to x^T K x.
                        h01=h01+(ui*w*I(e.tangentRowV[column])+vi*w*I(e.tangentRowU[column]))*I(.5);
                        h11=h11+vi*w*I(e.tangentRowV[column]);
                    }
                    g0=g0+I(e.tangentRowU[role*6+i])*q0[i]; g1=g1+I(e.tangentRowV[role*6+i])*q0[i];
                }
            }
            auto f=[&](double x,double y) { return g0*I(x)+g1*I(y)+(h00*I::Square(I(x))+I(2)*h01*I(x)*I(y)+h11*I::Square(I(y)))*I(.5); };
            const auto actual=f(u,v),det=h00*h11-I::Square(h01);
            // Project only the diagnostic candidate inward. Feasibility is then
            // certified with outward arithmetic, not presumed from a float clamp.
            double x=u,y=v;
            if (radius==0) x=y=0;
            else if (radius>0 && std::isfinite(radius)) {
                const double lengthUpper=I::Norm(I(x),I(y)).hi;
                if (std::isfinite(lengthUpper) && lengthUpper>radius) {
                    // Construct against the same outward bound used below.
                    // Frozen binary32 tangents and mu=.25 need at most eight
                    // inward ULP refinements; invalid inputs still fail closed.
                    const double scale=std::nextafter(radius/lengthUpper,0.);
                    x=std::nextafter(x*scale,0.); y=std::nextafter(y*scale,0.);
                    for (unsigned refinement=0;refinement<8 && I::Norm(I(x),I(y)).hi>radius;++refinement) {
                        x=std::nextafter(x,0.); y=std::nextafter(y,0.);
                    }
                }
            }
            const auto feasibleLength=I::Norm(I(x),I(y));
            // Zero is exact and avoids a subnormal outward square-root artifact.
            const bool disk=(x==0 && y==0) || feasibleLength.hi<=radius;
            const auto projected=f(x,y),gx=g0+h00*I(x)+h01*I(y),gy=g1+h01*I(x)+h11*I(y);
            const auto gap=gx*I(x)+gy*I(y)+I(radius)*I::Norm(gx,gy);
            const double upper=I::Up(std::max(0.,gap.hi)+I::AbsUpper(actual-projected));
            const bool valid=h00.Finite() && h01.Finite() && h11.Finite() && g0.Finite() && g1.Finite() &&
                actual.Finite() && gap.Finite() && det.lo>=0 && h00.lo>=0 && h11.lo>=0 &&
                radius>=0 && mu>=0 && disk && std::isfinite(upper);
            Gate(20,valid,valid?0:1,0,&e);
            Gate(15,valid && actual.hi<=tau,actual.hi,tau,&e); Gate(16,valid && upper<=tau,upper,tau,&e);
            audit.current.maxF=std::max(audit.current.maxF,actual.hi); audit.current.maxGap=std::max(audit.current.maxGap,upper);
            cache.lambda=e.accumulatedAfter;
        }
        void ConditionalEvent(const PD::Event& e) noexcept
        {
            ++currentTick.events; ++eventCount;
            const auto op=e.operation;
            if (op==PD::Operation::ToiNormal) {
                toiNormalImpulse=glm::dot(glm::dvec3(e.linearImpulseB),glm::dvec3(e.normal));
                toiBodyA=e.a; toiBodyB=e.b;
                currentTick.minNormal=std::min(currentTick.minNormal,toiNormalImpulse);
                Gate(0,toiNormalImpulse>=-1e-6,toiNormalImpulse,-1e-6,&e);
            }
            if (op==PD::Operation::ToiFriction) {
                if (toiBodyA!=e.a || toiBodyB!=e.b) { Fail("conditional TOI response pairing missing"); return; }
                const double excess=glm::length(glm::dvec3(e.linearImpulseB))-.25*toiNormalImpulse;
                const double allowance=1e-5*std::max(1.,std::abs(toiNormalImpulse));
                currentTick.maxCoulombExcess=std::max(currentTick.maxCoulombExcess,excess);
                Gate(1,excess<=allowance,excess,allowance,&e);
            }
            const bool accumulated=op==PD::Operation::WarmStart || op==PD::Operation::NormalSliding || op==PD::Operation::BlockNormal;
            if (accumulated) {
                const double n=e.accumulatedAfter[0],t=std::hypot(e.accumulatedAfter[1],e.accumulatedAfter[2]);
                currentTick.minNormal=std::min(currentTick.minNormal,n);
                const double excess=t-.25*n,allowance=1e-5*std::max(1.,std::abs(n));
                currentTick.maxCoulombExcess=std::max(currentTick.maxCoulombExcess,excess);
                Gate(0,n>=-1e-6,n,-1e-6,&e);
                if (!postPolicy || op!=PD::Operation::BlockNormal) Gate(1,excess<=allowance,excess,allowance,&e);
            }
            double frictionWork=0,pairKinetic=0;
            for (unsigned role=0;role<2;++role) {
                const auto* body=role?e.b:e.a;
                if (!body || (role && body==e.a) || body->GetInverseMass()<=0) continue;
                const auto& before=role?e.beforeB:e.beforeA; const auto& after=role?e.afterB:e.afterA;
                const double kb=Kinetic(*body,before),ka=Kinetic(*body,after),du=Potential(*body,after)-Potential(*body,before);
                pairKinetic+=kb;
                if (op==PD::Operation::GravityImpulse) {
                    const auto dv=glm::dvec3(scene->GetPhysicsSystem()->GetPhysicsWorld()->GetGravity())*e.duration;
                    currentTick.gravityWork+=(glm::dot(glm::dvec3(before.linear),dv)+.5*glm::dot(dv,dv))/body->GetInverseMass();
                } else if (op==PD::Operation::Integrate) { currentTick.integrationPotential+=du; currentTick.integrationKinetic+=ka-kb; }
                else if (op==PD::Operation::Position || op==PD::Operation::ToiPosition) {
                    currentTick.positionPotential+=du; currentTick.upwardCorrection+=std::max(0.,double(after.position.y-before.position.y));
                } else if (op==PD::Operation::SleepDecision) currentTick.sleepWork+=ka-kb;
                if (e.appliedImpulsesKnown) {
                    const glm::dvec3 j(role?e.linearImpulseB:e.linearImpulseA),l(role?e.angularImpulseB:e.angularImpulseA);
                    const RigidBody3D observed=*body; // Cache-populating getters operate on the observer copy.
                    const auto inverse=glm::dmat3(observed.GetInverseInertiaTensorWorldSpace());
                    const auto dv=double(body->GetInverseMass())*j,dw=inverse*l;
                    const double expected=glm::dot(glm::dvec3(before.linear),j)+glm::dot(glm::dvec3(before.angular),l)+.5*(glm::dot(j,dv)+glm::dot(l,dw));
                    currentTick.responseWork+=expected; currentTick.responseMeasured+=ka-kb;
                    const auto actualDv=glm::dvec3(after.linear)-glm::dvec3(before.linear);
                    const auto actualDw=glm::dvec3(after.angular)-glm::dvec3(before.angular);
                    for (int component=0;component<3;++component) for (unsigned angular=0;angular<2;++angular) {
                        const double expectedDelta=angular?dw[component]:dv[component];
                        const double error=std::abs((angular?actualDw[component]:actualDv[component])-expectedDelta);
                        const double allowance=1e-5*std::max(1.,std::abs(expectedDelta));
                        currentTick.maxImpulseError=std::max(currentTick.maxImpulseError,error);
                        Gate(4,error<=allowance,error,allowance,&e);
                    }
                    if (op==PD::Operation::Spin || op==PD::Operation::Rolling) {
                        const float coefficient=op==PD::Operation::Spin?body->GetSpinResistanceLength():body->GetRollingResistanceLength();
                        if (coefficient==0) Gate(3,glm::length2(l)==0,glm::length(l),0,&e);
                        frictionWork+=expected;
                    }
                    if (op==PD::Operation::ToiFriction) frictionWork+=expected;
                    if (op==PD::Operation::NormalSliding) {
                        const int base=role?6:0; const double dn=e.accumulatedAfter[0]-e.accumulatedBefore[0];
                        const double dt0=e.accumulatedAfter[1]-e.accumulatedBefore[1],dt1=e.accumulatedAfter[2]-e.accumulatedBefore[2];
                        glm::dvec3 jn{},ln{},jt{},lt{};
                        for (int k=0;k<3;++k) {
                            jn[k]=e.normalRow[base+k]*dn; ln[k]=e.normalRow[base+3+k]*dn;
                            jt[k]=e.tangentRowU[base+k]*dt0+e.tangentRowV[base+k]*dt1;
                            lt[k]=e.tangentRowU[base+3+k]*dt0+e.tangentRowV[base+3+k]*dt1;
                        }
                        frictionWork+=glm::dot(glm::dvec3(before.linear)+double(body->GetInverseMass())*jn,jt)+
                            glm::dot(glm::dvec3(before.angular)+inverse*ln,lt)+.5*(double(body->GetInverseMass())*glm::dot(jt,jt)+glm::dot(lt,inverse*lt));
                    }
                    if (episode>=3 && episode<=5 && before.linear.y<-.1f && after.linear.y>before.linear.y+.1f) {
                        ++impactResponses; const double outgoing=episode==5?.375:0.;
                        Gate(14,std::abs(after.linear.y-outgoing)<=.01,std::abs(after.linear.y-outgoing),.01,&e);
                        if (episode==5) Gate(14,std::abs(after.linear.y/1.5-.25)<=.01,std::abs(after.linear.y/1.5-.25),.01,&e);
                    }
                    if (episode==6 && glm::length2(dw)>0) ++impactResponses;
                }
            }
            if (op==PD::Operation::NormalSliding || op==PD::Operation::ToiFriction || op==PD::Operation::Spin || op==PD::Operation::Rolling) {
                const double allowance=1e-5*std::max(1.,pairKinetic);
                currentTick.maxFrictionWork=std::max(currentTick.maxFrictionWork,frictionWork);
                if (!postPolicy || op!=PD::Operation::NormalSliding) Gate(2,frictionWork<=allowance,frictionWork,allowance,&e);
            }
            if (postPolicy) PostEvent(e,pairKinetic,frictionWork);
        }
        void FlushConditionalTick()
        {
            if (!conditional || !tickPending) return;
            const auto after=GetPhysicsProfileSnapshot(); const auto& r=pendingTick;
            if (after.stepCount-profileBefore.stepCount!=1 || after.physicsWorldTimeNs<=profileBefore.physicsWorldTimeNs) Fail("conditional completed Physics profile interval missing");
            else { ++profiledTicks; physicsWorldNs+=after.physicsWorldTimeNs-profileBefore.physicsWorldTimeNs; }
            if (r.episode==0 || r.episode==8 || r.episode==10) {
                cpuRows<<r.episode<<','<<r.tick;
                for (const auto& [name,field]:ProfileFields) cpuRows<<','<<(field==&PhysicsProfileSnapshot::candidatePairCount?after.*field:after.*field-profileBefore.*field);
                cpuRows<<','<<after.manifoldCount<<','<<after.manifoldContactCount<<','<<after.solverConstraintCount<<','<<r.observerNs<<'\n';
            }
            qualificationRows<<r.episode<<','<<r.tick<<','<<r.bodies<<','<<r.contacts<<','<<r.active<<','<<r.sleeping<<','<<r.quiet<<','<<r.wakes<<','<<r.sleeps<<','<<r.eligible<<','<<r.missed;
            for (double value:{r.energy,r.kinetic,r.potential,r.penetration,r.maxLinear,r.maxAngular,r.y,r.vy,r.upwardCorrection,
                r.gravityWork,r.responseWork,r.responseMeasured,r.integrationPotential,r.integrationKinetic,r.positionPotential,r.sleepWork,r.residual,
                r.minNormal,r.maxCoulombExcess,r.maxFrictionWork,r.maxImpulseError}) qualificationRows<<','<<value;
            qualificationRows<<','<<r.events<<','<<r.observerNs<<','<<r.capacityBytes<<','<<r.capacityGrowth
                <<','<<r.boundaryNs<<','<<r.eventSinkNs<<','<<r.eventObservationNs<<'\n';
            for (;failureExampleWritten<failureExampleCount;++failureExampleWritten) {
                const auto& f=failureExamples[failureExampleWritten]; const auto& e=f.event;
                events<<f.episode<<','<<f.tick<<','<<failureExampleWritten<<','<<int(e.operation)<<','<<f.gate<<','<<GateNames[f.gate]<<','<<f.value<<','<<f.limit;
                for (const auto& state:{e.beforeA,e.beforeB,e.afterA,e.afterB}) {
                    CsvVector(events,state.position); events<<','<<state.orientation.w<<','<<state.orientation.x<<','<<state.orientation.y<<','<<state.orientation.z;
                    CsvVector(events,state.linear); CsvVector(events,state.angular);
                }
                for (const auto& v:{e.normal,e.anchorA,e.anchorB,e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB}) CsvVector(events,v);
                for (double value:e.accumulatedBefore) events<<','<<value;
                for (double value:e.accumulatedAfter) events<<','<<value;
                events<<','<<e.restitution<<','<<e.closingSpeed<<','<<e.penetration<<','<<e.duration;
                if (postPolicy) {
                    for (auto value:f.identity) events<<','<<value;
                    events<<','<<e.slot<<','<<e.constraintToken<<','<<e.featureA<<','<<e.featureB<<','<<e.appliedImpulsesKnown;
                    for (const auto& v:{e.localA,e.localB,e.tangentU,e.tangentV}) CsvVector(events,v);
                    for (const auto& row:{e.normalRow,e.tangentRowU,e.tangentRowV}) for (auto value:row) events<<','<<value;
                }
                events<<'\n';
            }
            tickPending=false;
            if (!cpuRows || !qualificationRows || !events) Fail("conditional compact evidence write failed");
        }
        void ConditionalNarrowphase(const PhysicsWorld& world,ManifoldCollector& manifolds) noexcept
        {
            const auto& all=world.GetPhysicsBodies(); if (all.empty()) return;
            const unsigned groundIndex=episode==0?4u:0u;
            std::array<bool,185> admitted{};
            for (auto& manifold:manifolds.m_Manifolds) for (int j=0;j<manifold.GetNumContacts();++j) {
                const auto c=manifold.GetContact(j); const auto a=Index(c.m_BodyA),b=Index(c.m_BodyB);
                if (a==groundIndex && b<admitted.size()) admitted[b]=true;
                if (b==groundIndex && a<admitted.size()) admitted[a]=true;
            }
            for (unsigned i=1;i<all.size();++i) {
                const auto& b=*all[i]; if (b.IsSleeping() || b.GetInverseMass()<=0 || b.m_Shape->GetShapeType()!=ShapeType::Sphere) continue;
                const float radius=static_cast<const ShapeSphere*>(b.m_Shape)->GetRadius();
                if (std::abs(b.m_Position.x)>50-radius || std::abs(b.m_Position.z)>50-radius) continue;
                const double penetration=.5+radius-b.m_Position.y;
                if (penetration>1e-4) { ++currentTick.eligible; if (!admitted[i]) ++currentTick.missed; Gate(13,admitted[i],penetration,1e-4); }
            }
        }
        void ConditionalEndTick(const PhysicsWorld& world,ManifoldCollector& manifolds,std::size_t bytes)
        {
            auto& r=currentTick; const auto& all=world.GetPhysicsBodies(); r.bodies=unsigned(all.size());
            if (all.size()!=inputs.size() || previous.size()!=all.size()) { Fail("conditional body membership mismatch"); return; }
            for (unsigned i=0;i<all.size();++i) {
                const auto& b=*all[i]; const auto state=PD::Capture(b);
                if (!b.HasFiniteState()) { Fail("conditional nonfinite body state"); return; }
                if (b.GetInverseMass()>0) {
                    const double speed=glm::length(glm::dvec3(state.linear)),spin=glm::length(glm::dvec3(state.angular));
                    r.active+=!state.sleeping; r.sleeping+=state.sleeping;
                    const auto& thresholds=b.GetSleepSettings();
                    r.quiet+=state.sleeping || (speed<=thresholds.linearSpeedThreshold && spin<=thresholds.angularSpeedThreshold);
                    r.wakes+=previous[i].sleeping && !state.sleeping; r.sleeps+=!previous[i].sleeping && state.sleeping;
                    r.kinetic+=Kinetic(b,state); r.potential+=Potential(b,state);
                    r.maxLinear=std::max(r.maxLinear,speed); r.maxAngular=std::max(r.maxAngular,spin);
                    double bottom=state.position.y;
                    if (b.m_Shape->GetShapeType()==ShapeType::Sphere) bottom-=static_cast<const ShapeSphere*>(b.m_Shape)->GetRadius();
                    else {
                        const auto bounds=b.m_Shape->GetBounds(); bottom=std::numeric_limits<double>::max();
                        for (int x=0;x<2;++x) for (int y=0;y<2;++y) for (int z=0;z<2;++z) {
                            const Vec3f p(x?bounds.maxs.x:bounds.mins.x,y?bounds.maxs.y:bounds.mins.y,z?bounds.maxs.z:bounds.mins.z);
                            bottom=std::min(bottom,double((state.position+state.orientation*p).y));
                        }
                    }
                    if (std::abs(state.position.x)<49 && std::abs(state.position.z)<49) r.penetration=std::max(r.penetration,.5-bottom);
                    if (i==1) { r.y=state.position.y; r.vy=state.linear.y; }
                }
                previous[i]=state;
            }
            for (auto& manifold:manifolds.m_Manifolds) for (int j=0;j<manifold.GetNumContacts();++j) {
                const auto c=manifold.GetContact(j);
                if (!world.IsBodyIdentityValid(c.m_BodyA->GetIdentity()) || !world.IsBodyIdentityValid(c.m_BodyB->GetIdentity())) { Fail("conditional stale contact identity"); return; }
                const RigidBody3D observedA=*c.m_BodyA,observedB=*c.m_BodyB;
                const auto a=observedA.BodySpaceToWorldSpace(c.ptOnA_LocalSpace),b=observedB.BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
                r.penetration=std::max(r.penetration,-double(glm::dot(a-b,c.normal))); ++r.contacts;
            }
            debugLineCapacity=std::max(std::size_t{32768},all.size()*63+std::size_t(r.contacts)*50);
            r.energy=r.kinetic+r.potential;
            if (!std::isfinite(r.energy)) { Fail("conditional nonfinite energy"); return; }
            r.residual=r.energy-previousEnergy-r.gravityWork-r.responseWork-r.integrationPotential-r.positionPotential-r.sleepWork;
            if (!LightInterval() && (episode==1 || episode==7 || episode==9)) {
                const double allowance=1e-4*std::max({1.,previousKinetic,std::abs(r.integrationPotential+r.positionPotential)});
                Gate(5,std::abs(r.residual)<=allowance,std::abs(r.residual),allowance);
            }
            if (episode==0) Gate(12,r.energy<=30391.2,r.energy,30391.2);
            if (episode==9) {
                const auto work=GetPhysicsProfileSnapshot();
                const auto secondary=(work.spinResistanceImpulseCount-profileBefore.spinResistanceImpulseCount)+
                    (work.rollingResistanceImpulseCount-profileBefore.rollingResistanceImpulseCount);
                Gate(3,secondary==0,double(secondary),0);
            }
            if (episode==1) {
                if (tick>60) Gate(9,r.upwardCorrection<=.005,r.upwardCorrection,.005);
                if (tick==181) { restWindowEnergy=r.energy; restMinY=restMaxY=r.y; }
                if (tick>=181) {
                    restMinY=std::min(restMinY,r.y); restMaxY=std::max(restMaxY,r.y);
                    Gate(6,r.penetration<=.035,r.penetration,.035); Gate(7,restMaxY-restMinY<=.005,restMaxY-restMinY,.005);
                    Gate(8,std::abs(r.vy)<=.01,std::abs(r.vy),.01); Gate(12,r.energy-restWindowEnergy<=.001,r.energy-restWindowEnergy,.001);
                }
            }
            const unsigned quietTicks=episode==8?120:60;
            if ((episode==1 || episode==2 || episode==8 || episode==10) && tick>TickLimit()-quietTicks) {
                Gate(10,r.quiet==r.active+r.sleeping,r.active+r.sleeping-r.quiet,0);
                Gate(6,r.penetration<=.035,r.penetration,.035); Gate(11,r.wakes==0,r.wakes,0);
            }
            if (tick==TickLimit() && episode>=3 && episode<=6) Gate(14,impactResponses>0,impactResponses,1);
            previousEnergy=r.energy; previousKinetic=r.kinetic;
            if (bytes>lastStorage) storageGrowth+=bytes-lastStorage; lastStorage=bytes;
            r.capacityBytes=bytes; r.capacityGrowth=storageGrowth;
            pendingTick=r; tickPending=true;
            if (failed || tick==TickLimit()) { episodeDone=true; scene->SetPaused(true); }
        }

        unsigned TickLimit() const noexcept { if (phase21) return phase21->cases[episode].ticks; return conditional?ConditionalDurations[episode]:Durations[episode]; }
        const char* EpisodeName() const noexcept { if (phase21) return phase21->cases[episode].name; return conditional?ConditionalNames[episode]:Names[episode]; }
        bool LightInterval() const noexcept { return conditional && (episode==0 || episode==8 || episode==10); }
        double StepSeconds() const noexcept { if (phase21) return 1.0/phase21->cases[episode].hz; return conditional && episode!=0?1.0/60.0:1.0/120.0; }
        void RetireOwnedScene() noexcept
        {
            if (!sixtyHzScene) return;
            PD::observer=nullptr; PD::queryObserver=nullptr;
            sixtyHzScene->OnRuntimeStop(); // Retire body borrowers before shapes/resources.
            entities.clear(); sixtyHzScene.reset(); ownedShapes.clear();
            scene=applicationScene;
        }

        explicit Session(const RestingContactDiagnostic& c):config(c) {}
        void Fail(const char* reason) noexcept
        {
            if (!failed) {
                failed=true; failure=reason;
                if (!postPolicy) Log::GetCoreLogger()->error("PHASE19 diagnostic: {}",reason);
            }
        }
        bool Open()
        {
            // Own CRT environment values while request parsing runs; never retain a borrowed getenv pointer.
            const auto readRequest = [this](const char* name) {
                char* value=nullptr; std::size_t size{};
                const auto error=_dupenv_s(&value,&size,name);
                if (error) Fail("diagnostic request environment read failed");
                return std::unique_ptr<char,decltype(&std::free)>(value,&std::free);
            };
            const auto phase21Request=readRequest("GENGINE_P21_REQUEST");
            if (failed) return false;
            if (phase21Request) return OpenPhase21(phase21Request.get());
            const auto phase20Request=readRequest("GENGINE_P20_CONDITIONAL_FOCUSED_REQUEST");
            if (failed) return false;
            if (const char* request=phase20Request.get()) {
                const auto expected=(std::filesystem::path(config.outputDirectory).parent_path()/"focused-request.txt").lexically_normal();
                const auto path=std::filesystem::path(request);
                std::ifstream in(path); std::string magic,role,latticeHash,replayHash,end;
                unsigned cases{},calls{},direct{},ca{},projection{},episodes{},ticks{},seconds{},wall{};
                if (!path.is_absolute() || path.lexically_normal()!=expected ||
                    !(in>>magic>>role>>latticeHash>>replayHash>>cases>>calls>>direct>>ca>>projection>>episodes>>ticks>>seconds>>wall>>end) ||
                    (magic!="P20_CONDITIONAL_FOCUSED_V1" && magic!="P20_POST_CANDIDATE_FOCUSED_V1") || (role!="control" && role!="candidate") ||
                    latticeHash!="79eb5a76b06726b77333fff93ddb47999671df025ef1c75322494b8ac62116c6" ||
                    replayHash!="0285e8dd4303d7aee1c4ebc41a504734faccf4b1b2309faf3bfb3a854b719ee6" ||
                    cases!=25 || calls!=50 || direct!=16 || ca!=12 || projection!=4 || episodes!=11 ||
                    ticks!=4980 || seconds!=68 || wall!=120 || end!="END") { Fail("invalid conditional focused request"); return false; }
                in>>std::ws; if (!in.eof() || config.observation!=Observation::Observed) { Fail("conditional request trailing bytes or observer mode"); return false; }
                conditional=true; candidate=role=="candidate"; postPolicy=magic=="P20_POST_CANDIDATE_FOCUSED_V1";
                if (postPolicy) failure.reserve(256); // Failure recording cannot allocate in a Physics callback.
                if (postPolicy && !candidate) { Fail("post-candidate policy requires candidate role"); return false; }
            }
            if (!LoadLattice(config.latticePath,lattice)) { Fail("invalid frozen lattice input"); return false; }
            const auto dir=std::filesystem::path(config.outputDirectory);
            // The runner creates an empty contained output directory. Never overwrite evidence.
            const char* names[]{"bodies.bin","stages.bin","contacts.csv","timing.csv","metrics.csv",
                "events.csv","frames.csv","initial.csv","islands.csv","result.json"};
            std::error_code ec;
            if (!std::filesystem::is_directory(dir,ec) || ec) return false;
            for (auto name:names) if (std::filesystem::exists(dir/name,ec) || ec) return false;
            bodies.open(dir/"bodies.bin",std::ios::binary); stages.open(dir/"stages.bin",std::ios::binary);
            contacts.open(dir/"contacts.csv"); timing.open(dir/"timing.csv"); metrics.open(dir/"metrics.csv");
            events.open(dir/"events.csv"); frames.open(dir/"frames.csv"); initial.open(dir/"initial.csv");
            islandRows.open(dir/"islands.csv");
            for (auto* out:{&contacts,&timing,&metrics,&events,&frames,&initial}) *out<<std::setprecision(17);
            contacts<<"episode,tick,a,b,feature_a,feature_b,ax,ay,az,bx,by,bz,nx,ny,nz,depth,lax,lay,laz,lbx,lby,lbz\n";
            islandRows<<"episode,tick,island,role,a,b\n";
            timing<<"episode,tick,stage,interval_ns,boundary_ns,detailed_ns,storage_bytes\n";
            metrics<<"episode,tick,bodies,contacts,islands,active,sleeping,wakes,sleeps,max_linear,max_angular,drift,energy,penetration,capacity_growth_bytes,finite\n";
            events<<(conditional?"episode,tick,example,operation,gate,gate_name,value,limit":"episode,tick,sequence,operation,a,b,nx,ny,nz,ax,ay,az,bx,by,bz,jx,jy,jz,tx,ty,tz,l0_before,l1_before,l2_before,l0_after,l1_after,l2_after,restitution,closing_speed,penetration");
            for (auto prefix:{"a_before","b_before","a_after","b_after"}) {
                if (conditional) for (auto field:{"px","py","pz","qw","qx","qy","qz","vx","vy","vz","wx","wy","wz"}) events<<','<<prefix<<'_'<<field;
                else for (auto field:{"px","py","pz","vx","vy","vz","wx","wy","wz"}) events<<','<<prefix<<'_'<<field;
            }
            if (conditional) {
                for (auto prefix:{"normal","anchor_a","anchor_b","linear_impulse_a","linear_impulse_b","angular_impulse_a","angular_impulse_b"})
                    for (char axis:std::string("xyz")) events<<','<<prefix<<'_'<<axis;
                events<<",l0_before,l1_before,l2_before,l0_after,l1_after,l2_after,restitution,closing_speed,penetration,duration";
                if (postPolicy) {
                    events<<",a_slot,a_generation,b_slot,b_generation,slot,token,feature_a,feature_b,impulses_known";
                    for (auto prefix:{"local_a","local_b","tangent_u","tangent_v"}) for (char axis:std::string("xyz")) events<<','<<prefix<<'_'<<axis;
                    for (auto prefix:{"jn","ju","jv"}) for (unsigned i=0;i<12;++i) events<<','<<prefix<<'_'<<i;
                }
            }
            events<<'\n';
            frames<<"episode,tick,elapsed,pending,discarded,total_steps,steps_this_frame,interpolation\n";
            initial<<"episode,index,slot,generation,shape,type,inverse_mass,elasticity,friction,layer,mask,radius,px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,spin_length,rolling_length,linear_sleep,angular_sleep,dwell,ixx,iyy,izz\n";
            sink={this,config.observation==Observation::Observed,&Select,&Event,&Boundary};
            if (postPolicy) {
                postAudit=std::make_unique<PostCandidateAudit>(); // Before any measured Physics interval.
                for (auto name:{"post-events.csv","post-stages.csv","post-ticks.csv"})
                    if (std::filesystem::exists(dir/name,ec) || ec) { Fail("post-candidate evidence destination exists"); return false; }
                postEvents.open(dir/"post-events.csv"); postStages.open(dir/"post-stages.csv"); postTicks.open(dir/"post-ticks.csv");
                for (auto* stream:{&postEvents,&postStages,&postTicks}) *stream<<std::setprecision(17);
                postEvents<<"episode,tick,sequence,operation,a_slot,a_generation,b_slot,b_generation,slot,previous_slot,reason,token,feature_a,feature_b,impulses_known";
                for (auto prefix:{"a_before","b_before","a_after","b_after"}) PostStateHeader(postEvents,prefix);
                for (auto prefix:{"normal","anchor_a","anchor_b","local_a","local_b","tangent_u","tangent_v","linear_a","linear_b","angular_a","angular_b"})
                    for (char axis:std::string("xyz")) postEvents<<','<<prefix<<'_'<<axis;
                for (auto prefix:{"before","after"}) for (unsigned i=0;i<3;++i) postEvents<<','<<prefix<<'_'<<i;
                for (auto prefix:{"jn","ju","jv"}) for (unsigned i=0;i<12;++i) postEvents<<','<<prefix<<'_'<<i;
                postEvents<<",restitution,closing_speed,penetration,time,duration,bias\n";
                postStages<<"episode,tick,stage,slot,generation"; PostStateHeader(postStages,"state"); postStages<<'\n';
                postTicks<<"episode,tick,sliding,legacy_positive,infeasible_old,explained,max_f_upper,max_gap_upper,max_increment,max_intermediate_cone,before_k,after_k,signed_work,balance\n";
                if (!postEvents || !postStages || !postTicks) { Fail("post-candidate evidence open failed"); return false; }
                sink.violation=[](void* context) noexcept { static_cast<Session*>(context)->Fail("post-candidate observer altered physical state"); };
            }
            if (conditional) {
                for (auto name:{"focused-queries.csv","focused-inputs.csv","focused-helpers.jsonl","focused-cpu.csv","focused-qualification.csv","focused-gates.csv"})
                    if (std::filesystem::exists(dir/name,ec) || ec) { Fail("conditional evidence destination exists"); return false; }
                queryRows.open(dir/"focused-queries.csv"); queryInputs.open(dir/"focused-inputs.csv");
                helperRows.open(dir/"focused-helpers.jsonl"); cpuRows.open(dir/"focused-cpu.csv"); qualificationRows.open(dir/"focused-qualification.csv");
                for (auto* out:{&queryRows,&queryInputs,&helperRows,&cpuRows,&qualificationRows}) *out<<std::setprecision(17);
                queryRows<<"case,mode,hit,geometry_ok,pointers_ok,unchanged,elapsed_ns,observer_ns";
                for (unsigned i=0;i<19;++i) queryRows<<",contact_bits_"<<i; queryRows<<'\n';
                queryInputs<<"case,body,shape,type,inverse_mass_bits,elasticity_bits,friction_bits,layer,mask,spin_bits,rolling_bits,linear_sleep_bits,angular_sleep_bits,dwell_bits,sleeping,inactive_bits,radius_bits";
                for (unsigned i=0;i<13;++i) queryInputs<<",pose_bits_"<<i;
                for (const char* prefix:{"com","bounds_min","bounds_max"}) for (char axis:std::string("xyz")) queryInputs<<','<<prefix<<'_'<<axis<<"_bits";
                for (unsigned i=0;i<9;++i) queryInputs<<",inertia_bits_"<<i; queryInputs<<'\n';
                cpuRows<<"episode,tick"; for (const auto& [name,field]:ProfileFields) cpuRows<<','<<name;
                cpuRows<<",manifoldCount,manifoldContactCount,solverConstraintCount,observer_ns\n";
                qualificationRows<<"episode,tick,bodies,contacts,active,sleeping,quiet,wakes,sleeps,eligible_awake_ground,missed_awake_ground,energy,kinetic,potential,penetration,max_linear,max_angular,y,vy,upward_correction,gravity_work,response_work,response_measured,integration_potential,integration_kinetic,position_potential,sleep_work,energy_residual,min_normal,max_coulomb_excess,max_friction_work,max_impulse_error,events,observer_ns,capacity_bytes,capacity_growth,boundary_ns,event_sink_ns,event_observation_ns\n";
                if (!queryRows || !queryInputs || !helperRows || !cpuRows || !qualificationRows) { Fail("conditional evidence open failed"); return false; }
            }
            return bodies && stages && contacts && timing && metrics && events && frames && initial && islandRows;
        }
        unsigned Index(const RigidBody3D* b) const noexcept
        {
            if (!b) return std::numeric_limits<unsigned>::max();
            const auto slot=b->GetIdentity().GetSlot();
            // Every episode is a freshly owned world; verify this mapping at import.
            return slot>0 && slot<=inputs.size() ? static_cast<unsigned>(slot-1) : std::numeric_limits<unsigned>::max();
        }
        bool Selected(unsigned index) const noexcept
        { return episode!=0 || index==10 || index==12 || index==22 || index==24; }
        static bool Select(void* context,const RigidBody3D* a,const RigidBody3D* b) noexcept
        {
            const auto& self=*static_cast<Session*>(context);
            return self.Selected(self.Index(a)) || self.Selected(self.Index(b));
        }
        static void Event(void* context,const PD::Event& e) noexcept
        {
            auto& self=*static_cast<Session*>(context);
            const auto start=PD::Nanoseconds();
            if (self.conditional) { self.ConditionalEvent(e); self.detailedNs+=PD::Nanoseconds()-start; return; }
            auto& out=self.events;
            out<<self.episode<<','<<self.tick<<','<<++self.sequence<<','<<int(e.operation)<<','<<self.Index(e.a)<<','<<self.Index(e.b);
            for (const auto& v:{e.normal,e.anchorA,e.anchorB,e.linearImpulseB,e.angularImpulseB}) CsvVector(out,v);
            for (auto v:e.accumulatedBefore) out<<','<<v;
            for (auto v:e.accumulatedAfter) out<<','<<v;
            out<<','<<e.restitution<<','<<e.closingSpeed<<','<<e.penetration;
            for (const auto& state:{e.beforeA,e.beforeB,e.afterA,e.afterB}) {
                CsvVector(out,state.position); CsvVector(out,state.linear); CsvVector(out,state.angular);
            }
            out<<'\n'; ++self.eventCount;
            if (!out) self.Fail("contact trace write failed");
            self.detailedNs+=PD::Nanoseconds()-start;
        }
        static void PostBoundary(void* context,PD::Stage stage,const PhysicsWorld& world,
                                 ManifoldCollector& manifolds,std::span<const ContactIsland> islands,
                                 std::size_t bytes,int passes) noexcept
        {
            auto& self=*static_cast<Session*>(context);
            const auto start=PD::Nanoseconds(),oldRecorder=self.recorderNs;
            Boundary(context,stage,world,manifolds,islands,bytes,passes);
            auto& audit=*self.postAudit; const auto& all=world.GetPhysicsBodies();
            if (stage==PD::Stage::Begin) {
                audit.current={}; audit.current.episode=self.episode; audit.current.tick=self.tick;
                audit.responseOpen=audit.responseComplete=false; audit.cache={};
            }
            if ((self.episode==2 || self.episode==9) && self.tick<=6) for (const auto* body:all) {
                if (audit.stageCount==audit.stages.size()) { self.overflow=true; self.Fail("post-candidate stage capacity"); break; }
                auto& row=audit.stages[audit.stageCount++]; row.state=PD::Capture(*body);
                row.episode=self.episode; row.tick=self.tick; row.stage=unsigned(stage);
                row.slot=body->GetIdentity().GetSlot(); row.generation=body->GetIdentity().GetGeneration();
            }
            if (stage==PD::Stage::Narrowphase) {
                // Failure operands are doubles; preserve an exact integer count, even on failure.
                if (all.size()>(std::uint64_t{1}<<std::numeric_limits<double>::digits)) {
                    self.Fail("diagnostic body count exceeds exact double range");
                    return;
                }
                self.Gate(17,all.size()==2,static_cast<double>(all.size()),2);
                if (all.size()==2) for (unsigned i=0;i<2;++i) {
                    audit.responsePose[i]=PD::Capture(*all[i]); audit.current.beforeK+=self.Kinetic(*all[i],audit.responsePose[i]);
                }
                audit.responseWorkStart=self.currentTick.responseWork; audit.responseOpen=true;
                unsigned pairs=0;
                for (auto& manifold:manifolds.m_Manifolds) {
                    if (manifold.GetNumContacts()==0) continue;
                    ++pairs; self.Gate(17,pairs==1 && manifold.GetNumContacts()<=4,pairs,1);
                    for (int slot=0;slot<manifold.GetNumContacts() && slot<4;++slot) {
                        const auto c=manifold.GetContact(slot); auto& cache=audit.cache[slot];
                        cache.present=true; cache.a=c.m_BodyA; cache.b=c.m_BodyB;
                        cache.localA=c.ptOnA_LocalSpace; cache.localB=c.ptOnB_LocalSpace;
                    }
                }
            }
            if (stage==PD::Stage::Solve) {
                bool pose=audit.responseOpen && all.size()==2;
                if (all.size()==2) for (unsigned i=0;i<2;++i) {
                    const auto state=PD::Capture(*all[i]); const auto& before=audit.responsePose[i];
                    pose=pose && state.position==before.position && state.orientation==before.orientation;
                    audit.current.afterK+=self.Kinetic(*all[i],state);
                }
                self.Gate(17,pose,pose?0:1,0);
                audit.current.work=self.currentTick.responseWork-audit.responseWorkStart;
                const double delta=audit.current.afterK-audit.current.beforeK,tau=1e-5*std::max(1.,audit.current.beforeK);
                audit.current.balance=delta-audit.current.work;
                self.Gate(18,pose && std::isfinite(delta) && delta<=tau,delta,tau);
                self.Gate(19,pose && std::isfinite(audit.current.balance) && std::abs(audit.current.balance)<=tau,std::abs(audit.current.balance),tau);
                audit.responseOpen=false; audit.responseComplete=true;
            }
            if (stage==PD::Stage::End) {
                self.Gate(17,audit.responseComplete && !audit.responseOpen,audit.responseComplete?0:1,0);
                if (audit.pendingCount==audit.pending.size()) { self.overflow=true; self.Fail("post-candidate tick capacity"); }
                else audit.pending[audit.pendingCount++]=audit.current;
                ++audit.ticks;
            }
            const auto elapsed=PD::Nanoseconds()-start,oldCost=self.recorderNs-oldRecorder;
            const auto extra=elapsed>=oldCost?elapsed-oldCost:0;
            self.callbackNs+=extra; self.recorderNs+=extra;
            if (stage==PD::Stage::End) {
                self.pendingTick.boundaryNs=self.callbackNs;
                self.pendingTick.observerNs=self.callbackNs+self.pendingTick.eventObservationNs;
            }
        }
        static void PostStateHeader(std::ostream& out,const char* prefix)
        {
            for (const char* field:{"px","py","pz","qw","qx","qy","qz","vx","vy","vz","wx","wy","wz","inactive","sleeping"}) out<<','<<prefix<<'_'<<field;
        }
        static void PostStateRow(std::ostream& out,const PD::BodyState& state)
        {
            CsvVector(out,state.position); out<<','<<state.orientation.w<<','<<state.orientation.x<<','<<state.orientation.y<<','<<state.orientation.z;
            CsvVector(out,state.linear); CsvVector(out,state.angular); out<<','<<state.inactive<<','<<state.sleeping;
        }
        void FlushPostAudit()
        {
            if (!postAudit) return;
            auto& audit=*postAudit;
            for (;audit.eventWritten<audit.eventCount;++audit.eventWritten) {
                const auto& row=audit.events[audit.eventWritten]; const auto& e=row.event;
                postEvents<<row.episode<<','<<row.tick<<','<<row.sequence<<','<<int(e.operation);
                for (auto value:row.identity) postEvents<<','<<value;
                postEvents<<','<<e.slot<<','<<e.previousSlot<<','<<e.reason<<','<<e.constraintToken<<','<<e.featureA<<','<<e.featureB<<','<<e.appliedImpulsesKnown;
                for (const auto& state:{e.beforeA,e.beforeB,e.afterA,e.afterB}) PostStateRow(postEvents,state);
                for (const auto& vector:{e.normal,e.anchorA,e.anchorB,e.localA,e.localB,e.tangentU,e.tangentV,
                    e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB}) CsvVector(postEvents,vector);
                for (const auto& values:{e.accumulatedBefore,e.accumulatedAfter}) for (auto value:values) postEvents<<','<<value;
                for (const auto& values:{e.normalRow,e.tangentRowU,e.tangentRowV}) for (auto value:values) postEvents<<','<<value;
                for (auto value:{e.restitution,e.closingSpeed,e.penetration,e.time,e.duration,e.bias}) postEvents<<','<<value;
                postEvents<<'\n';
            }
            for (;audit.stageWritten<audit.stageCount;++audit.stageWritten) {
                const auto& row=audit.stages[audit.stageWritten];
                postStages<<row.episode<<','<<row.tick<<','<<row.stage<<','<<row.slot<<','<<row.generation;
                PostStateRow(postStages,row.state); postStages<<'\n';
            }
            for (unsigned i=0;i<audit.pendingCount;++i) {
                const auto& row=audit.pending[i];
                postTicks<<row.episode<<','<<row.tick<<','<<row.sliding<<','<<row.legacyPositive<<','<<row.infeasible<<','<<row.explained;
                for (double value:{row.maxF,row.maxGap,row.maxIncrement,row.maxIntermediateCone,row.beforeK,row.afterK,row.work,row.balance}) postTicks<<','<<value;
                postTicks<<'\n';
            }
            audit.pendingCount=0;
            if (!postEvents || !postStages || !postTicks) Fail("post-candidate evidence write failed");
            if (failed && !postFailureLogged) {
                Log::GetCoreLogger()->error("PHASE20 post-candidate diagnostic: {}",failure); postFailureLogged=true;
            }
        }
        void RecordBodies(const PhysicsWorld& world)
        {
            if (conditional || phase21) return;
            const auto& all=world.GetPhysicsBodies();
            if (all.size()!=inputs.size()) { Fail("unexpected body membership"); return; }
            for (unsigned i=0;i<all.size();++i) {
                const auto* b=all[i];
                Binary(bodies,episode); Binary(bodies,tick); Binary(bodies,i);
                Binary(bodies,b->GetIdentity().GetSlot()); Binary(bodies,b->GetIdentity().GetGeneration());
                BinaryState(bodies,PD::Capture(*b));
            }
        }
        static void Boundary(void* context,PD::Stage stage,const PhysicsWorld& world,
                             ManifoldCollector& manifolds,std::span<const ContactIsland> islands,
                             std::size_t bytes,int passes) noexcept
        {
            auto& self=*static_cast<Session*>(context);
            if (self.phase21) { self.Phase21Boundary(stage,world,manifolds,islands,bytes,passes); return; }
            const auto now=PD::Nanoseconds();
            if (self.conditional) {
                if (stage==PD::Stage::Begin) {
                    self.FlushConditionalTick();
                    ++self.tick; self.currentTick={}; self.currentTick.episode=self.episode; self.currentTick.tick=self.tick;
                    self.toiNormalImpulse=0; self.toiBodyA=self.toiBodyB=nullptr;
                    self.profileBefore=GetPhysicsProfileSnapshot(); self.callbackNs=self.detailedNs=0;
                    self.eventObservationBefore=self.sink.observationNs;
                    if (passes!=1 || self.tick>self.TickLimit()) self.Fail("conditional tick/pass budget mismatch");
                }
                if (stage==PD::Stage::Narrowphase) self.ConditionalNarrowphase(world,manifolds);
                if (stage==PD::Stage::End) self.ConditionalEndTick(world,manifolds,bytes);
                const auto cost=PD::Nanoseconds()-now; self.callbackNs+=cost; self.recorderNs+=cost;
                if (stage==PD::Stage::End) {
                    self.pendingTick.boundaryNs=self.callbackNs;
                    self.pendingTick.eventSinkNs=self.detailedNs;
                    self.pendingTick.eventObservationNs=self.sink.observationNs-self.eventObservationBefore;
                    self.pendingTick.observerNs=self.callbackNs+self.pendingTick.eventObservationNs;
                }
                return;
            }
            if (stage==PD::Stage::Begin) {
                ++self.tick; self.stageClock=self.stepClock=now; self.callbackNs=0; self.detailedNs=0;
                if (passes!=1 || self.tick>self.TickLimit()) self.Fail("tick/pass budget mismatch");
            }
            const auto interval=now-self.stageClock;
            const auto& all=world.GetPhysicsBodies();
            for (unsigned i=0;i<all.size();++i) if (self.Selected(i)) {
                Binary(self.stages,self.episode); Binary(self.stages,self.tick);
                Binary(self.stages,static_cast<unsigned>(stage)); Binary(self.stages,i);
                BinaryState(self.stages,PD::Capture(*all[i]));
            }
            if (stage==PD::Stage::End) self.EndTick(world,manifolds,islands,bytes);
            const auto cost=PD::Nanoseconds()-now;
            self.callbackNs+=cost; self.recorderNs+=cost;
            self.timing<<self.episode<<','<<self.tick<<','<<int(stage)<<','<<interval<<','<<cost<<','<<self.detailedNs<<','<<bytes<<'\n';
            if (!self.stages || !self.timing) self.Fail("boundary trace write failed");
            self.stageClock=PD::Nanoseconds();
        }
        void EndTick(const PhysicsWorld& world,ManifoldCollector& manifolds,
                     std::span<const ContactIsland> islands,std::size_t bytes)
        {
            double energy=0,maxLinear=0,maxAngular=0,drift=0,penetration=0;
            unsigned active=0,sleeping=0,contactCount=0;
            bool finite=true;
            const auto& all=world.GetPhysicsBodies();
            if (all.size()!=inputs.size() || previous.size()!=all.size()) { Fail("unexpected tick membership"); return; }
            for (unsigned i=0;i<all.size();++i) {
                const auto& b=*all[i]; const auto s=PD::Capture(b);
                finite &= b.HasFiniteState();
                if (b.GetInverseMass()>0) {
                    active+=!s.sleeping; sleeping+=s.sleeping;
                    wakeCount+=previous[i].sleeping && !s.sleeping;
                    sleepCount+=!previous[i].sleeping && s.sleeping;
                    const glm::dvec3 v(s.linear),w(s.angular);
                    const auto rotation=glm::dmat3(glm::mat3_cast(s.orientation));
                    const auto localW=glm::transpose(rotation)*w;
                    const auto mass=1.0/double(b.GetInverseMass());
                    energy+=mass*(.5*glm::dot(v,v)+.5*glm::dot(localW,glm::dmat3(b.m_Shape->InertiaTensor())*localW)-
                                  glm::dot(glm::dvec3(world.GetGravity()),glm::dvec3(s.position)));
                    maxLinear=std::max(maxLinear,glm::length(v)); maxAngular=std::max(maxAngular,glm::length(w));
                    drift=std::max(drift,glm::length(glm::dvec3(s.position)-glm::dvec3(inputs[i].position)));
                }
                previous[i]=s;
            }
            for (auto& manifold:manifolds.m_Manifolds) for (int j=0;j<manifold.GetNumContacts();++j) {
                const auto c=manifold.GetContact(j);
                if (!world.IsBodyIdentityValid(c.m_BodyA->GetIdentity()) ||
                    !world.IsBodyIdentityValid(c.m_BodyB->GetIdentity())) { Fail("stale contact identity"); return; }
                const auto a=c.m_BodyA->BodySpaceToWorldSpace(c.ptOnA_LocalSpace);
                const auto b=c.m_BodyB->BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
                const double depth=-glm::dot(a-b,c.normal);
                penetration=std::max(penetration,depth); ++contactCount;
                contacts<<episode<<','<<tick<<','<<Index(c.m_BodyA)<<','<<Index(c.m_BodyB)<<','<<c.featureA<<','<<c.featureB;
                CsvVector(contacts,a); CsvVector(contacts,b); CsvVector(contacts,c.normal); contacts<<','<<depth;
                CsvVector(contacts,c.ptOnA_LocalSpace); CsvVector(contacts,c.ptOnB_LocalSpace); contacts<<'\n';
            }
            for (unsigned i=0;i<islands.size();++i) {
                for (auto id:islands[i].dynamicBodies) islandRows<<episode<<','<<tick<<','<<i<<",dynamic,"<<id.GetSlot()-1<<",-1\n";
                for (auto id:islands[i].boundaryBodies) islandRows<<episode<<','<<tick<<','<<i<<",boundary,"<<id.GetSlot()-1<<",-1\n";
                for (const auto& pair:islands[i].contactPairs) islandRows<<episode<<','<<tick<<','<<i<<",pair,"<<pair.bodyA.GetSlot()-1<<','<<pair.bodyB.GetSlot()-1<<'\n';
            }
            // Accepted fixtures contain only spheres/boxes: at most 63 body lines
            // and exactly 50 lines per contact. Preserve every overlay primitive.
            debugLineCapacity=std::max(std::size_t{32768},all.size()*63+std::size_t(contactCount)*50);
            if (bytes>lastStorage) storageGrowth+=bytes-lastStorage;
            lastStorage=bytes;
            metrics<<episode<<','<<tick<<','<<all.size()<<','<<contactCount<<','<<islands.size()<<','<<active<<','<<sleeping<<','
                <<wakeCount<<','<<sleepCount<<','<<maxLinear<<','<<maxAngular<<','<<drift<<','<<energy<<','<<penetration<<','<<storageGrowth<<','<<finite<<'\n';
            RecordBodies(world);
            if (!finite || !std::isfinite(energy)) Fail("non-finite physical state");
            if (!bodies || !contacts || !metrics || !islandRows) Fail("common recording failed");
            if (failed || tick==TickLimit()) {
                episodeDone=true; scene->SetPaused(true);
            }
        }
        bool BeginEpisode(_Scene& target,SceneRenderResources& resources,
                          const SceneAssets& sceneAssets,std::vector<std::unique_ptr<PhysicalShape>>& shapes)
        {
            struct SetupGuard { Session& s; bool done{}; ~SetupGuard() { if (!done && (s.conditional || s.phase21)) s.RetireOwnedScene(); } } setup{*this};
            if (phase21 && sixtyHzScene) RetireOwnedScene();
            PD::observer=nullptr;
            applicationScene=&target; assets=sceneAssets;
            if (!entities.empty()) {
                scene->OnRuntimeStop();
                for (auto entity:entities) if (auto removed=scene->DestroyEntity(entity);!removed) {
                    Fail("fixture entity retirement failed"); return false;
                }
                entities.clear();
                if (sixtyHzScene) ownedShapes.clear(); else shapes.clear();
            }
            if ((phase21 && !phase21->renderedSupport) || (conditional && episode!=0 && !sixtyHzScene)) {
                sixtyHzScene.reset(new(std::nothrow) _Scene);
                if (!sixtyHzScene || sixtyHzScene->GetPhysicsStep().count()!=1.0/60.0) { Fail("fresh 60Hz fixture Scene unavailable"); RetireOwnedScene(); return false; }
                if (phase21 && !sixtyHzScene->SetPhysicsStep(Seconds{StepSeconds()})) { Fail("Phase21 fixed step setup"); RetireOwnedScene(); return false; }
            }
            scene=sixtyHzScene?sixtyHzScene.get():&target;
            if (phase21 && phase21->renderedSupport &&
                ((episode==0 && !scene->SetPhysicsStep(Seconds{1.0/60.0})) || scene->GetPhysicsStep().count()!=1.0/60.0 || scene!=applicationScene)) {
                Fail("Phase21 rendered support clock/scene"); return false;
            }
            auto& shapeOwners=sixtyHzScene?ownedShapes:shapes;
            inputs=episode==0?lattice:Focused(episode);
            if (phase21) {
                const auto kind=phase21->cases[episode].kind;
                inputs=kind==0?lattice:Focused(kind==1?8:1);
                if (kind==4) {
                    // Normal preset runtime order: the five later-authored boundaries,
                    // then sphere, then reverse insertion order of the sixteen boxes.
                    inputs.assign(lattice.begin(),lattice.begin()+5);
                    BodyInput sphere; sphere.type=1; sphere.inverseMass=1; sphere.position={3.5f,5,-20}; sphere.linear={0,0,40}; inputs.push_back(sphere);
                    for (int y=3;y>=0;--y) for (int x=3;x>=0;--x) inputs.push_back(Box({float(x)*(2.f+.01f),1.5f+float(y)*2.f,0},{2,2,2},true));
                }
            }
            if (conditional && episode==9) { inputs=Focused(1); inputs[1].linear={.5f,0,0}; }
            if (conditional && episode==10) { inputs=Focused(2); inputs[1].shape=2; }
            entities.resize(inputs.size());
            // EnTT's packed component view visits reverse insertion order. Author
            // in reverse so PhysicsWorld construction matches export order exactly.
            for (std::size_t n=inputs.size();n>0;--n) {
                const auto i=n-1; const auto& b=inputs[i];
                auto created=scene->CreateEntity("P19 "+std::string(EpisodeName())+" "+std::to_string(i));
                if (!created) { Fail("fixture entity creation failed"); return false; }
                auto entity=*created; entities[i]=entity;
                auto& transform=entity.GetComponent<Component::Transform3DComponent>();
                transform.Translation=b.position; transform.QuatRotation=b.orientation;
                if (b.shape!=0) {
                    Vec3f low=b.points.front(),high=low;
                    for (auto p:b.points) { low=glm::min(low,p); high=glm::max(high,p); }
                    transform.Scale=high-low;
                }
                const auto mesh=b.shape==0?assets.smoothSphereGeo:assets.boxMesh;
                const auto material=b.type==0?assets.floorMaterial:(b.shape==0?assets.sphereMaterial:assets.boxMaterial);
                if (auto assigned=resources.AssignRenderable(entity,{mesh,material});!assigned) {
                    Fail("fixture render assignment failed"); return false;
                }
                if (b.shape!=0) if (auto attached=resources.AttachPhysicsShape(entity,"Box");!attached) {
                    Fail("fixture box attachment failed"); return false;
                }
                Component::RigidBody3DComponent rb; rb.Type=static_cast<Component::BodyType>(b.type);
                rb.CollisionLayer=b.layer; rb.CollisionMask=b.mask;
                entity.AddOrReplaceComponent<Component::RigidBody3DComponent>(rb);
                Component::Fixture3DProperty property;
                property.m_Position=b.position; property.m_Orientation=b.orientation;
                property.m_LinearVelocity=b.linear; property.m_AngularVelocity=b.angular;
                property.m_InvMass=b.inverseMass; property.m_Elasticity=b.elasticity; property.m_Friction=b.friction;
                if (b.shape==0) {
                    Component::SphereFixture3DComponent fixture; fixture.Radius=b.radius; fixture.Property=property;
                    entity.AddOrReplaceComponent<Component::SphereFixture3DComponent>(fixture);
                } else if (b.shape==1) {
                    Component::BoxFixture3DComponent fixture; fixture.Property=property;
                    entity.AddOrReplaceComponent<Component::BoxFixture3DComponent>(fixture);
                } else {
                    Component::ConvexFixture3DComponent fixture; fixture.Property=property;
                    entity.AddOrReplaceComponent<Component::ConvexFixture3DComponent>(fixture);
                }
            }
            shapeOwners.reserve(inputs.size());
            if (auto started=scene->OnRuntimeStart();!started) { Fail("fixture runtime construction failed"); return false; }
            auto* system=scene->GetPhysicsSystem(); auto* world=system->GetPhysicsWorld();
            const auto& all=world->GetPhysicsBodies();
            for (auto* b:all) shapeOwners.emplace_back(b->m_Shape);
            if (all.size()!=inputs.size() || system->GetSolverIterations()!=1 || !system->IsSleepingEnabled()) {
                Fail("fixture membership or Physics policy differs"); return false;
            }
            world->SetGravity(!phase21 && episode>=3 && episode<=6?Vec3f(0):Vec3f(0,-12,0));
            if (phase21) {
                const auto& c=phase21->cases[episode];
                if (!system->SetSolverIterations(int(c.passes))) { Fail("Phase21 pass selection"); return false; }
                system->SetSleepingEnabled(c.sleep);
            }
            for (unsigned i=0;i<all.size();++i) {
                auto& b=*all[i]; const auto& source=inputs[i];
                if (&b!=entities[i].GetComponent<Component::RigidBody3DComponent>().RuntimeBody || Index(&b)!=i) {
                    Fail("runtime creation order differs from frozen input"); return false;
                }
                // The historical text contains float quaternion bits. Configure
                // those exact initial fields before the first normal Scene tick.
                b.m_Position=source.position; b.m_Orientation=source.orientation;
                if (source.shape!=0) b.m_Shape->Build(source.points);
                const float resistance=(conditional && episode==9) || (phase21 && phase21->cases[episode].kind==4)?0.f:.05f;
                if (!b.m_Shape->IsValid() || !b.SetSpinResistanceLength(resistance) || !b.SetRollingResistanceLength(resistance) ||
                    b.m_LinearVelocity!=source.linear || b.m_AngularVelocity!=source.angular ||
                    b.m_InvMass!=source.inverseMass || b.m_Elasticity!=source.elasticity || b.m_Friction!=source.friction ||
                    b.GetSleepSettings()!=RigidBody3D::SleepSettings{}) {
                    Fail("runtime input interpretation differs"); return false;
                }
                const auto inertia=b.m_Shape->InertiaTensor();
                initial<<episode<<','<<i<<','<<b.GetIdentity().GetSlot()<<','<<b.GetIdentity().GetGeneration()<<','
                    <<source.shape<<','<<int(b.Type)<<','<<b.m_InvMass<<','<<b.m_Elasticity<<','<<b.m_Friction<<','
                    <<b.m_CollisionLayer<<','<<b.m_CollisionMask<<','<<source.radius;
                CsvVector(initial,b.m_Position);
                initial<<','<<b.m_Orientation.w<<','<<b.m_Orientation.x<<','<<b.m_Orientation.y<<','<<b.m_Orientation.z;
                CsvVector(initial,b.m_LinearVelocity); CsvVector(initial,b.m_AngularVelocity);
                initial<<','<<b.GetSpinResistanceLength()<<','<<b.GetRollingResistanceLength()<<','
                    <<b.GetSleepSettings().linearSpeedThreshold<<','<<b.GetSleepSettings().angularSpeedThreshold<<','
                    <<b.GetSleepSettings().inactivitySeconds<<','<<inertia[0][0]<<','<<inertia[1][1]<<','<<inertia[2][2]<<'\n';
            }
            scene->SetPaused(false);
            scene->Update(Timestep(0.0)); // Synchronize authoritative initial poses; no Physics tick.
            tick=0; sequence=0; wakeCount=sleepCount=0; lastStorage=storageGrowth=0;
            previous.clear(); previous.reserve(all.size());
            for (auto* b:all) previous.push_back(PD::Capture(*b));
            if (phase21 && phase21->amended && phase21->cases[episode].kind==0) {
                auto& pairs=phase21->pairs;
                for (unsigned i=0;i<all.size();++i) {
                    pairs.slots[i]=all[i]->GetIdentity().GetSlot(); pairs.generations[i]=all[i]->GetIdentity().GetGeneration();
                    const auto settings=all[i]->GetSleepSettings();
                    pairs.quiet[i]=glm::length(glm::dvec3(previous[i].linear))<=settings.linearSpeedThreshold && glm::length(glm::dvec3(previous[i].angular))<=settings.angularSpeedThreshold;
                    pairs.priorQuiet[i]=pairs.quiet[i];
                }
            }
            if (conditional) {
                previousEnergy=previousKinetic=0; impactResponses=0; restWindowEnergy=restMinY=restMaxY=0;
                for (auto* b:all) { const double kinetic=Kinetic(*b,PD::Capture(*b)); previousKinetic+=kinetic; previousEnergy+=kinetic+Potential(*b,PD::Capture(*b)); }
            }
            RecordBodies(*world);
            episodeDone=false;
            if (phase21) {
                double energy=0; for (auto* body:all) energy+=Kinetic(*body,PD::Capture(*body))+Potential(*body,PD::Capture(*body));
                if (phase21->refined) { phase21->refinement.minY=phase21->refinement.maxY=phase21->refinement.restEnergy=0; }
                if (phase21->recovery03) { phase21->audit.originsUsed=0; phase21->audit.originIndex.fill(0); }
                const auto& c=phase21->cases[episode];
                events<<episode<<','<<c.name<<','<<c.hz<<','<<c.passes<<','<<c.sleep<<','<<c.ticks<<','<<energy<<','<<all.size()<<'\n';
                if (c.kind==0 && energy!=30240.) { Fail("Phase21 historical initial energy"); return false; }
                sink.detailed=phase21->observed || phase21->recovery03; sink.targeted=sink.detailed; sink.boundary=&Boundary; sink.event=&Phase21Event;
                PD::observer=&sink; setup.done=true; return true;
            }
            if (conditional && episode==0 && !queriesComplete && !RunConditionalQueries(*this)) return false;
            sink.detailed=config.observation==Observation::Observed && !LightInterval();
            sink.targeted=conditional && !LightInterval();
            sink.boundary=postPolicy && !LightInterval()?&PostBoundary:&Boundary;
            PD::observer=&sink;
            Log::GetCoreLogger()->info("PHASE19_EPISODE_BEGIN {} bodies={} ticks={}",EpisodeName(),all.size(),TickLimit());
            setup.done=!failed && bodies && initial;
            return !failed && bodies && initial;
        }
        bool FrameComplete(double elapsed)
        {
            if (phase21) return Phase21Frame(elapsed);
            if (conditional && sixtyHzScene && !complete && !failed) scene->Update(Timestep(elapsed));
            FlushConditionalTick();
            FlushPostAudit();
            const auto& t=scene->GetPhysicsTiming();
            frames<<episode<<','<<tick<<','<<elapsed<<','<<t.pendingSeconds<<','<<t.discardedSeconds<<','
                <<t.totalSteps<<','<<t.stepsLastUpdate<<','<<scene->GetRenderInterpolationAlpha()<<'\n';
            if (!frames) Fail("frame accounting write failed");
            if (episodeDone && !failed) {
                if (t.totalSteps!=TickLimit() || scene->SetPhysicsStep(Seconds{StepSeconds()==1.0/60.0?1.0/120.0:1.0/60.0}) ||
                    scene->GetPhysicsStep().count()!=StepSeconds()) { Fail("clock immutability/tick count failed"); RetireOwnedScene(); return false; }
                ++completedEpisodes;
                completedTicks+=tick;
                Log::GetCoreLogger()->info("PHASE19_EPISODE_COMPLETE {} ticks={}",EpisodeName(),tick);
                if (completedEpisodes==(conditional?11u:9u)) { complete=true; PD::observer=nullptr; RetireOwnedScene(); }
            }
            if (failed) RetireOwnedScene();
            if (conditional) {
                std::uint64_t total=queryOperandExportBytes;
                for (auto* stream:{&bodies,&stages,&contacts,&timing,&metrics,&events,&frames,&initial,&islandRows,&queryRows,&queryInputs,&helperRows,&cpuRows,&qualificationRows,&postEvents,&postStages,&postTicks})
                    if (stream->is_open()) { const auto n=stream->tellp(); if (n<0) Fail("conditional export position failed"); else total+=static_cast<std::uint64_t>(n); }
                if (total>8*1024*1024-4096) { overflow=true; Fail("conditional eight MiB export bound"); RetireOwnedScene(); }
            }
            return !failed;
        }
        bool FinishAfterAppShutdown(bool clean)
        {
            if (phase21) return Phase21Finish(clean);
            PD::observer=nullptr;
            FlushPostAudit();
            for (auto* out:{&bodies,&stages,&contacts,&timing,&metrics,&events,&frames,&initial,&islandRows}) {
                out->flush(); if (!*out) Fail("final evidence flush failed"); out->close();
            }
            std::ofstream out(std::filesystem::path(config.outputDirectory)/"result.json");
            for (auto* stream:{&queryRows,&queryInputs,&helperRows,&cpuRows,&qualificationRows,&postEvents,&postStages,&postTicks}) if (stream->is_open()) { stream->flush(); if (!*stream) Fail("conditional evidence flush failed"); stream->close(); }
            if (conditional) {
                std::ofstream gates(std::filesystem::path(config.outputDirectory)/"focused-gates.csv");
                gates<<"gate,name,checks,failures,examples\n";
                for (unsigned i=0;i<(postPolicy?GateNames.size():15);++i) gates<<i<<','<<GateNames[i]<<','<<gateChecks[i]<<','<<gateFailures[i]<<','<<std::min<std::uint64_t>(2,gateFailures[i])<<'\n';
                gates.flush(); if (!gates) Fail("conditional gate aggregate write failed");
            }
            const bool ok=clean && complete && !failed && completedEpisodes==(conditional?11u:9u) &&
                (!conditional || (queriesComplete && completedTicks==4980 && profiledTicks==4980 && physicsWorldNs>0 && !overflow && collisionCalls==50 && directCalls==16 && caCalls==12 && projectionCalls==4)) &&
                (!postPolicy || (postAudit->ticks==1680 && postAudit->stageCount==288 && postAudit->sliding>0 && gateChecks[15]>0 && gateChecks[16]>0 && gateChecks[20]>0));
            out<<"{\"schema\":1,\"complete\":"<<(ok?"true":"false")<<",\"episodes\":"<<completedEpisodes
                <<",\"detailed_events\":"<<eventCount<<",\"common_recorder_ns\":"<<recorderNs
                <<",\"shutdown\":"<<(clean?"true":"false")<<",\"conditional\":"<<(conditional?"true":"false")
                <<",\"candidate\":"<<(candidate?"true":"false")<<",\"ticks\":"<<completedTicks<<",\"collision_calls\":"<<collisionCalls
                <<",\"direct_gjk_calls\":"<<directCalls<<",\"ca_helper_calls\":"<<caCalls<<",\"projection_helper_calls\":"<<projectionCalls
                <<",\"overflow\":"<<(overflow?"true":"false")<<",\"profiling_enabled\":"<<(IsPhysicsProfilingEnabled()?"true":"false")
                <<",\"profiled_ticks\":"<<profiledTicks<<",\"physics_world_ns\":"<<physicsWorldNs
                <<",\"post_candidate_policy\":"<<(postPolicy?"true":"false")<<",\"post_storage_bytes\":"<<(postPolicy?sizeof(PostCandidateAudit):0)
                <<",\"post_ticks\":"<<(postAudit?postAudit->ticks:0)<<",\"post_event_records\":"<<(postAudit?postAudit->eventCount:0)
                <<",\"post_stage_records\":"<<(postAudit?postAudit->stageCount:0)<<",\"post_sliding\":"<<(postAudit?postAudit->sliding:0)
                <<",\"legacy_positive_sliding\":"<<(postAudit?postAudit->legacyPositive:0)<<",\"explained_infeasible_old\":"<<(postAudit?postAudit->explained:0)
                <<",\"physical_failures\":"<<physicalFailures<<",\"failure\":\""<<failure<<"\"}\n";
            return ok && bool(out);
        }
    };
    inline volatile const std::uint64_t Phase21CompiledStorage[8]{
        0x50323153544f5231ull,sizeof(Session),sizeof(Phase21Capture),sizeof(PostCandidateAudit),
        sizeof(ConstraintPenetration),sizeof(Manifold),sizeof(Phase21Capture::Refinement),sizeof(Phase21Capture::Row)};
    static_assert(sizeof(Phase21Capture::PairStorage)+sizeof(Phase21Capture::Attribution)+sizeof(Phase21Capture::CausalCapture)+
        sizeof(Phase21Capture::Refinement)+2*sizeof(std::ofstream)+32*185*sizeof(double)<=2*1024*1024,"refinement within existing incremental observer allocation");
    inline std::unique_ptr<Session> session;

    // Fixed-size, value-only capture. Raw words avoid decimal round-trip ambiguity
    // and padding-dependent equality when deduplicating repeated EPA hulls.
    struct EpaCapture
    {
        static constexpr std::size_t Header=58, PointStart=Header,
            FaceStart=PointStart+PD::EpaCaptureMaxPoints*9,
            Words=FaceStart+PD::EpaCaptureMaxFaces*3, Capacity=13*11;
        using Snapshot=std::array<std::uint32_t,Words>;
        struct Attempt { unsigned tick{},iteration{},snapshot{}; };
        std::array<Snapshot,Capacity> snapshots{};
        std::array<Attempt,Capacity> attempts{};
        PD::BarycentricEvent bary{};
        unsigned snapshotCount{},attemptCount{};
        bool hasBary{},overflow{},missingBary{};
        void Capture(unsigned tick,const PD::EpaProjectionEvent& e) noexcept
        {
            if (attemptCount==Capacity) { overflow=true; return; }
            Snapshot packed{}; unsigned n=0;
            auto u=[&](auto v){packed[n++]=static_cast<std::uint32_t>(v);};
            auto f=[&](float v){u(std::bit_cast<std::uint32_t>(v));};
            auto v=[&](const Vec3f& p){f(p.x);f(p.y);f(p.z);};
            u(e.pointCount);u(e.faceCount);u(e.selectedFace);u(e.epaIterations);
            u(e.convergenceReason);u(e.failedPredicates);u(e.overflow);
            f(e.bias);f(e.originDistance);f(e.lambdaSum);f(e.projectionErrorSquared);f(e.projectionScaleSquared);
            v(e.normal);v(e.lambdas);v(e.projectedPoint);v(e.expectedProjection);
            v(e.lastSupport.xyz);v(e.lastSupport.ptA);v(e.lastSupport.ptB);v(e.lastSupportNormal);
            f(e.lastOriginDistance);f(e.lastExpansionDistance);f(e.lastConvergenceTolerance);u(e.lastSelectedFace);
            const bool matched=hasBary && bary.iteration==e.iteration;
            missingBary=missingBary || !matched;
            u(bary.exit);u(bary.validFields);f(bary.normalLengthSquared);f(bary.dominantArea);u(bary.dominantAxis);
            v(bary.projectedOrigin);v(bary.subareas);v(bary.rawWeights);v(bary.returnedWeights);u(matched);
            if (n!=Header || e.overflow || e.pointCount>PD::EpaCaptureMaxPoints ||
                e.faceCount>PD::EpaCaptureMaxFaces || e.points.size()!=e.pointCount || e.faces.size()!=e.faceCount) overflow=true;
            n=PointStart;
            for (const auto& p:e.points.first(std::min(e.points.size(),PD::EpaCaptureMaxPoints))) {v(p.xyz);v(p.ptA);v(p.ptB);}
            n=FaceStart;
            for (const auto& face:e.faces.first(std::min(e.faces.size(),PD::EpaCaptureMaxFaces))) {u(face.a);u(face.b);u(face.c);}
            unsigned index=0;
            while (index<snapshotCount && snapshots[index]!=packed) ++index;
            if (index==snapshotCount) {
                if (snapshotCount==Capacity) {overflow=true;return;}
                snapshots[snapshotCount++]=packed;
            }
            attempts[attemptCount++]={tick,e.iteration,index};
        }
        bool Write(const std::filesystem::path& dir) const
        {
            std::ofstream rows(dir/"epa-projections.csv"), points(dir/"epa-points.csv"),
                faces(dir/"epa-faces.csv"), calls(dir/"epa-attempts.csv");
            rows<<"snapshot,point_count,face_count,selected_face,epa_iterations,convergence_reason,failed_predicates,overflow,bias_bits,origin_distance_bits,lambda_sum_bits,projection_error_squared_bits,projection_scale_squared_bits";
            for (const char* name:{"normal","lambdas","projected","expected","last_support_xyz","last_support_a","last_support_b","last_support_normal"})
                for (char axis:std::string("xyz")) rows<<','<<name<<'_'<<axis<<"_bits";
            rows<<",last_origin_distance_bits,last_expansion_distance_bits,last_tolerance_bits,last_selected_face,bary_exit,bary_valid_fields,bary_normal_squared_bits,bary_area_bits,bary_axis";
            for (const char* name:{"bary_projected","bary_subareas","bary_raw_weights","bary_returned_weights"})
                for (char axis:std::string("xyz")) rows<<','<<name<<'_'<<axis<<"_bits";
            rows<<",bary_matched\n";
            points<<"snapshot,point";
            for (const char* name:{"xyz","a","b"}) for (char axis:std::string("xyz")) points<<','<<name<<'_'<<axis<<"_bits";
            points<<'\n';faces<<"snapshot,face,a,b,c\n";calls<<"attempt,tick,iteration,snapshot\n";
            for (unsigned i=0;i<snapshotCount;++i) {
                const auto& p=snapshots[i];rows<<i;
                for (unsigned j=0;j<Header;++j) rows<<','<<p[j];rows<<'\n';
                for (unsigned j=0;j<std::min<std::size_t>(p[0],PD::EpaCaptureMaxPoints);++j) {
                    points<<i<<','<<j;for(unsigned k=0;k<9;++k) points<<','<<p[PointStart+j*9+k];points<<'\n';
                }
                for (unsigned j=0;j<std::min<std::size_t>(p[1],PD::EpaCaptureMaxFaces);++j)
                    faces<<i<<','<<j<<','<<p[FaceStart+j*3]<<','<<p[FaceStart+j*3+1]<<','<<p[FaceStart+j*3+2]<<'\n';
            }
            for(unsigned i=0;i<attemptCount;++i) {const auto& a=attempts[i];calls<<i<<','<<a.tick<<','<<a.iteration<<','<<a.snapshot<<'\n';}
            rows.flush();points.flush();faces.flush();calls.flush();
            return bool(rows)&&bool(points)&&bool(faces)&&bool(calls);
        }
    };

    // Phase20's accepted query replay. Copies are never registered in a world.
    struct QueryReplay
    {
        struct Input { unsigned tick; std::array<std::uint32_t,13> bits; };
        inline static constexpr std::array<Input,13> Inputs{{
            {566, {0x3f441671u, 0x3fb8dd29u, 0x3fc6a02du, 0xbeb6d2e1u, 0x3e07a4aeu, 0xbdab3477u, 0xbf6bbcacu, 0xbeaff5b2u, 0xbf83f758u, 0x3dab1049u, 0x3dc5218cu, 0xbe179a69u, 0x3eaea752u}},
            {579, {0x3f2fa723u, 0x3fbd63fcu, 0x3fc936deu, 0xbea55860u, 0x3e161b9fu, 0xbd805b34u, 0xbf6ed237u, 0xbed2aeddu, 0xbe6e244cu, 0x3dd389efu, 0x3dea306au, 0xbe1745cdu, 0x3ed1661bu}},
            {580, {0x3f2dd948u, 0x3fbd5700u, 0x3fc970bdu, 0xbea3c8c7u, 0x3e172e7fu, 0xbd793810u, 0xbf6f141eu, 0xbed87edau, 0xbe723ce6u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {581, {0x3f2c0b6du, 0x3fbcd5ceu, 0x3fc9aa9cu, 0xbea23890u, 0x3e1840cdu, 0xbd71b8c6u, 0xbf6f551eu, 0xbed87edau, 0xbedf84dau, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {582, {0x3f2a3d92u, 0x3fbbe762u, 0x3fc9e47bu, 0xbea0a7bcu, 0x3e195288u, 0xbd6a3894u, 0xbf6f9537u, 0xbed87edau, 0xbf22f5a0u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {583, {0x3f286fb7u, 0x3fba8bbcu, 0x3fca1e5au, 0xbe9f164cu, 0x3e1a63afu, 0xbd62b77eu, 0xbf6fd468u, 0xbed87edau, 0xbf5628d4u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {584, {0x3f26a1dcu, 0x3fb8c2dcu, 0x3fca5839u, 0xbe9d8442u, 0x3e1b7441u, 0xbd5b358cu, 0xbf7012b1u, 0xbed87edau, 0xbf84ae04u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {585, {0x3f24d401u, 0x3fb68cc2u, 0x3fca9218u, 0xbe9bf1a0u, 0x3e1c843cu, 0xbd53b2c7u, 0xbf705013u, 0xbed87edau, 0xbf9e479eu, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {586, {0x3f230626u, 0x3fb3e96eu, 0x3fcacbf7u, 0xbe9a5e67u, 0x3e1d93a0u, 0xbd4c2f35u, 0xbf708c8bu, 0xbed87edau, 0xbfb7e138u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {587, {0x3f21384bu, 0x3fb0d8e0u, 0x3fcb05d6u, 0xbe98ca9au, 0x3e1ea26du, 0xbd44aae0u, 0xbf70c81cu, 0xbed87edau, 0xbfd17ad2u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {588, {0x3f1f6a70u, 0x3fad5b18u, 0x3fcb3fb5u, 0xbe973639u, 0x3e1fb0a0u, 0xbd3d25cbu, 0xbf7102c3u, 0xbed87edau, 0xbfeb146cu, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {589, {0x3f1d9c95u, 0x3fa97016u, 0x3fcb7994u, 0xbe95a146u, 0x3e20be38u, 0xbd359ffeu, 0xbf713c81u, 0xbed87edau, 0xc0025703u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {590, {0x3f1bcc30u, 0x3fae9029u, 0x3fcbb6d8u, 0xbe9406e5u, 0x3e21cf59u, 0xbd2e44a5u, 0xbf7175c9u, 0xbed9b3dau, 0xbe4dd7beu, 0x3de5bf0bu, 0x3de5dc8cu, 0xbe173a31u, 0x3ed9202bu}},
        }};
        static std::array<std::uint32_t,13> PoseBits(const PD::BodyState& b) noexcept
        {
            const std::array<float,13> values{b.position.x,b.position.y,b.position.z,
                b.orientation.w,b.orientation.x,b.orientation.y,b.orientation.z,
                b.linear.x,b.linear.y,b.linear.z,b.angular.x,b.angular.y,b.angular.z};
            std::array<std::uint32_t,13> out{};
            for (unsigned i=0;i<out.size();++i) out[i]=std::bit_cast<std::uint32_t>(values[i]);
            return out;
        }
        static bool SameBody(const PD::BodyState& a,const PD::BodyState& b) noexcept
        {
            return PoseBits(a)==PoseBits(b) && a.sleeping==b.sleeping &&
                std::bit_cast<std::uint64_t>(a.inactive)==std::bit_cast<std::uint64_t>(b.inactive);
        }
        static std::array<std::uint32_t,19> ContactBits(const contact_t& c) noexcept
        {
            std::array<std::uint32_t,19> out{}; unsigned n=0;
            for (const auto& v:{c.ptOnA_WorldSpace,c.ptOnB_WorldSpace,c.ptOnA_LocalSpace,c.ptOnB_LocalSpace,c.normal})
                for (int i=0;i<3;++i) out[n++]=std::bit_cast<std::uint32_t>(v[i]);
            out[n++]=std::bit_cast<std::uint32_t>(c.separationDistance);
            out[n++]=std::bit_cast<std::uint32_t>(c.timeOfImpact);
            out[n++]=c.featureA; out[n++]=c.featureB; return out;
        }
        static void Load(RigidBody3D& b,const Input& input) noexcept
        {
            std::array<float,13> f{};
            for (unsigned i=0;i<f.size();++i) f[i]=std::bit_cast<float>(input.bits[i]);
            b.m_Position={f[0],f[1],f[2]}; b.m_Orientation=Quat(f[3],f[4],f[5],f[6]);
            b.m_LinearVelocity={f[7],f[8],f[9]}; b.m_AngularVelocity={f[10],f[11],f[12]};
        }
    };
    static_assert(sizeof(EpaCapture)<=4*1024*1024,"accepted EPA capture storage bound");

    // Fixed Phase20 matrix. Query copies are never installed in a PhysicsWorld.
    // The source pins every construction; exports bind all interpreted float bits
    // before a query. No reference contact is fed back into collision execution.
    struct ConditionalQueryTrace
    {
        EpaCapture* epa{}; unsigned caseIndex{},events{};
        PD::QueryEvent contact{},distance{},exit{};
        static void Observe(void* context,const PD::QueryEvent& e) noexcept
        {
            auto& s=*static_cast<ConditionalQueryTrace*>(context); ++s.events;
            if (e.kind==PD::QueryKind::Begin && s.epa) s.epa->hasBary=false;
            if (e.kind==PD::QueryKind::ContactSearch) s.contact=e;
            if (e.kind==PD::QueryKind::ClosestPoints) s.distance=e;
            if (e.kind==PD::QueryKind::Exit) s.exit=e;
        }
        static void Barycentric(void* context,const PD::BarycentricEvent& e) noexcept
        { auto& s=*static_cast<ConditionalQueryTrace*>(context); if(s.epa){s.epa->bary=e;s.epa->hasBary=true;} }
        static void Projection(void* context,const PD::EpaProjectionEvent& e) noexcept
        { auto& s=*static_cast<ConditionalQueryTrace*>(context); if(s.epa)s.epa->Capture(s.caseIndex,e); }
    };
    class ConditionalInvalidSupport final:public ShapeSphere
    {
    public:
        mutable unsigned calls{}; unsigned failAt{};
        Vec3f Support(const Vec3f& direction,const Vec3f& position,const Quat& orientation,float bias) const override
        { if (++calls==failAt) return Vec3f(std::numeric_limits<float>::quiet_NaN()); return ShapeSphere::Support(direction,position,orientation,bias); }
    };
    inline void ConditionalJsonVector(std::ostream& out,const Vec3f& v)
    { out<<'['<<v.x<<','<<v.y<<','<<v.z<<']'; }
    inline bool RunConditionalQueries(Session& s)
    {
        if (!s.conditional || s.queriesComplete || PD::queryObserver || s.scene->GetPhysicsTiming().totalSteps!=0) { s.Fail("conditional query entry invariant"); return false; }
        if (!IsPhysicsProfilingEnabled()) { s.Fail("conditional original Physics timers disabled"); return false; }
        const auto& world=s.scene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies();
        if (world.size()!=185) { s.Fail("conditional original world membership"); return false; }
        const auto unitPoints=Box({},Vec3f(2),true).points;
        auto boxResult=ShapeBox::Create(unitPoints);
        if (!boxResult) { s.Fail("conditional unit Box construction"); return false; }
        ShapeBox unitBox=std::move(*boxResult);
        ShapeConvex unitConvex(unitPoints),tetra(std::vector<Vec3f>{{-1,-1,-1},{1,-1,-1},{0,-1,1},{0,1,0}});
        if (!unitConvex.IsValid() || !tetra.IsValid()) { s.Fail("conditional Convex construction"); return false; }
        const auto f=[](std::uint32_t bits){return std::bit_cast<float>(bits);};
        const Quat rotation(f(0x3f7ba1b6),f(0x3d495ddc),f(0x3dc95ddc),f(0x3e170665));
        const Quat tilt(f(0x3f7dcf55),0,0,f(0x3e05a8a9));
        if (rotation!=glm::angleAxis(.37f,Math::NormalizeOr(Vec3f(1,2,3))) || tilt!=glm::angleAxis(glm::radians(15.f),Vec3f(0,0,1))) {
            s.Fail("conditional pinned GLM rotation differs"); return false;
        }
        const Vec3f axis=rotation*Vec3f(1,0,0);
        std::array<PD::BodyState,185> liveBefore{};
        for (unsigned i=0;i<world.size();++i) liveBefore[i]=PD::Capture(*world[i]);
        auto capture=std::unique_ptr<EpaCapture>(new(std::nothrow) EpaCapture);
        if (!capture) { s.Fail("conditional bounded query recorder allocation"); return false; }
        ConditionalQueryTrace trace; PD::QueryObserver observer{}; observer.context=&trace; observer.event=&ConditionalQueryTrace::Observe;
        observer.barycentric=&ConditionalQueryTrace::Barycentric; observer.epaProjection=&ConditionalQueryTrace::Projection;
        const auto bodyInput=[&](unsigned id,unsigned role,const RigidBody3D& body) {
            auto& out=s.queryInputs; out<<id<<','<<role<<','<<int(body.m_Shape->GetShapeType())<<','<<int(body.Type);
            for (float x:{body.m_InvMass,body.m_Elasticity,body.m_Friction}) out<<','<<std::bit_cast<std::uint32_t>(x);
            out<<','<<body.m_CollisionLayer<<','<<body.m_CollisionMask;
            const auto& sleep=body.GetSleepSettings();
            for(float x:{body.GetSpinResistanceLength(),body.GetRollingResistanceLength(),sleep.linearSpeedThreshold,sleep.angularSpeedThreshold}) out<<','<<std::bit_cast<std::uint32_t>(x);
            out<<','<<std::bit_cast<std::uint64_t>(sleep.inactivitySeconds)<<','<<body.IsSleeping()<<','<<std::bit_cast<std::uint64_t>(body.GetInactiveSeconds());
            out<<','<<std::bit_cast<std::uint32_t>(body.m_Shape->GetShapeType()==ShapeType::Sphere?static_cast<const ShapeSphere*>(body.m_Shape)->GetRadius():0.f);
            for (auto x:QueryReplay::PoseBits(PD::Capture(body))) out<<','<<x;
            const auto bounds=body.m_Shape->GetBounds();
            for (const auto& v:{body.m_Shape->GetCenterOfMass(),bounds.mins,bounds.maxs}) for (int k=0;k<3;++k) out<<','<<std::bit_cast<std::uint32_t>(v[k]);
            const auto inertia=body.m_Shape->InertiaTensor();
            for (int c=0;c<3;++c) for(int r=0;r<3;++r) out<<','<<std::bit_cast<std::uint32_t>(inertia[c][r]); out<<'\n';
        };
        const auto shapeContains=[&](const RigidBody3D& body,const Vec3f& witness,float toi) {
            constexpr double tolerance=.0025;
            const auto position=body.m_Position+body.GetLinearVelocity()*toi;
            const glm::dvec3 p=glm::transpose(glm::dmat3(glm::mat3_cast(body.m_Orientation)))*(glm::dvec3(witness)-glm::dvec3(position));
            if (body.m_Shape->GetShapeType()==ShapeType::Sphere) return std::abs(glm::length(p)-static_cast<const ShapeSphere*>(body.m_Shape)->GetRadius())<=tolerance;
            if (body.m_Shape==&tetra) {
                const std::array<glm::dvec3,4> vertices{{{-1,-1,-1},{1,-1,-1},{0,-1,1},{0,1,0}}};
                double nearest=std::numeric_limits<double>::max();
                for (int omitted=0;omitted<4;++omitted) {
                    std::array<int,3> ids{};int n=0;for(int i=0;i<4;++i)if(i!=omitted)ids[n++]=i;
                    const auto a=vertices[ids[0]];auto normal=glm::normalize(glm::cross(vertices[ids[1]]-a,vertices[ids[2]]-a));
                    if(glm::dot(normal,vertices[omitted]-a)>0)normal=-normal;
                    const double excess=glm::dot(normal,p-a); if(excess>tolerance)return false;nearest=std::min(nearest,std::abs(excess));
                }
                return nearest<=tolerance;
            }
            const auto bounds=body.m_Shape->GetBounds(); double nearest=std::numeric_limits<double>::max();
            for (int k=0;k<3;++k) {
                if (p[k]<bounds.mins[k]-tolerance || p[k]>bounds.maxs[k]+tolerance) return false;
                nearest=std::min({nearest,std::abs(p[k]-bounds.mins[k]),std::abs(p[k]-bounds.maxs[k])});
            }
            return nearest<=tolerance;
        };
        for (unsigned id=0;id<25 && !s.failed;++id) {
            RigidBody3D a=*world[4],b=*world[5]; Vec3f expectedNormal(0,-1,0); double expectedDepth{}; float horizon=1.f/60.f;
            bool separated=false,sweep=id>=21;
            if (id<17) {
                QueryReplay::Load(b,QueryReplay::Inputs[id<13?id:2]);
                expectedDepth=1.5-b.m_Position.y;
                if (id==13) { std::swap(a,b);expectedNormal=-expectedNormal; }
                if (id==14) { a.m_Position+=Vec3f(8,4,-8);b.m_Position+=Vec3f(8,4,-8); }
                if (id==15) {
                    for (auto* body:{&a,&b}) {
                        body->m_Position=tilt*body->m_Position;body->m_Orientation=tilt*body->m_Orientation;
                        body->m_LinearVelocity=tilt*body->m_LinearVelocity;body->m_AngularVelocity=tilt*body->m_AngularVelocity;
                    }
                    expectedNormal=tilt*expectedNormal;
                }
                if (id==16) { a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);a.m_LinearVelocity={.25f,0,0};a.m_AngularVelocity={0,.5f,0}; }
            } else {
                a=RigidBody3D{};b=RigidBody3D{};a.m_Shape=&unitBox;b.m_Shape=&unitBox;
                a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);b.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);
                a.m_Elasticity=b.m_Elasticity=a.m_Friction=b.m_Friction=.5f;
                a.m_Orientation=b.m_Orientation=rotation;expectedNormal=-axis;
                b.m_Position=rotation*Vec3f(1.2f,.3f,.2f);expectedDepth=.8;
                if (id==18) a.m_Shape=b.m_Shape=&unitConvex;
                if (id==19) { a=*world[4];b.m_Shape=&tetra;b.m_Orientation=Quat(1,0,0,0);b.m_Position={0,1.25f,0};expectedNormal={0,-1,0};expectedDepth=.25; }
                if (id==20) { a.m_Shape=&unitConvex;b.m_Position=rotation*Vec3f(3,.3f,.2f);separated=true;expectedDepth=-1; }
                if (sweep) {
                    a.m_Shape=id>=23?static_cast<PhysicalShape*>(&unitConvex):static_cast<PhysicalShape*>(&unitBox);
                    a.m_Position=Vec3f(0);b.m_Position=4.f*axis;a.m_LinearVelocity=3.f*axis;b.m_LinearVelocity=-axis;
                    b.SetBodyTypeAndInverseMass(Component::BodyType::Kinematic,0);horizon=.6f;expectedDepth=0;
                    if (id==22 || id==24) {std::swap(a,b);expectedNormal=-expectedNormal;}
                }
            }
            for (auto* body:{&a,&b}) { body->SetSpinResistanceLength(0); body->SetRollingResistanceLength(0); }
            bodyInput(id,0,a);bodyInput(id,1,b);
            std::array<std::uint32_t,19> offBits{};bool offHit{};
            capture->snapshotCount=capture->attemptCount=0;capture->overflow=capture->missingBary=capture->hasBary=false;
            for (unsigned mode=0;mode<2 && !s.failed;++mode) {
                RigidBody3D qa=a,qb=b; contact_t c{}; const auto beforeA=PD::Capture(qa),beforeB=PD::Capture(qb);
                trace={};trace.caseIndex=id;trace.epa=capture.get();observer.iteration=0;observer.observationNs=0;
                PD::queryObserver=mode?&observer:nullptr;const auto start=PD::Nanoseconds();
                const bool hit=Collision::Intersect(&qa,&qb,horizon,c);const auto elapsed=PD::Nanoseconds()-start;PD::queryObserver=nullptr;++s.collisionCalls;
                const auto contactBits=QueryReplay::ContactBits(c);
                const bool pointers=c.m_BodyA==&qa && c.m_BodyB==&qb;
                const bool unchanged=QueryReplay::SameBody(beforeA,PD::Capture(qa)) && QueryReplay::SameBody(beforeB,PD::Capture(qb));
                bool geometry=separated?!hit:hit;
                if (hit) {
                    geometry=geometry && Math::IsFinite(c.normal) && std::abs(glm::length(glm::dvec3(c.normal))-1)<=1e-4;
                    const double cosine=glm::dot(glm::normalize(glm::dvec3(c.normal)),glm::normalize(glm::dvec3(expectedNormal)));
                    geometry=geometry && (sweep?glm::length(glm::dvec3(c.normal-expectedNormal))<=.002:std::acos(std::clamp(cosine,-1.,1.))<=(id<17?.001:.002));
                    geometry=geometry && (sweep?std::abs(c.timeOfImpact-.5f)<=.002f:c.timeOfImpact==0);
                    if (!sweep) geometry=geometry && std::abs(-double(c.separationDistance)-expectedDepth)<=.0045;
                    for (unsigned role=0;role<2;++role) {
                        const auto& body=role?qb:qa;const auto local=role?c.ptOnB_LocalSpace:c.ptOnA_LocalSpace;const auto witness=role?c.ptOnB_WorldSpace:c.ptOnA_WorldSpace;
                        const auto reconstructed=body.m_Position+body.GetLinearVelocity()*c.timeOfImpact+body.m_Orientation*(local+body.m_Shape->GetCenterOfMass());
                        geometry=geometry && Math::IsFinite(witness) && glm::length(glm::dvec3(reconstructed-witness))<=(sweep?2e-5:1e-4) && shapeContains(body,witness,c.timeOfImpact);
                    }
                }
                s.queryRows<<id<<','<<mode<<','<<hit<<','<<geometry<<','<<pointers<<','<<unchanged<<','<<elapsed<<','<<observer.observationNs;
                for (auto word:contactBits)s.queryRows<<','<<word;s.queryRows<<'\n';
                if (capture->overflow) s.overflow=true;
                if (!pointers || !unchanged || capture->overflow || capture->missingBary) s.Fail("conditional query identity/capture integrity");
                if (mode) { if (offHit!=hit || offBits!=contactBits) s.Fail("conditional off/on query output differs"); }
                else {offHit=hit;offBits=contactBits;}
                if (id==0 || id==11) {
                    constexpr std::array<std::uint32_t,19> c566{1061446240u,1056998163u,1069971920u,1061446261u,1055026910u,1069971924u,1061446240u,1056998163u,1069971920u,3206750400u,1060850488u,3196072454u,934662938u,3212836864u,923437173u,3178543680u,0u,0u,0u};
                    constexpr std::array<std::uint32_t,19> c589{1058937088u,1056998162u,1070283552u,1058937042u,1050983206u,1070283550u,1058937088u,1056998162u,1070283552u,3205099175u,1062431328u,3191226898u,3078686327u,3212836864u,3048440686u,3191379452u,0u,0u,0u};
                    if (!hit || contactBits!=(id==0?c566:c589)) s.Fail("conditional successful historical contact bits changed");
                }
                if (!s.candidate && id<13 && id!=0 && id!=11 &&
                    (hit || !std::all_of(contactBits.begin(),contactBits.end(),[](auto word){return word==0;})))
                    s.Fail("conditional frozen failed historical output changed");
                if (!geometry) { ++s.physicalFailures;if (s.candidate) s.Fail("conditional required query geometry"); }
            }
            if (!s.candidate && id<13 && capture->attemptCount!=((id==0 || id==11)?1u:11u))
                s.Fail("conditional frozen historical EPA attempt count changed");
            if (capture->attemptCount) {
                const auto dir=std::filesystem::path(s.config.outputDirectory)/("query-epa-"+std::to_string(id));std::error_code ec;
                if (!std::filesystem::create_directory(dir,ec) || ec || !capture->Write(dir)) s.Fail("conditional query operands export");
                for (const char* file:{"epa-projections.csv","epa-points.csv","epa-faces.csv","epa-attempts.csv"}) {
                    const auto bytes=std::filesystem::file_size(dir/file,ec);if(ec)s.Fail("conditional operand export size unavailable");else s.queryOperandExportBytes+=bytes;
                }
            }
        }
        // Exactly eight producer-contract cases, each disconnected/connected.
        ConditionalInvalidSupport bad;
        std::array<std::string,8> directSignatures{};
        for (unsigned id=0;id<8 && !s.failed;++id) for (unsigned mode=0;mode<2 && !s.failed;++mode) {
            RigidBody3D a,b;a.m_Shape=id>=6?static_cast<PhysicalShape*>(&bad):static_cast<PhysicalShape*>(&unitBox);b.m_Shape=&unitBox;
            a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);b.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);b.m_Position={1.2f,.3f,.2f};
            bad.calls=0;bad.failAt=id>=6?id-5:0;Vec3f pa(99),pb(99);GjkDiagnostics diagnostics;GjkContactStatus status=GjkContactStatus::Failed;
            const auto beforeA=PD::Capture(a),beforeB=PD::Capture(b);
            trace={};observer.iteration=0;PD::queryObserver=mode?&observer:nullptr;
            if (id<2) status=GJK_GetContact(&a,&b,.001f,pa,pb,&diagnostics,id);
            else if(id<4) GJK_ClosestPoints(&a,&b,pa,pb,&diagnostics,id-2);
            else status=GJK_GetContact(&a,&b,.001f,pa,pb,&diagnostics,GjkMaxIterations,id<6?id-4:EpaMaxIterations);
            PD::queryObserver=nullptr;++s.directCalls;
            const bool reason=id<4?diagnostics.termination==GjkTermination::IterationLimit && diagnostics.iterations==id%2:
                id<6?diagnostics.termination==GjkTermination::ContactExpansionFailed && diagnostics.epa.termination==EpaTermination::IterationLimit && diagnostics.epa.iterations==id-4:
                diagnostics.termination==GjkTermination::InvalidSupport && diagnostics.iterations==id-6;
            const bool ok=reason && status==GjkContactStatus::Failed && pa==Vec3f(0) && pb==Vec3f(0) && diagnostics.iterations<=GjkMaxIterations && diagnostics.epa.iterations<=EpaMaxIterations &&
                QueryReplay::SameBody(beforeA,PD::Capture(a)) && QueryReplay::SameBody(beforeB,PD::Capture(b));
            std::ostringstream signature;signature<<int(status)<<','<<int(diagnostics.termination)<<','<<diagnostics.iterations<<','<<int(diagnostics.epa.termination)<<','<<diagnostics.epa.iterations;
            for (auto v:{pa,pb}) for(int k=0;k<3;++k) signature<<','<<std::bit_cast<std::uint32_t>(v[k]);
            if (mode) {if(signature.str()!=directSignatures[id])s.Fail("conditional direct GJK off/on mismatch");}else directSignatures[id]=signature.str();
            s.helperRows<<"{\"kind\":\"gjk\",\"case\":"<<id<<",\"mode\":"<<mode<<",\"pass\":"<<(ok?"true":"false")<<",\"status\":"<<int(status)<<",\"termination\":"<<int(diagnostics.termination)<<",\"iterations\":"<<diagnostics.iterations<<",\"epa_termination\":"<<int(diagnostics.epa.termination)<<",\"epa_iterations\":"<<diagnostics.epa.iterations<<",\"a\":";
            ConditionalJsonVector(s.helperRows,pa);s.helperRows<<",\"b\":";ConditionalJsonVector(s.helperRows,pb);s.helperRows<<"}\n";
            if(!ok)s.Fail("conditional direct GJK producer contract");
        }
        std::array<std::string,6> caSignatures{};
        for (unsigned id=0;id<6 && !s.failed;++id) for (unsigned mode=0;mode<2 && !s.failed;++mode) {
            RigidBody3D a=*world[4],b=*world[5];QueryReplay::Load(b,QueryReplay::Inputs[2]);
            for (auto* body:{&a,&b}) { body->SetSpinResistanceLength(0); body->SetRollingResistanceLength(0); }
            const auto ba=PD::Capture(a),bb=PD::Capture(b);
            trace={};PD::queryObserver=mode?&observer:nullptr;const auto result=PD::EvaluateCaContract(id,&a,&b);PD::queryObserver=nullptr;++s.caCalls;
            constexpr unsigned beforeAdvances[]{1,2,1,1,1,0},afterAdvances[]{0,1,0,0,0,0};
            constexpr unsigned queryCounts[]{1,2,1,1,1,0};constexpr int outcomes[]{2,2,2,2,1,-1},reasons[]{7,7,8,10,0,0},beforeExits[]{0,0,0,0,0,6},afterExits[]{7,7,8,10,10,6};
            constexpr float beforeRemaining[]{.75f,.5f,1,1,1,0},afterRemaining[]{1,.75f,1,1,1,0},beforeToi[]{.25f,.5f,0,0,1,0},afterToi[]{0,.25f,0,0,1,0};
            constexpr GjkTermination rawReasons[]{GjkTermination::ContactExpansionFailed,GjkTermination::ContactExpansionFailed,
                GjkTermination::InvalidSupport,GjkTermination::DuplicateSupport,GjkTermination::DuplicateSupport,GjkTermination::InvalidInput};
            const bool raw=result.diagnostics.termination==rawReasons[id] && result.diagnostics.iterations==0 &&
                result.diagnostics.epa.termination==(id<2?EpaTermination::InvalidProjection:EpaTermination::NotRun) && result.diagnostics.epa.iterations==0;
            const bool intact=QueryReplay::SameBody(ba,PD::Capture(a)) && QueryReplay::SameBody(bb,PD::Capture(b));
            const auto words=QueryReplay::ContactBits(result.contact);
            const bool cleared=std::all_of(words.begin(),words.end(),[](auto word){return word==0;});
            const bool pointers=result.contact.m_BodyA==&a && result.contact.m_BodyB==&b;
            const bool unadvanced=QueryReplay::SameBody(ba,result.predictedA) && QueryReplay::SameBody(bb,result.predictedB);
            const bool behavior=raw && result.advances==(s.candidate?afterAdvances[id]:beforeAdvances[id]) && result.queries==queryCounts[id] &&
                result.queryOutcome==outcomes[id] && int(result.queryReason)==reasons[id] && int(result.exit)==(s.candidate?afterExits[id]:beforeExits[id]) &&
                result.remaining==(s.candidate?afterRemaining[id]:beforeRemaining[id]) && result.toi==(s.candidate?afterToi[id]:beforeToi[id]);
            const bool requirement=result.advances==afterAdvances[id] && intact && pointers && cleared && (id==1 || unadvanced);
            std::ostringstream signature;signature<<result.queryOutcome<<','<<int(result.queryReason)<<','<<int(result.exit)<<','<<result.queries<<','<<result.advances<<','<<std::bit_cast<std::uint32_t>(result.remaining)<<','<<std::bit_cast<std::uint32_t>(result.toi);
            signature<<','<<int(result.diagnostics.termination)<<','<<result.diagnostics.iterations<<','<<int(result.diagnostics.epa.termination)<<','<<result.diagnostics.epa.iterations;
            for(auto word:words)signature<<','<<word;
            for(const auto& state:{result.predictedA,result.predictedB}) {for(auto word:QueryReplay::PoseBits(state))signature<<','<<word;signature<<','<<state.sleeping<<','<<std::bit_cast<std::uint64_t>(state.inactive);}
            if(mode){if(signature.str()!=caSignatures[id])s.Fail("conditional CA off/on mismatch");}else caSignatures[id]=signature.str();
            s.helperRows<<"{\"kind\":\"ca\",\"case\":"<<id<<",\"mode\":"<<mode<<",\"pass\":"<<(requirement?"true":"false")<<",\"expected_build_behavior\":"<<(behavior?"true":"false")<<",\"unchanged\":"<<(intact?"true":"false")<<",\"outcome\":"<<result.queryOutcome<<",\"reason\":"<<int(result.queryReason)<<",\"exit\":"<<int(result.exit)<<",\"queries\":"<<result.queries<<",\"advances\":"<<result.advances<<",\"remaining\":"<<result.remaining<<",\"toi\":"<<result.toi<<",\"gjk_termination\":"<<int(result.diagnostics.termination)<<",\"epa_termination\":"<<int(result.diagnostics.epa.termination)<<",\"contact_bits\":[";
            for(unsigned i=0;i<words.size();++i)s.helperRows<<(i?",":"")<<words[i];s.helperRows<<"],\"predicted_a_bits\":[";
            const auto predictedA=QueryReplay::PoseBits(result.predictedA),predictedB=QueryReplay::PoseBits(result.predictedB);
            for(unsigned i=0;i<predictedA.size();++i)s.helperRows<<(i?",":"")<<predictedA[i];s.helperRows<<"],\"predicted_b_bits\":[";
            for(unsigned i=0;i<predictedB.size();++i)s.helperRows<<(i?",":"")<<predictedB[i];
            s.helperRows<<"],\"predicted_a_sleeping\":"<<(result.predictedA.sleeping?"true":"false")<<",\"predicted_b_sleeping\":"<<(result.predictedB.sleeping?"true":"false")
                <<",\"predicted_a_inactive_bits\":"<<std::bit_cast<std::uint64_t>(result.predictedA.inactive)<<",\"predicted_b_inactive_bits\":"<<std::bit_cast<std::uint64_t>(result.predictedB.inactive)
                <<",\"gjk_iterations\":"<<result.diagnostics.iterations<<",\"epa_iterations\":"<<result.diagnostics.epa.iterations
                <<",\"pointers_ok\":"<<(pointers?"true":"false")<<",\"outputs_cleared\":"<<(cleared?"true":"false")<<",\"unadvanced_state\":"<<(unadvanced?"true":"false")<<"}\n";
            if(!intact || !behavior || (s.candidate && !requirement))s.Fail("conditional CA helper contract differs from frozen build behavior");
            if(!requirement)++s.physicalFailures;
        }
        std::array<std::string,2> projectionSignatures{};
        for (unsigned id=0;id<2 && !s.failed;++id) for(unsigned mode=0;mode<2 && !s.failed;++mode) {
            trace={};PD::queryObserver=mode?&observer:nullptr;const auto result=PD::EvaluateProjectionPrimitive(id);PD::queryObserver=nullptr;++s.projectionCalls;
            const Vec3f expectedWeights=id?Vec3f(.25f,.5f,.25f):Vec3f(.5f,.25f,.25f);
            const bool recovered=(result.termination==EpaTermination::Converged || result.termination==EpaTermination::DuplicateSupport) &&
                result.selectedFace==(id?0:1) && result.normal==Vec3f(0,1,0) && result.originDistance==-.125f &&
                result.projectedPoint==Vec3f(0,.125f,0) && result.expectedProjection==Vec3f(0,.125f,0) && result.lambdaSum==1 &&
                result.projectionErrorSquared==0 && result.projectionScaleSquared==1 && result.penetrationDistance==.125f &&
                result.lambdas==expectedWeights && result.witnessA==Vec3f(0,.125f,0) && result.witnessB==Vec3f(0) && result.failedPredicates==0;
            const bool baseline=result.termination==EpaTermination::InvalidProjection && result.selectedFace==0 &&
                result.lambdas==(id?Vec3f(1,0,0):Vec3f(.75f,.5f,-.25f)) && result.failedPredicates==(id?128u:32u) && result.witnessA==Vec3f(0) && result.witnessB==Vec3f(0);
            std::ostringstream signature;signature<<int(result.termination)<<','<<result.selectedFace<<','<<result.failedPredicates;
            for(auto v:{result.normal,result.lambdas,result.projectedPoint,result.expectedProjection,result.witnessA,result.witnessB})for(int k=0;k<3;++k)signature<<','<<std::bit_cast<std::uint32_t>(v[k]);
            for(float v:{result.originDistance,result.lambdaSum,result.projectionErrorSquared,result.projectionScaleSquared,result.penetrationDistance})signature<<','<<std::bit_cast<std::uint32_t>(v);
            if(mode){if(signature.str()!=projectionSignatures[id])s.Fail("conditional projection off/on mismatch");}else projectionSignatures[id]=signature.str();
            s.helperRows<<"{\"kind\":\"projection\",\"case\":"<<id<<",\"mode\":"<<mode<<",\"pass\":"<<(recovered?"true":"false")<<",\"termination\":"<<int(result.termination)<<",\"selected_face\":"<<result.selectedFace<<",\"failed_predicates\":"<<result.failedPredicates<<",\"weights\":";
            ConditionalJsonVector(s.helperRows,result.lambdas);s.helperRows<<",\"projection\":";ConditionalJsonVector(s.helperRows,result.projectedPoint);s.helperRows<<",\"witness_a\":";ConditionalJsonVector(s.helperRows,result.witnessA);s.helperRows<<",\"witness_b\":";ConditionalJsonVector(s.helperRows,result.witnessB);
            s.helperRows<<",\"normal\":";ConditionalJsonVector(s.helperRows,result.normal);s.helperRows<<",\"expected_projection\":";ConditionalJsonVector(s.helperRows,result.expectedProjection);
            s.helperRows<<",\"origin_distance\":"<<result.originDistance<<",\"lambda_sum\":"<<result.lambdaSum<<",\"penetration_distance\":"<<result.penetrationDistance
                <<",\"error_squared\":"<<result.projectionErrorSquared<<",\"scale_squared\":"<<result.projectionScaleSquared<<"}\n";
            if(!(s.candidate?recovered:baseline))s.Fail("conditional projection primitive frozen requirement");
            if(!recovered)++s.physicalFailures;
        }
        PD::queryObserver=nullptr;
        for(unsigned i=0;i<world.size();++i)if(!QueryReplay::SameBody(liveBefore[i],PD::Capture(*world[i])))s.Fail("conditional queries changed live Scene");
        if (s.scene->GetPhysicsTiming().totalSteps!=0) s.Fail("conditional query work advanced Scene");
        s.queriesComplete=!s.failed && s.collisionCalls==50 && s.directCalls==16 && s.caCalls==12 && s.projectionCalls==4;
        return s.queriesComplete;
    }

}
