#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GEngine
{
    enum class PhysicsShapeErrorCode { InvalidRadius, InvalidPointSet, Allocation };
    struct PhysicsShapeError
    {
        PhysicsShapeErrorCode code;
        std::string_view operation;
        std::string_view message;
        float radius = 0.0f;
        std::size_t pointCount = 0;
        std::uint64_t entity = 0;
    };
}
