#pragma once
// Read-only boundary instrumentation. Absent from ordinary products.
#ifdef GENGINE_JOLT_FINAL_AUDIT
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
namespace GEngine::JoltAudit {
inline std::array<std::uint64_t,5> ns{}; // Scene, PhysicsSystem, adapter pre, Update, adapter post
inline std::atomic<bool> countAllocations{false};
inline std::atomic<std::uint64_t> cppCalls{},cppBytes{},joltCalls{},joltBytes{};
inline std::uint64_t tempPeak{},tempCalls{},tempBytes{},bodyVisits{};
inline std::uint64_t Now() noexcept {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct Scope {unsigned id;std::uint64_t start=Now();explicit Scope(unsigned i):id(i){}~Scope(){ns[id]+=Now()-start;}};
inline void Reset() {ns={};cppCalls=0;cppBytes=0;joltCalls=0;joltBytes=0;tempPeak=tempCalls=tempBytes=bodyVisits=0;}
inline void Alloc(std::size_t bytes,bool jolt=false) noexcept {
    if(countAllocations.load(std::memory_order_relaxed)) {
        (jolt?joltCalls:cppCalls).fetch_add(1,std::memory_order_relaxed);
        (jolt?joltBytes:cppBytes).fetch_add(bytes,std::memory_order_relaxed);
    }
}
}
#endif
