#pragma once

#include "Material/Pipeline.h"
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace GEngine
{
    enum class MaterialParameterType { Boolean, Integer, UnsignedInteger, Float, Float2, Float3, Float4, Matrix4 };
    using MaterialParameterValue = std::variant<bool, std::int32_t, std::uint32_t, float,
        std::array<float, 2>, std::array<float, 3>, std::array<float, 4>, std::array<float, 16>>;
    struct MaterialParameterDecl
    {
        std::string name;
        MaterialParameterType type = MaterialParameterType::Float;
        MaterialParameterValue defaultValue = 0.f;
        bool operator==(const MaterialParameterDecl&) const = default;
    };
    struct MaterialParameterLayout
    {
        MaterialParameterDecl declaration;
        std::uint32_t byteOffset;
        std::uint32_t byteSize;
        bool operator==(const MaterialParameterLayout&) const = default;
    };
    struct MaterialTextureValue
    {
        Asset::TextureHandle texture;
        Asset::SamplerHandle sampler;
        bool operator==(const MaterialTextureValue&) const = default;
    };
    struct MaterialTextureSlotDecl
    {
        std::string name;
        bool required = true;
        std::optional<MaterialTextureValue> defaultValue;
        bool operator==(const MaterialTextureSlotDecl&) const = default;
    };
    struct MaterialTextureSlot
    {
        MaterialTextureSlotDecl declaration;
        std::uint32_t bindingIndex; // Semantic ordinal in sorted declaration order, never a native texture unit.
        bool operator==(const MaterialTextureSlot&) const = default;
    };
    struct MaterialTextureAssignment { std::string_view name; MaterialTextureValue value; };
    struct MaterialTemplateDesc
    {
        PipelineView pipeline;
        std::span<const MaterialParameterDecl> parameters;
        std::span<const MaterialTextureSlotDecl> textures;
        bool castsShadow = true;
        bool depthPass = true;
    };
    struct TemplateKey
    {
        PipelineKey pipeline;
        std::vector<MaterialParameterLayout> parameters;
        std::vector<MaterialTextureSlot> textures;
        bool castsShadow;
        bool depthPass;
        bool operator==(const TemplateKey&) const = default;
    };
    // Immutable declaration. The pipeline lease pins the exact resolved version.
    // CPU parameter offsets describe packed 4-byte scalar storage, not a backend UBO ABI.
    class MaterialTemplate final
    {
    public:
        [[nodiscard]] static std::expected<MaterialTemplate, MaterialDeclarationError> Create(const MaterialTemplateDesc&);
        const TemplateKey& Key() const noexcept { return m_Key; }
        Asset::PipelineHandle Pipeline() const noexcept { return m_Pipeline.Identity(); }
        std::uint64_t PipelineRevision() const noexcept { return m_Pipeline.Revision(); }
        const PipelineState& State() const noexcept { return *m_Pipeline; }
        AlphaMode Alpha() const noexcept { return State().Alpha(); }
        std::span<const MaterialParameterLayout> Parameters() const noexcept { return m_Key.parameters; }
        std::span<const MaterialTextureSlot> Textures() const noexcept { return m_Key.textures; }
        std::uint32_t ParameterBytes() const noexcept;
        [[nodiscard]] std::expected<void, MaterialDeclarationError> ValidateBindings(std::span<const MaterialTextureAssignment>) const;
    private:
        MaterialTemplate(PipelineView pipeline, TemplateKey key) : m_Pipeline(std::move(pipeline)), m_Key(std::move(key)) {}
        PipelineView m_Pipeline;
        TemplateKey m_Key;
    };
    using MaterialTemplateRegistry = Asset::AssetRegistry<Asset::MaterialTemplateHandle, MaterialTemplate>;
}
