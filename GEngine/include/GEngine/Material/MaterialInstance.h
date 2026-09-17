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
