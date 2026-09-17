#pragma once

#include "Material/MaterialInstance.h"
#include "Assets/Samplers/Sampler.h"
#include "Assets/Shaders/Shader.h"

namespace GEngine
{
    using ShaderProgramRegistry = Asset::AssetRegistry<Asset::ShaderProgramHandle, Asset::ShaderProgram>;
    using ShaderProgramView = ShaderProgramRegistry::Lease;
    enum class MaterialBindingCode { InvalidInstance, InvalidProgram, ProgramRevisionMismatch,
        MissingTexture, InvalidTexture, InvalidSampler, InvalidFallback };
    struct MaterialBindingError
    {
        MaterialBindingCode code;
        std::string binding;
        const char* message;
        Asset::RegistryError registry = Asset::RegistryError::InvalidHandle;
        std::optional<MaterialBindingCode> cause; // Resolution cause for InvalidFallback.
    };
    struct MaterialBindingResources
    {
        const ShaderProgramRegistry& programs;
        const Asset::TextureRegistry& textures;
        const Asset::SamplerRegistry& samplers;
    };
    // Explicit opt-in only. A fallback never masks a program/template/parameter
    // error. Every substitution preserves its original failure in the packet.
    struct MaterialBindingFallback { MaterialTextureValue value; };
    struct MaterialFallbackUse { std::size_t bindingIndex; MaterialBindingError reason; };
    struct ResolvedMaterialTexture
    {
        Asset::TextureRegistry::Lease texture;
        Asset::SamplerView sampler;
    };
    struct MaterialGpuParameter
    {
        MaterialParameterType type;
        std::uint32_t wordOffset;
        std::uint32_t wordCount;
    };
    // Immutable prepared submission data. Prepare once per visible instance after
    // publication and before the draw loop, then retain through submission. Rebuild
    // after resource publication even if the authored instance revision is stable.
    // This packet performs no GL work and owns no native object. Backend upload and
    // shader layout compatibility remain submission responsibilities.
    class PreparedMaterialBinding final
    {
    public:
        [[nodiscard]] static std::expected<PreparedMaterialBinding, MaterialBindingError> Prepare(
            const MaterialInstanceView&, const Asset::AssetPublication::FrameAccess&,
            const MaterialBindingResources&, std::optional<MaterialBindingFallback> = {});
        Asset::MaterialInstanceHandle Instance() const noexcept { return m_Instance.Identity(); }
        std::uint64_t PublicationRevision() const noexcept { return m_Instance.Revision(); }
        std::uint64_t Revision() const noexcept { return m_Instance->Revision(); }
        const MaterialInstanceView& Source() const noexcept { return m_Instance; }
        const ShaderProgramView& Program() const noexcept { return m_Program; }
        const PipelineState& Pipeline() const noexcept { return m_Instance->Declaration()->State(); }
        std::span<const ResolvedMaterialTexture> Textures() const noexcept { return m_Textures; }
        std::span<const MaterialFallbackUse> Fallbacks() const noexcept { return m_Fallbacks; }
        std::span<const MaterialGpuParameter> Parameters() const noexcept { return m_Parameters; }
        // Engine GPU upload format: sorted template parameter order; one zero-padded
        // 16-byte lane per scalar/vector; Matrix4 uses four column-major lanes.
        // Bool is uint32 0/1; ints/floats preserve 32-bit representation. Word offsets
        // are distinct from the template's compact CPU offsets. No host structs or
        // variant padding are uploaded. The renderer selects the destination ABI.
        std::span<const std::uint32_t> PackedWords() const noexcept { return m_Words; }
    private:
        PreparedMaterialBinding(MaterialInstanceView instance, ShaderProgramView program)
            : m_Instance(std::move(instance)), m_Program(std::move(program)) {}
        MaterialInstanceView m_Instance;
        ShaderProgramView m_Program;
        std::vector<ResolvedMaterialTexture> m_Textures;
        std::vector<MaterialFallbackUse> m_Fallbacks;
        std::vector<MaterialGpuParameter> m_Parameters;
        std::vector<std::uint32_t> m_Words;
    };
}
