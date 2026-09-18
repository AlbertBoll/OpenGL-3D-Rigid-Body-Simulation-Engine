#pragma once

#include "Renderer/RenderFrame.h"
#include "Scene/_Scene.h"
#include <variant>

namespace GEngine
{
    struct RenderExtractionStats
    {
        std::size_t sceneEntities{}, candidates{}, disabled{}, draws{};
        std::size_t lightCandidates{}, nonContributingLights{}, directionalLights{}, pointLights{}, spotLights{};
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
    // RenderLightComponent presence publishes a light, independent of actor visibility,
    // mesh presence and the legacy uniform light lists. Zero intensity contributes nothing;
    // negative intensity/color or unknown kind fails. Existing preparation rejects any
    // nonfinite authored light field/transform, even for a zero-intensity light. Positive
    // intensity lights require finite positive point/spot range and ordered spot half-angles.
    // Pose uses the presentation hierarchy: translation is position, transformed local -Z
    // is ray direction (normalized with double precision; a collapsed axis fails). Range
    // stays an authored world distance, unaffected by scale. Cones are radians as defined
    // by SpotLightData. There is no lighting equation, attenuation or shadow algorithm here.
    // All contributing lights share MaxFrameLights; overflow fails the whole frame without
    // truncation. Each typed collection keeps deterministic hierarchy order. Per-entity and
    // aggregate light revisions include pose/intent/contribution/removal changes. Shadow
    // intent is castShadows; targets/bias/filtering stay renderer-owned. Light authoring is
    // read once by preparation; consumers receive only immutable values, never ECS borrows.
    // Retain the returned frame through submission and its registries through retirement.
    [[nodiscard]] std::expected<RenderFrame, RenderExtractionError> ExtractRenderFrame(
        _Scene&, const RenderStateResources&, RenderExtractionStats&,
        std::span<const FrameCamera> cameras = {}, std::span<const FrameDebugLine> debugLines = {});
}
