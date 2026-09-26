#pragma once
#include <cstdint>
#include <string_view>
#include <optional>
#include "Core/UUID.h"

namespace GEngine
{
	enum class TransformErrorCode
	{
		InvalidEntity, ForeignEntity, InvalidParent, Cycle, MissingTransform,
		NonFiniteTransform, InvalidRotation, IdentityExhausted, NonFiniteRenderData,
		AllocationFailed, CapacityOverflow
	};
	struct TransformError
	{
		TransformErrorCode code;
		UUID entity{0};
		UUID parent{0};
	};
    enum class SceneErrorCode { ForeignEntity, MissingIdentity, Parenting, InvalidIdentity, InvalidProgram, InvalidScene };
    struct SceneError
    {
        SceneErrorCode code;
        std::string_view operation;
        std::string_view message;
        std::uint64_t entity{};
        std::optional<TransformError> transform;
    };
}
