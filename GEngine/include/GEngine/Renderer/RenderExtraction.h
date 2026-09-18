#pragma once

#include "Renderer/RenderFrame.h"
#include "Scene/_Scene.h"
#include <variant>

namespace GEngine
{
    struct RenderExtractionStats
    {
        std::size_t sceneEntities{}, candidates{}, disabled{}, draws{};
        FrameStorageAccounting frameStorage;
        // Preparation includes presentation/bounds evaluation and resource resolution.
        // Extraction covers frozen ECS reads, frame allocation, emission and finalization.
        // frameStorage counts only the new frame arrays, not preparation/cache/packet
        // allocations. Allocator-level validation reports the complete call separately.
        double preparationMicroseconds{}, extractionMicroseconds{};
    };
    using RenderExtractionCause = std::variant<TransformError, RenderEcsError,
        Asset::RegistryError, MaterialBindingError, FrameError>;
    struct RenderExtractionError
    {
        EntityRenderId entity; // Invalid for a scene-wide error; TransformError retains UUID context.
        RenderExtractionCause cause;
    };

    // Owner-thread serial stage. Finish authoring and asset publication first; the
    // supplied FrameAccess must cover this entire call and CPU submission. It blocks
    // publication/replacement, so preparation and emission see the same ready versions.
    // This entry point creates its own snapshot: a prior-frame snapshot cannot enter.
    // It updates only the scene's existing presentation/revision caches, then freezes
    // ECS reads. No GL, manager mutation, simulation update, sorting or culling occurs.
    //
    // One enabled MeshRendererComponent produces its selected submesh/material draw
    // in deterministic hierarchy order. A multi-submesh asset uses one component per
    // pairing, as defined by the ECS contract. Visibility defaults to enabled/all layers;
    // zero layer masks are preserved for the later visibility stage. Any enabled stale
    // resource or invalid submesh fails the complete frame, with entity/cause context.
    // Camera/debug values are supplied in caller order (the existing RenderFrame contract).
    // Retain the returned frame through submission and its registries through retirement.
    [[nodiscard]] std::expected<RenderFrame, RenderExtractionError> ExtractRenderFrame(
        _Scene&, const RenderStateResources&, RenderExtractionStats&,
        std::span<const FrameCamera> cameras = {}, std::span<const FrameDebugLine> debugLines = {});
}
