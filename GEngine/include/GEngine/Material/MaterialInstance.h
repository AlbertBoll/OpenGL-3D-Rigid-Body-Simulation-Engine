#pragma once

#include "Material/MaterialTemplate.h"

namespace GEngine
{
    using MaterialTemplateView = MaterialTemplateRegistry::Lease;
    enum class MaterialInstanceCode { InvalidTemplate, UnknownParameter, InvalidParameter,
        UnknownTexture, InvalidTexture, RevisionExhausted };
    struct MaterialInstanceError
    {
        MaterialInstanceCode code;
        std::string binding;
        const char* message;
    };
    using MaterialEditResult = std::expected<void, MaterialInstanceError>;

    // CPU authoring value. Copying creates independent overrides while retaining
    // the same immutable template version. Edits require exclusive caller access.
    // Shared instances use the same MaterialInstanceHandle; unique instances are
    // copies published under distinct handles. Publish before frame extraction.
    class MaterialInstance final
    {
    public:
        [[nodiscard]] static std::expected<MaterialInstance, MaterialInstanceError> Create(MaterialTemplateView);
        Asset::MaterialTemplateHandle Template() const noexcept { return m_Template.Identity(); }
        std::uint64_t TemplateRevision() const noexcept { return m_Template.Revision(); }
        const MaterialTemplateView& Declaration() const noexcept { return m_Template; }
        std::uint64_t Revision() const noexcept { return m_Revision; }
        std::span<const MaterialParameterValue> Values() const noexcept { return m_Values; }
        std::span<const std::optional<MaterialTextureValue>> Textures() const noexcept { return m_Textures; }
        [[nodiscard]] MaterialEditResult SetParameter(std::string_view, MaterialParameterValue);
        [[nodiscard]] MaterialEditResult ResetParameter(std::string_view);
        [[nodiscard]] MaterialEditResult SetTexture(std::string_view, MaterialTextureValue);
        [[nodiscard]] MaterialEditResult ResetTexture(std::string_view);
        // A shader reload changes the immutable declaration, not authored state.
        // Copy exact values (including unbound optional slots) and revision.
        [[nodiscard]] std::expected<MaterialInstance, MaterialInstanceError>
        WithDeclaration(MaterialTemplateView declaration) const
        {
            const auto incompatible = [] {
                return std::unexpected(MaterialInstanceError{MaterialInstanceCode::InvalidTemplate,
                    {}, "Replacement declaration must preserve the material schema"});
            };
            if (!m_Template || !declaration) return incompatible();
            const auto before = m_Template->Parameters(), after = declaration->Parameters();
            const auto oldTextures = m_Template->Textures(), newTextures = declaration->Textures();
            if (before.size() != after.size() || oldTextures.size() != newTextures.size()) return incompatible();
            for (std::size_t i = 0; i < before.size(); ++i)
                if (before[i].declaration.name != after[i].declaration.name ||
                    before[i].declaration.type != after[i].declaration.type) return incompatible();
            for (std::size_t i = 0; i < oldTextures.size(); ++i)
                if (oldTextures[i].declaration.name != newTextures[i].declaration.name ||
                    oldTextures[i].declaration.required != newTextures[i].declaration.required) return incompatible();
            auto result = *this;
            result.m_Template = std::move(declaration);
            return result;
        }
    private:
        explicit MaterialInstance(MaterialTemplateView declaration) : m_Template(std::move(declaration)) {}
        MaterialEditResult AssignTexture(std::size_t, std::optional<MaterialTextureValue>);
        MaterialTemplateView m_Template;
        std::vector<MaterialParameterValue> m_Values;
        std::vector<std::optional<MaterialTextureValue>> m_Textures;
        std::uint64_t m_Revision = 1;
    };
    // Registry revision is publication identity; Revision() is effective authored
    // content identity. Skip Replace when an edit leaves Revision() unchanged.
    using MaterialInstanceRegistry = Asset::AssetRegistry<Asset::MaterialInstanceHandle, MaterialInstance>;
    using MaterialInstanceView = MaterialInstanceRegistry::Lease;
}
