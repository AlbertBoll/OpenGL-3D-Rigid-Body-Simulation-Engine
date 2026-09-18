#include "gepch.h"
#include "Renderer/RenderFrame.h"
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
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
        bool LightValues(EntityRenderId entity, std::uint64_t revision, const glm::vec3& color, float intensity) noexcept
        {
            return entity && revision && Finite(color) && color.x >= 0 && color.y >= 0 && color.z >= 0
                && std::isfinite(intensity) && intensity > 0;
        }
        bool UnitDirection(const glm::vec3& direction) noexcept
        { return Finite(direction) && std::abs(glm::dot(direction, direction) - 1.f) <= 1.e-5f; }
        bool Range(float range) noexcept { return std::isfinite(range) && range > 0; }
        std::size_t Bytes(FrameCapacity count) noexcept
        {
            return count.cameras * sizeof(FrameCamera) + count.draws * sizeof(DrawItem)
                + count.debugLines * sizeof(FrameDebugLine) + count.resources * sizeof(FrameResources)
                + count.directionalLights * sizeof(DirectionalLightData) + count.pointLights * sizeof(PointLightData)
                + count.spotLights * sizeof(SpotLightData);
        }
    }

    void RenderFrame::Swap(RenderFrame& other) noexcept
    {
        using std::swap;
        swap(m_Cameras, other.m_Cameras); swap(m_Draws, other.m_Draws);
        swap(m_Debug, other.m_Debug); swap(m_Resources, other.m_Resources);
        swap(m_Directional, other.m_Directional); swap(m_Point, other.m_Point); swap(m_Spot, other.m_Spot);
        swap(m_LightRevision, other.m_LightRevision);
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
            + bool(m_Draws) + bool(m_Debug) + bool(m_Resources) + bool(m_Directional) + bool(m_Point) + bool(m_Spot)};
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
    std::expected<RenderFrameBuilder, FrameError> RenderFrameBuilder::Create(FrameCapacity capacity, std::uint64_t lightRevision) noexcept
    {
        if (capacity.directionalLights > MaxFrameLights || capacity.pointLights > MaxFrameLights - capacity.directionalLights
            || capacity.spotLights > MaxFrameLights - capacity.directionalLights - capacity.pointLights)
            return Error(Code::LightLimitExceeded, Section::Lights, MaxFrameLights);
        const std::size_t counts[]{capacity.cameras, capacity.draws, capacity.debugLines, capacity.resources,
            capacity.directionalLights, capacity.pointLights, capacity.spotLights};
        const std::size_t widths[]{sizeof(FrameCamera), sizeof(DrawItem), sizeof(FrameDebugLine), sizeof(FrameResources),
            sizeof(DirectionalLightData), sizeof(PointLightData), sizeof(SpotLightData)};
        const Section sections[]{Section::Cameras, Section::Draws, Section::DebugLines, Section::Resources,
            Section::DirectionalLights, Section::PointLights, Section::SpotLights};
        auto remaining = static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)());
        for (std::size_t i = 0; i < std::size(counts); ++i)
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
        if (capacity.directionalLights)
        {
            frame.m_Directional.reset(new (std::nothrow) DirectionalLightData[capacity.directionalLights]);
            if (!frame.m_Directional) return Error(Code::AllocationFailed, Section::DirectionalLights, capacity.directionalLights);
        }
        if (capacity.pointLights)
        {
            frame.m_Point.reset(new (std::nothrow) PointLightData[capacity.pointLights]);
            if (!frame.m_Point) return Error(Code::AllocationFailed, Section::PointLights, capacity.pointLights);
        }
        if (capacity.spotLights)
        {
            frame.m_Spot.reset(new (std::nothrow) SpotLightData[capacity.spotLights]);
            if (!frame.m_Spot) return Error(Code::AllocationFailed, Section::SpotLights, capacity.spotLights);
        }
        frame.m_Capacity = capacity;
        frame.m_LightRevision = lightRevision;
        return builder;
    }
    std::expected<void, FrameError> RenderFrameBuilder::AddLight(const DirectionalLightData& light) noexcept
    {
        const auto index = m_Frame.m_Size.directionalLights;
        if (m_Finalized) return Error(Code::Finalized, Section::DirectionalLights);
        if (index == m_Frame.m_Capacity.directionalLights) return Error(Code::CapacityExceeded, Section::DirectionalLights, index);
        if (!LightValues(light.entity, light.revision, light.color, light.intensity))
            return Error(Code::InvalidLight, Section::DirectionalLights, index);
        if (!UnitDirection(light.direction)) return Error(Code::InvalidLightDirection, Section::DirectionalLights, index);
        m_Frame.m_Directional[index] = light;
        ++m_Frame.m_Size.directionalLights;
        return {};
    }
    std::expected<void, FrameError> RenderFrameBuilder::AddLight(const PointLightData& light) noexcept
    {
        const auto index = m_Frame.m_Size.pointLights;
        if (m_Finalized) return Error(Code::Finalized, Section::PointLights);
        if (index == m_Frame.m_Capacity.pointLights) return Error(Code::CapacityExceeded, Section::PointLights, index);
        if (!LightValues(light.entity, light.revision, light.color, light.intensity) || !Finite(light.position))
            return Error(Code::InvalidLight, Section::PointLights, index);
        if (!Range(light.range)) return Error(Code::InvalidLightRange, Section::PointLights, index);
        m_Frame.m_Point[index] = light;
        ++m_Frame.m_Size.pointLights;
        return {};
    }
    std::expected<void, FrameError> RenderFrameBuilder::AddLight(const SpotLightData& light) noexcept
    {
        const auto index = m_Frame.m_Size.spotLights;
        if (m_Finalized) return Error(Code::Finalized, Section::SpotLights);
        if (index == m_Frame.m_Capacity.spotLights) return Error(Code::CapacityExceeded, Section::SpotLights, index);
        if (!LightValues(light.entity, light.revision, light.color, light.intensity) || !Finite(light.position))
            return Error(Code::InvalidLight, Section::SpotLights, index);
        if (!UnitDirection(light.direction)) return Error(Code::InvalidLightDirection, Section::SpotLights, index);
        if (!Range(light.range)) return Error(Code::InvalidLightRange, Section::SpotLights, index);
        if (!std::isfinite(light.innerConeRadians) || !std::isfinite(light.outerConeRadians)
            || light.innerConeRadians < 0 || light.innerConeRadians > light.outerConeRadians
            || light.outerConeRadians >= std::numbers::pi_v<float>)
            return Error(Code::InvalidLightCone, Section::SpotLights, index);
        m_Frame.m_Spot[index] = light;
        ++m_Frame.m_Size.spotLights;
        return {};
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
