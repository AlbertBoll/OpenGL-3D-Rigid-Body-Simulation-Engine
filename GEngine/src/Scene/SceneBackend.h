#pragma once
// Backend-only retained grouping inspection; program names are not engine identities.
#include "Scene/_Scene.h"
namespace GEngine::SceneDetail
{
    struct BackendAccess
    {
        static auto& Groups(_Scene& scene) { return scene.GetGroupEntities(); }
        static auto& Lights(_Scene& scene) { return scene.GetLightEntities(); }
    };
}
