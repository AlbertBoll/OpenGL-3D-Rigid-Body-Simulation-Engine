#pragma once

#include "Assets/AssetRegistry.h"
#include <cstdint>
#include <expected>
#include <string>

namespace GEngine
{
    enum class AlphaMode { Opaque, Masked, Transparent };
    enum class DepthCompare { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
    enum class CullMode { None, Front, Back };
    enum class FrontFace { CounterClockwise, Clockwise };
    enum class PolygonMode { Fill, Line, Point };
    enum class TransparentBlend { StraightAlpha, PremultipliedAlpha };
    enum class BlendFactor { Zero, One, SourceAlpha, OneMinusSourceAlpha };
    enum class MaterialDeclarationCode { InvalidProgram, InvalidState, InvalidPipeline, InvalidName,
        DuplicateBinding, InvalidParameter, InvalidDefault, MissingBinding, UnknownBinding };
    struct MaterialDeclarationError
    {
        MaterialDeclarationCode code;
        std::string binding;
        const char* message;
    };
    struct PipelineDesc
    {
        Asset::ShaderProgramHandle program;
        std::uint64_t programRevision = 0;
        bool depthTest = true;
        DepthCompare depthCompare = DepthCompare::Less;
        CullMode cull = CullMode::Back;
        FrontFace frontFace = FrontFace::CounterClockwise;
        PolygonMode polygon = PolygonMode::Fill;
        AlphaMode alpha = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        TransparentBlend transparentBlend = TransparentBlend::StraightAlpha;
        bool operator==(const PipelineDesc&) const = default;
    };
    // Exact semantic key, not a native name or a hash whose collisions imply identity.
    // A disabled depth test/inactive alpha option is canonicalized before comparison.
    struct PipelineKey { PipelineDesc declaration; bool operator==(const PipelineKey&) const = default; };
    struct DepthState { bool test; bool write; DepthCompare compare; };
    struct BlendState
    {
        bool enabled;
        BlendFactor sourceColor, destinationColor, sourceAlpha, destinationAlpha;
    };
    class PipelineState final
    {
    public:
        [[nodiscard]] static std::expected<PipelineState, MaterialDeclarationError> Create(PipelineDesc);
        const PipelineKey& Key() const noexcept { return m_Key; }
        const PipelineDesc& Description() const noexcept { return m_Key.declaration; }
        DepthState Depth() const noexcept;
        BlendState Blend() const noexcept;
        AlphaMode Alpha() const noexcept { return Description().alpha; }
    private:
        explicit PipelineState(PipelineDesc desc) : m_Key{desc} {}
        PipelineKey m_Key;
    };
    using PipelineRegistry = Asset::AssetRegistry<Asset::PipelineHandle, PipelineState>;
    using PipelineView = PipelineRegistry::Lease;
}
