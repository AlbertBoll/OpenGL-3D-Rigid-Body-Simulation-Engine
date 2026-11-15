#pragma once
#include <cstdint>
#include <string_view>

namespace GEngine
{
    enum class PhysicsBackend : std::uint8_t { Native, Jolt };
    constexpr std::string_view PhysicsBackendName(PhysicsBackend backend) noexcept
    {
        switch (backend) { case PhysicsBackend::Native: return "Native"; case PhysicsBackend::Jolt: return "Jolt"; }
        return "Unsupported";
    }
    [[nodiscard]] bool PhysicsBackendAvailable(PhysicsBackend) noexcept;
    // Authored resistance lengths remain readable in PhysicsMaterial. The Owner
    // accepted Jolt's native laws: those two fields do not drive Jolt contacts.
    constexpr std::string_view PhysicsBackendModelNote(PhysicsBackend backend) noexcept
    {
        return backend == PhysicsBackend::Jolt
            ? "Jolt 5.6.0: native friction/sleep/CCD; authored spin and rolling resistance lengths are retained but unsupported. No added damping."
            : "Native physics";
    }
}
