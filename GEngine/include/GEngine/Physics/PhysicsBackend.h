#pragma once
#include <cstdint>
#include <string_view>

namespace GEngine
{
    enum class PhysicsBackend : std::uint8_t { Native, Jolt };
    inline constexpr PhysicsBackend DefaultPhysicsBackend = PhysicsBackend::Jolt;
    constexpr std::string_view PhysicsBackendName(PhysicsBackend backend) noexcept
    {
        switch (backend) { case PhysicsBackend::Native: return "Native"; case PhysicsBackend::Jolt: return "Jolt"; }
        return "Unsupported";
    }
    [[nodiscard]] bool PhysicsBackendAvailable(PhysicsBackend) noexcept;
    // Describe the pinned provider. Each scene owns its editable material fields.
    constexpr std::string_view PhysicsBackendModelNote(PhysicsBackend backend) noexcept
    {
        return backend == PhysicsBackend::Jolt
            ? "Jolt 5.6.0 with native friction, sleeping and continuous collision detection."
            : "Native physics";
    }
}
