#include "gepch.h"
#include "Material/MaterialTemplate.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GEngine
{
    namespace
    {
        auto Failure(MaterialDeclarationCode code, std::string_view binding, const char* message)
        { return std::unexpected(MaterialDeclarationError{code, std::string(binding), message}); }
        template<class T> requires std::is_enum_v<T>
        bool InRange(T value, T last) { return value >= T{} && value <= last; }
        bool ValidName(std::string_view name)
        { return !name.empty() && name.find('\0') == std::string_view::npos; }
        bool ValidBinding(const MaterialTextureValue& value) { return bool(value.texture) && bool(value.sampler); }
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
    std::expected<PipelineState, MaterialDeclarationError> PipelineState::Create(PipelineDesc desc)
    {
        if (!desc.program || !desc.programRevision)
            return Failure(MaterialDeclarationCode::InvalidProgram, {}, "Pipeline requires a program handle and resolved revision");
        if (!InRange(desc.alpha, AlphaMode::Transparent) || !InRange(desc.depthCompare, DepthCompare::Always)
            || !InRange(desc.cull, CullMode::Back) || !InRange(desc.frontFace, FrontFace::Clockwise)
            || !InRange(desc.polygon, PolygonMode::Point) || !InRange(desc.transparentBlend, TransparentBlend::PremultipliedAlpha))
            return Failure(MaterialDeclarationCode::InvalidState, {}, "Unsupported pipeline policy");
        if (desc.alpha == AlphaMode::Masked)
        {
            if (!std::isfinite(desc.alphaCutoff) || desc.alphaCutoff < 0 || desc.alphaCutoff > 1)
                return Failure(MaterialDeclarationCode::InvalidState, {}, "Masked alpha cutoff must be finite and in [0,1]");
            if (desc.alphaCutoff == 0) desc.alphaCutoff = 0;
        }
        else desc.alphaCutoff = 0;
        if (desc.alpha != AlphaMode::Transparent) desc.transparentBlend = TransparentBlend::StraightAlpha;
        if (!desc.depthTest) desc.depthCompare = DepthCompare::Always;
        return PipelineState(desc);
    }
    DepthState PipelineState::Depth() const noexcept
    { return {Description().depthTest, Description().depthTest && Alpha() != AlphaMode::Transparent, Description().depthCompare}; }
    BlendState PipelineState::Blend() const noexcept
    {
        if (Alpha() != AlphaMode::Transparent)
            return {false, BlendFactor::One, BlendFactor::Zero, BlendFactor::One, BlendFactor::Zero};
        return {true, Description().transparentBlend == TransparentBlend::StraightAlpha ? BlendFactor::SourceAlpha : BlendFactor::One,
            BlendFactor::OneMinusSourceAlpha, BlendFactor::One, BlendFactor::OneMinusSourceAlpha};
    }
    std::expected<MaterialTemplate, MaterialDeclarationError> MaterialTemplate::Create(const MaterialTemplateDesc& desc)
    {
        if (!desc.pipeline) return Failure(MaterialDeclarationCode::InvalidPipeline, {}, "Template requires a resolved pipeline version");
        if (desc.pipeline->Alpha() == AlphaMode::Transparent && (desc.castsShadow || desc.depthPass))
            return Failure(MaterialDeclarationCode::InvalidState, {}, "Transparent materials cannot declare an opaque shadow/depth pass");
        if (desc.depthPass && !desc.pipeline->Depth().write)
            return Failure(MaterialDeclarationCode::InvalidState, {}, "Depth pass requires a depth-writing pipeline");
        if (desc.parameters.size() > (std::numeric_limits<std::uint32_t>::max)() / 64
            || desc.textures.size() > (std::numeric_limits<std::uint32_t>::max)())
            return Failure(MaterialDeclarationCode::InvalidState, {}, "Declaration exceeds supported layout size");
        TemplateKey key{desc.pipeline->Key(), {}, {}, desc.castsShadow, desc.depthPass};
        for (auto parameter : desc.parameters)
        {
            if (!ValidName(parameter.name)) return Failure(MaterialDeclarationCode::InvalidName, parameter.name, "Parameter name is empty or contains NUL");
            if (!InRange(parameter.type, MaterialParameterType::Matrix4)
                || parameter.defaultValue.index() != static_cast<std::size_t>(parameter.type))
                return Failure(MaterialDeclarationCode::InvalidParameter, parameter.name, "Parameter type and default disagree");
            if (!Canonicalize(parameter.defaultValue)) return Failure(MaterialDeclarationCode::InvalidDefault, parameter.name, "Parameter defaults must be finite");
            constexpr std::uint32_t sizes[] = {4, 4, 4, 4, 8, 12, 16, 64};
            const auto bytes = sizes[static_cast<std::size_t>(parameter.type)];
            key.parameters.push_back({std::move(parameter), 0, bytes});
        }
        std::sort(key.parameters.begin(), key.parameters.end(), [](const auto& a, const auto& b) { return a.declaration.name < b.declaration.name; });
        std::uint32_t offset = 0;
        for (std::size_t i = 0; i < key.parameters.size(); ++i)
        {
            auto& parameter = key.parameters[i];
            if (i && parameter.declaration.name == key.parameters[i-1].declaration.name)
                return Failure(MaterialDeclarationCode::DuplicateBinding, parameter.declaration.name, "Duplicate parameter name");
            parameter.byteOffset = offset; offset += parameter.byteSize;
        }
        for (const auto& texture : desc.textures)
        {
            if (!ValidName(texture.name)) return Failure(MaterialDeclarationCode::InvalidName, texture.name, "Texture name is empty or contains NUL");
            if (texture.defaultValue && !ValidBinding(*texture.defaultValue))
                return Failure(MaterialDeclarationCode::InvalidDefault, texture.name, "Texture default requires both texture and sampler handles");
            if (std::any_of(key.parameters.begin(), key.parameters.end(), [&](const auto& p) { return p.declaration.name == texture.name; }))
                return Failure(MaterialDeclarationCode::DuplicateBinding, texture.name, "Parameter and texture names overlap");
            key.textures.push_back({texture, 0});
        }
        std::sort(key.textures.begin(), key.textures.end(), [](const auto& a, const auto& b) { return a.declaration.name < b.declaration.name; });
        for (std::size_t i = 0; i < key.textures.size(); ++i)
        {
            auto& slot = key.textures[i];
            if (i && slot.declaration.name == key.textures[i-1].declaration.name)
                return Failure(MaterialDeclarationCode::DuplicateBinding, slot.declaration.name, "Duplicate texture name");
            slot.unit = static_cast<std::uint32_t>(i);
        }
        return MaterialTemplate(desc.pipeline, std::move(key));
    }
    std::uint32_t MaterialTemplate::ParameterBytes() const noexcept
    { return m_Key.parameters.empty() ? 0 : m_Key.parameters.back().byteOffset + m_Key.parameters.back().byteSize; }
    std::expected<void, MaterialDeclarationError> MaterialTemplate::ValidateBindings(std::span<const MaterialTextureAssignment> bindings) const
    {
        for (std::size_t i = 0; i < bindings.size(); ++i)
        {
            const auto& binding = bindings[i];
            if (std::none_of(m_Key.textures.begin(), m_Key.textures.end(), [&](const auto& t) { return t.declaration.name == binding.name; }))
                return Failure(MaterialDeclarationCode::UnknownBinding, binding.name, "Texture binding is not declared");
            if (!ValidBinding(binding.value)) return Failure(MaterialDeclarationCode::InvalidDefault, binding.name, "Binding requires texture and sampler handles");
            for (std::size_t j = 0; j < i; ++j) if (binding.name == bindings[j].name)
                return Failure(MaterialDeclarationCode::DuplicateBinding, binding.name, "Texture binding supplied twice");
        }
        for (const auto& slot : m_Key.textures)
            if (slot.declaration.required && !slot.declaration.defaultValue
                && std::none_of(bindings.begin(), bindings.end(), [&](const auto& b) { return b.name == slot.declaration.name; }))
                return Failure(MaterialDeclarationCode::MissingBinding, slot.declaration.name, "Required texture/sampler binding is missing");
        return {};
    }
}
