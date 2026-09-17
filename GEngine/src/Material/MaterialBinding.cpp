#include "gepch.h"
#include "Material/MaterialBinding.h"
#include <bit>

namespace GEngine
{
    namespace
    {
        auto Failure(MaterialBindingCode code, std::string_view name, const char* message,
            Asset::RegistryError registry = Asset::RegistryError::InvalidHandle)
        { return std::unexpected(MaterialBindingError{code, std::string(name), message, registry}); }
        std::expected<ResolvedMaterialTexture, MaterialBindingError> Resolve(
            std::optional<MaterialTextureValue> value, std::string_view name,
            const Asset::AssetPublication::FrameAccess& access, const MaterialBindingResources& resources)
        {
            if (!value) return Failure(MaterialBindingCode::MissingTexture, name, "Texture/sampler binding is missing");
            auto texture = resources.textures.Acquire(access, value->texture);
            if (!texture) return Failure(MaterialBindingCode::InvalidTexture, name, "Texture handle is stale, null or foreign", texture.error());
            if (!**texture) return Failure(MaterialBindingCode::InvalidTexture, name, "Texture resource is empty");
            auto sampler = resources.samplers.Acquire(access, value->sampler);
            if (!sampler) return Failure(MaterialBindingCode::InvalidSampler, name, "Sampler handle is stale, null or foreign", sampler.error());
            if (!**sampler) return Failure(MaterialBindingCode::InvalidSampler, name, "Sampler resource is empty");
            return ResolvedMaterialTexture{std::move(*texture), std::move(*sampler)};
        }
    }
    std::expected<PreparedMaterialBinding, MaterialBindingError> PreparedMaterialBinding::Prepare(
        const MaterialInstanceView& instance, const Asset::AssetPublication::FrameAccess& access,
        const MaterialBindingResources& resources, std::optional<MaterialBindingFallback> fallback)
    {
        if (!instance || !instance->Declaration())
            return Failure(MaterialBindingCode::InvalidInstance, {}, "Preparation requires a published instance with a resolved template");
        const auto& declaration = *instance->Declaration();
        const auto& pipeline = declaration.State().Description();
        auto program = resources.programs.Acquire(access, pipeline.program);
        if (!program || !(*program)->IsLinked())
            return Failure(MaterialBindingCode::InvalidProgram, {}, "Pipeline program must resolve to a linked resource");
        if (program->Revision() != pipeline.programRevision)
            return Failure(MaterialBindingCode::ProgramRevisionMismatch, {}, "Republish the pipeline/template for the new program revision");
        PreparedMaterialBinding result(instance, std::move(*program));
        std::optional<ResolvedMaterialTexture> resolvedFallback;
        // Optional unbound slots remain explicit empty entries. Required missing or
        // stale bindings fail unless the caller opted into a resolvable fallback.
        for (std::size_t i = 0; i < declaration.Textures().size(); ++i)
        {
            const auto& slot = declaration.Textures()[i].declaration;
            const auto value = instance->Textures()[i];
            if (!value && !slot.required && !fallback) { result.m_Textures.emplace_back(); continue; }
            auto binding = Resolve(value, slot.name, access, resources);
            if (!binding)
            {
                if (!fallback) return std::unexpected(std::move(binding.error()));
                if (!resolvedFallback)
                {
                    auto resolved = Resolve(fallback->value, slot.name, access, resources);
                    if (!resolved)
                    {
                        auto error = std::move(resolved.error());
                        error.cause = error.code;
                        error.code = MaterialBindingCode::InvalidFallback;
                        return std::unexpected(std::move(error));
                    }
                    resolvedFallback = std::move(*resolved);
                }
                result.m_Fallbacks.push_back({i, std::move(binding.error())});
                result.m_Textures.push_back(*resolvedFallback);
            }
            else result.m_Textures.push_back(std::move(*binding));
        }
        for (std::size_t i = 0; i < instance->Values().size(); ++i)
        {
            const auto type = declaration.Parameters()[i].declaration.type;
            const auto count = type == MaterialParameterType::Matrix4 ? 16u : 4u;
            const auto offset = static_cast<std::uint32_t>(result.m_Words.size());
            result.m_Parameters.push_back({type, offset, count});
            result.m_Words.resize(result.m_Words.size() + count, 0);
            auto* words = result.m_Words.data() + offset;
            std::visit([&](const auto& value) {
                using T = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<T, bool>) words[0] = value ? 1u : 0u;
                else if constexpr (std::integral<T> || std::same_as<T, float>) words[0] = std::bit_cast<std::uint32_t>(value);
                else for (std::size_t k = 0; k < value.size(); ++k) words[k] = std::bit_cast<std::uint32_t>(value[k]);
            }, instance->Values()[i]);
        }
        return result;
    }
}
