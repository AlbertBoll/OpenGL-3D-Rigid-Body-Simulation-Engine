#include "gepch.h"
#include "Material/MaterialInstance.h"
#include <cmath>

namespace GEngine
{
    namespace
    {
        auto Failure(MaterialInstanceCode code, std::string_view name, const char* message)
        { return std::unexpected(MaterialInstanceError{code, std::string(name), message}); }
        bool Canonicalize(MaterialParameterValue& value)
        {
            return std::visit([](auto& item) {
                using T = std::remove_cvref_t<decltype(item)>;
                if constexpr (std::same_as<T, float>)
                { if (!std::isfinite(item)) return false; if (item == 0) item = 0; }
                else if constexpr (!std::integral<T>)
                    for (float& scalar : item) { if (!std::isfinite(scalar)) return false; if (scalar == 0) scalar = 0; }
                return true;
            }, value);
        }
    }
    std::expected<MaterialInstance, MaterialInstanceError> MaterialInstance::Create(MaterialTemplateView declaration)
    {
        if (!declaration) return Failure(MaterialInstanceCode::InvalidTemplate, {}, "Instance requires a resolved template version");
        MaterialInstance result(std::move(declaration));
        for (const auto& p : result.m_Template->Parameters()) result.m_Values.push_back(p.declaration.defaultValue);
        for (const auto& t : result.m_Template->Textures()) result.m_Textures.push_back(t.declaration.defaultValue);
        return result;
    }
    MaterialEditResult MaterialInstance::SetParameter(std::string_view name, MaterialParameterValue value)
    {
        if (!m_Template) return Failure(MaterialInstanceCode::InvalidTemplate, name, "Moved-from instance");
        const auto declarations = m_Template->Parameters();
        const auto found = std::lower_bound(declarations.begin(), declarations.end(), name,
            [](const auto& p, auto key) { return p.declaration.name < key; });
        if (found == declarations.end() || found->declaration.name != name)
            return Failure(MaterialInstanceCode::UnknownParameter, name, "Parameter is not declared");
        if (value.index() != static_cast<std::size_t>(found->declaration.type) || !Canonicalize(value))
            return Failure(MaterialInstanceCode::InvalidParameter, name, "Parameter type must match and values must be finite");
        auto& current = m_Values[static_cast<std::size_t>(found - declarations.begin())];
        if (current == value) return {};
        if (m_Revision == (std::numeric_limits<std::uint64_t>::max)())
            return Failure(MaterialInstanceCode::RevisionExhausted, name, "Material revision cannot wrap");
        current = std::move(value); ++m_Revision;
        return {};
    }
    MaterialEditResult MaterialInstance::ResetParameter(std::string_view name)
    {
        if (!m_Template) return Failure(MaterialInstanceCode::InvalidTemplate, name, "Moved-from instance");
        for (const auto& p : m_Template->Parameters())
            if (p.declaration.name == name) return SetParameter(name, p.declaration.defaultValue);
        return Failure(MaterialInstanceCode::UnknownParameter, name, "Parameter is not declared");
    }
    MaterialEditResult MaterialInstance::AssignTexture(std::size_t index, std::optional<MaterialTextureValue> value)
    {
        if (m_Textures[index] == value) return {};
        if (m_Revision == (std::numeric_limits<std::uint64_t>::max)())
            return Failure(MaterialInstanceCode::RevisionExhausted, m_Template->Textures()[index].declaration.name, "Material revision cannot wrap");
        m_Textures[index] = value; ++m_Revision;
        return {};
    }
    MaterialEditResult MaterialInstance::SetTexture(std::string_view name, MaterialTextureValue value)
    {
        if (!m_Template) return Failure(MaterialInstanceCode::InvalidTemplate, name, "Moved-from instance");
        const auto declarations = m_Template->Textures();
        for (std::size_t i = 0; i < declarations.size(); ++i)
            if (declarations[i].declaration.name == name)
            {
                if (!value.texture || !value.sampler)
                    return Failure(MaterialInstanceCode::InvalidTexture, name, "Binding requires both texture and sampler handles");
                return AssignTexture(i, value);
            }
        return Failure(MaterialInstanceCode::UnknownTexture, name, "Texture is not declared");
    }
    MaterialEditResult MaterialInstance::ResetTexture(std::string_view name)
    {
        if (!m_Template) return Failure(MaterialInstanceCode::InvalidTemplate, name, "Moved-from instance");
        const auto declarations = m_Template->Textures();
        for (std::size_t i = 0; i < declarations.size(); ++i)
            if (declarations[i].declaration.name == name) return AssignTexture(i, declarations[i].declaration.defaultValue);
        return Failure(MaterialInstanceCode::UnknownTexture, name, "Texture is not declared");
    }
}
