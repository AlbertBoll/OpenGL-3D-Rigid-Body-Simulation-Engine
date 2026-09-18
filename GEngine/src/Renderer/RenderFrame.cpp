#include "gepch.h"
#include "Renderer/RenderFrame.h"
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace GEngine
{
    namespace
    {
        using Code = FrameErrorCode;
        using Section = FrameSection;
        std::unexpected<FrameError> Error(Code code, Section section, std::size_t element = 0) noexcept
        { return std::unexpected(FrameError{code, section, element}); }

        bool Finite(const glm::mat4& matrix) noexcept
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix[column][row])) return false;
            return true;
        }
        bool Finite(const glm::vec3& value) noexcept
        { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
        std::size_t Bytes(FrameCapacity count) noexcept
        {
            return count.cameras * sizeof(FrameCamera) + count.draws * sizeof(DrawItem)
                + count.debugLines * sizeof(FrameDebugLine) + count.resources * sizeof(FrameResources);
        }
    }

    void RenderFrame::Swap(RenderFrame& other) noexcept
    {
        using std::swap;
        swap(m_Cameras, other.m_Cameras); swap(m_Draws, other.m_Draws);
        swap(m_Debug, other.m_Debug); swap(m_Resources, other.m_Resources);
        swap(m_Size, other.m_Size); swap(m_Capacity, other.m_Capacity);
    }
    RenderFrame::RenderFrame(RenderFrame&& other) noexcept { Swap(other); }
    RenderFrame& RenderFrame::operator=(RenderFrame&& other) noexcept
    {
        if (this != &other) { RenderFrame old(std::move(other)); Swap(old); }
        return *this;
    }
    FrameStorageAccounting RenderFrame::Storage() const noexcept
    {
        return {Bytes(m_Size), Bytes(m_Capacity), std::size_t(bool(m_Cameras))
            + bool(m_Draws) + bool(m_Debug) + bool(m_Resources)};
    }
    RenderFrameBuilder::RenderFrameBuilder(RenderFrameBuilder&& other) noexcept
        : m_Frame(std::move(other.m_Frame)), m_Finalized(std::exchange(other.m_Finalized, true)) {}
    RenderFrameBuilder& RenderFrameBuilder::operator=(RenderFrameBuilder&& other) noexcept
    {
        if (this != &other)
        {
            m_Frame = std::move(other.m_Frame);
            m_Finalized = std::exchange(other.m_Finalized, true);
        }
        return *this;
    }
    std::expected<RenderFrameBuilder, FrameError> RenderFrameBuilder::Create(FrameCapacity capacity) noexcept
    {
        const std::size_t counts[]{capacity.cameras, capacity.draws, capacity.debugLines, capacity.resources};
        const std::size_t widths[]{sizeof(FrameCamera), sizeof(DrawItem), sizeof(FrameDebugLine), sizeof(FrameResources)};
        const Section sections[]{Section::Cameras, Section::Draws, Section::DebugLines, Section::Resources};
        auto remaining = static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)());
        for (std::size_t i = 0; i < 4; ++i)
        {
            if (counts[i] > remaining / widths[i]) return Error(Code::CapacityOverflow, sections[i], counts[i]);
            remaining -= counts[i] * widths[i];
        }
        RenderFrameBuilder builder;
        auto& frame = builder.m_Frame;
        if (capacity.cameras)
        {
            frame.m_Cameras.reset(new (std::nothrow) FrameCamera[capacity.cameras]);
            if (!frame.m_Cameras) return Error(Code::AllocationFailed, Section::Cameras, capacity.cameras);
        }
        if (capacity.draws)
        {
            frame.m_Draws.reset(new (std::nothrow) DrawItem[capacity.draws]);
            if (!frame.m_Draws) return Error(Code::AllocationFailed, Section::Draws, capacity.draws);
        }
        if (capacity.debugLines)
        {
            frame.m_Debug.reset(new (std::nothrow) FrameDebugLine[capacity.debugLines]);
            if (!frame.m_Debug) return Error(Code::AllocationFailed, Section::DebugLines, capacity.debugLines);
        }
        if (capacity.resources)
        {
            frame.m_Resources.reset(new (std::nothrow) FrameResources[capacity.resources]);
            if (!frame.m_Resources) return Error(Code::AllocationFailed, Section::Resources, capacity.resources);
        }
        frame.m_Capacity = capacity;
        return builder;
    }
    std::expected<void, FrameError> RenderFrameBuilder::AddCamera(const FrameCamera& camera) noexcept
    {
        if (m_Finalized) return Error(Code::Finalized, Section::Cameras);
        const auto index = m_Frame.m_Size.cameras;
        if (index == m_Frame.m_Capacity.cameras) return Error(Code::CapacityExceeded, Section::Cameras, index);
        const auto limit = (std::numeric_limits<std::uint32_t>::max)();
        if (!camera.entity || !Finite(camera.view) || !Finite(camera.projection) || !Finite(camera.worldPosition)
            || !camera.viewportWidth || !camera.viewportHeight
            || camera.viewportWidth > limit - camera.viewportX || camera.viewportHeight > limit - camera.viewportY)
            return Error(Code::InvalidCamera, Section::Cameras, index);
        m_Frame.m_Cameras[index] = camera;
        ++m_Frame.m_Size.cameras;
        return {};
    }
    std::expected<std::size_t, FrameError> RenderFrameBuilder::AddResources(
        const MeshView& mesh, PreparedMaterialBinding&& material) noexcept
    {
        if (m_Finalized) return Error(Code::Finalized, Section::Resources);
        const auto index = m_Frame.m_Size.resources;
        if (index == m_Frame.m_Capacity.resources) return Error(Code::CapacityExceeded, Section::Resources, index);
        if (!mesh || !static_cast<bool>(*mesh) || !material.Source() || !material.Program())
            return Error(Code::InvalidResources, Section::Resources, index);
        auto& entry = m_Frame.m_Resources[index];
        entry.m_Mesh = mesh;
        entry.m_Material.emplace(std::move(material));
        ++m_Frame.m_Size.resources;
        return index;
    }
    std::expected<void, FrameError> RenderFrameBuilder::AddDraw(const FrameDrawDesc& desc) noexcept
    {
        if (m_Finalized) return Error(Code::Finalized, Section::Draws);
        const auto index = m_Frame.m_Size.draws;
        if (index == m_Frame.m_Capacity.draws) return Error(Code::CapacityExceeded, Section::Draws, index);
        if (desc.resources >= m_Frame.m_Size.resources) return Error(Code::InvalidResources, Section::Draws, desc.resources);
        if (!desc.entity || !Finite(desc.worldTransform) || desc.worldTransform[0][3] != 0.f
            || desc.worldTransform[1][3] != 0.f || desc.worldTransform[2][3] != 0.f || desc.worldTransform[3][3] != 1.f)
            return Error(Code::InvalidDraw, Section::Draws, index);
        const auto& entry = m_Frame.m_Resources[desc.resources];
        const auto ranges = entry.Mesh()->Submeshes();
        if (desc.submesh >= ranges.size()) return Error(Code::InvalidSubmesh, Section::Draws, desc.submesh);
        m_Frame.m_Draws[index] = {entry.Mesh().Identity(), ranges[desc.submesh],
            entry.Material().Source()->Declaration()->Pipeline(), entry.Material().Instance(),
            desc.worldTransform, desc.entity, desc.sortKey, desc.resources, desc.layers,
            desc.castShadows && entry.Material().Source()->Declaration()->Key().castsShadow,
            desc.receiveShadows, desc.pickable};
        ++m_Frame.m_Size.draws;
        return {};
    }
    std::expected<void, FrameError> RenderFrameBuilder::AddDebugLine(const FrameDebugLine& line) noexcept
    {
        if (m_Finalized) return Error(Code::Finalized, Section::DebugLines);
        const auto index = m_Frame.m_Size.debugLines;
        if (index == m_Frame.m_Capacity.debugLines) return Error(Code::CapacityExceeded, Section::DebugLines, index);
        if (!Finite(line.start) || !Finite(line.end) || !Finite(glm::vec3(line.color))
            || !std::isfinite(line.color.a) || line.color.a < 0.f || line.color.a > 1.f
            || (line.depth != DebugDepth::Tested && line.depth != DebugDepth::Overlay))
            return Error(Code::InvalidDebugLine, Section::DebugLines, index);
        m_Frame.m_Debug[index] = line;
        ++m_Frame.m_Size.debugLines;
        return {};
    }
    std::expected<RenderFrame, FrameError> RenderFrameBuilder::Finalize() && noexcept
    {
        if (m_Finalized) return Error(Code::Finalized, Section::Frame);
        m_Finalized = true;
        return std::move(m_Frame);
    }
}
