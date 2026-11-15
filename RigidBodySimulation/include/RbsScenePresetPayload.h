#pragma once
#include "RbsLaunchConfig.h"
#include <cstdint>
#include <cstring>

namespace Rbs
{
    inline constexpr char ScenePresetPayloadType[]="RBS_SCENE_PRESET_V1";
    struct ScenePresetPayload { std::uint32_t version{1}; SceneSelection selection; };
    inline std::optional<SceneSelection> DecodeScenePreset(const void* data,std::size_t size)
    {
        if(!data || size!=sizeof(ScenePresetPayload))return {};
        ScenePresetPayload payload;std::memcpy(&payload,data,size);
        if(payload.version!=1 || !payload.selection.IsValid())return {};
        return payload.selection;
    }
}
