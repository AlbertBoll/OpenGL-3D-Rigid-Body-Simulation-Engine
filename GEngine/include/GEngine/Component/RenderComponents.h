#pragma once

#include "Assets/AssetHandle.h"
#include <array>
#include <concepts>
#include <cstdint>

namespace GEngine
{
    struct EntityRenderTag;
    // The shared identity model: 32-bit slot, 64-bit generation, 64-bit domain.
    // Default construction is invalid. This is not a packed picking pixel.
    using EntityRenderId = Asset::AssetHandle<EntityRenderTag>;

    namespace Component
    {
        // One mesh submesh/material pairing. Resources are resolved after publication,
        // outside ECS; components neither own GPU resources nor bind textures/programs.
        struct MeshRendererComponent
        {
            Asset::MeshHandle mesh;
            Asset::MaterialInstanceHandle material;
            std::uint32_t submesh = 0;
            bool castShadows = true;
            bool receiveShadows = true;
            bool pickable = true;
        };

        struct VisibilityComponent
        {
            bool enabled = true;
            std::uint32_t layers = ~std::uint32_t{0};
        };

        enum class CameraProjection { Perspective, Orthographic };
        // Projection intent only. Pose comes from the entity transform; viewport
        // dimensions, matrices, camera selection and target ownership belong to consumers.
        struct RenderCameraComponent
        {
            CameraProjection projection = CameraProjection::Perspective;
            float verticalFovRadians = 0.785398163f;
            float orthographicHeight = 10.f;
            float nearPlane = 0.1f;
            float farPlane = 1000.f;
            std::uint32_t visibleLayers = ~std::uint32_t{0};
            bool primary = true;
        };

        enum class RenderLightKind { Directional, Point, Spot };
        // Linear color and authored intensity/range/cone intent. Pose is supplied by
        // the transform. Shadow targets, uniforms and light ordering are renderer work.
        struct RenderLightComponent
        {
            RenderLightKind kind = RenderLightKind::Directional;
            std::array<float, 3> color{1.f, 1.f, 1.f};
            float intensity = 1.f;
            float range = 10.f;
            float innerConeRadians = 0.4f;
            float outerConeRadians = 0.6f;
            bool castShadows = false;
        };

        template<class T>
        concept RenderDataComponent = std::same_as<T, MeshRendererComponent>
            || std::same_as<T, VisibilityComponent> || std::same_as<T, RenderCameraComponent>
            || std::same_as<T, RenderLightComponent>;
    }
}
