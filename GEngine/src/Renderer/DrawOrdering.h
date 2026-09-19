#pragma once

#include "Renderer/RenderFrame.h"
#include <algorithm>
#include <tuple>

namespace GEngine::RenderDetail
{
    // One pass partition; only caller-owned frame-local ordinals change.
    // Opaque/masked write depth: pipeline -> material -> mesh/submesh.
    // Full typed identities include registry/generation. Native names, pointers,
    // resource-table ordinals and advisory sortKey never determine ordering.
    inline void SortLocality(std::span<std::size_t> indices, std::span<const DrawItem> draws)
    {
        auto key = [&](std::size_t i) {
            const auto& draw = draws[i];
            return std::tuple(draw.pipeline, draw.material, draw.mesh,
                draw.submesh.firstElement, draw.submesh.elementCount, draw.submesh.materialSlot, i);
        };
        if (indices.size() > 1)
            std::sort(indices.begin(), indices.end(), [&](auto a, auto b) { return key(a) < key(b); });
    }

    // Camera-space transformed origin, far (-Z) to near, source-order ties.
    // Double intermediates keep finite Float32 camera/pose products finite.
    // Object ordering cannot solve intersecting surfaces or triangle ordering.
    inline void SortTransparent(std::span<std::size_t> indices,
        std::span<const DrawItem> draws, const FrameCamera& camera)
    {
        const glm::dmat4 view(camera.view);
        auto depth = [&](std::size_t i) { return (view * glm::dvec4(draws[i].worldTransform[3])).z; };
        if (indices.size() > 1)
            std::sort(indices.begin(), indices.end(), [&](auto a, auto b) {
                const auto za = depth(a), zb = depth(b);
                return za == zb ? a < b : za < zb;
            });
    }
}
