#include "Material/MaterialTemplate.h"
#include "Assets/Shaders/Shader.h"
#include "Managers/ShaderManager.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <print>
#include <type_traits>

#if defined(GL_VERSION_1_0) || defined(GLAD_GL_H_) || defined(SDL_MAJOR_VERSION)
#error Backend declarations leaked into the normal shader/material/pipeline headers
#endif

namespace
{
    using namespace GEngine;
    using namespace GEngine::Asset;
    int checks{};
    template<class T> void Check(const T& condition, const char* message)
    {
        ++checks;
        if (!static_cast<bool>(condition)) { std::println("[FAIL] {}", message); std::exit(1); }
    }
    static_assert(std::same_as<ShaderCreationResult, std::expected<Shader, ShaderError>>);
    static_assert(std::same_as<decltype(std::declval<Shader&>().Link()), ShaderResult>);
    static_assert(!ShaderUniform<std::pair<unsigned int, unsigned int>>);
    static_assert(!std::copy_constructible<Shader> && std::is_nothrow_move_constructible_v<Shader>);
    static_assert(std::same_as<decltype(std::declval<MaterialTemplate&>().Key()), const TemplateKey&>);
    static_assert(std::same_as<decltype(std::declval<PipelineState&>().Description()), const PipelineDesc&>);
}

int main()
{
    PipelineDesc desc;
    desc.program = {3, 7, 9}; desc.programRevision = 2;
    auto opaque = PipelineState::Create(desc);
    Check(opaque && opaque->Depth().write && !opaque->Blend().enabled, "Opaque state");
    desc.alpha = AlphaMode::Masked;
    auto masked = PipelineState::Create(desc);
    Check(masked && masked->Depth().write && !masked->Blend().enabled && masked->Key() != opaque->Key(), "Masked semantic key");
    desc.alpha = AlphaMode::Transparent;
    auto transparent = PipelineState::Create(desc);
    Check(transparent && !transparent->Depth().write && transparent->Blend().enabled
        && transparent->Blend().sourceColor == BlendFactor::SourceAlpha
        && transparent->Blend().sourceAlpha == BlendFactor::One, "Transparent alpha/blend/depth agreement");
    desc.transparentBlend = TransparentBlend::PremultipliedAlpha;
    auto premultiplied = PipelineState::Create(desc);
    Check(premultiplied && premultiplied->Blend().sourceColor == BlendFactor::One
        && premultiplied->Key() != transparent->Key(), "Premultiplied blend semantics");
    desc.alpha = AlphaMode::Opaque; desc.alphaCutoff = .1f;
    Check(PipelineState::Create(desc)->Key() == opaque->Key(), "Inactive alpha policy must canonicalize");
    desc.depthTest = false; desc.depthCompare = DepthCompare::Greater;
    auto noDepth = PipelineState::Create(desc);
    Check(noDepth && !noDepth->Depth().test && !noDepth->Depth().write && noDepth->Depth().compare == DepthCompare::Always, "Disabled depth policy");
    desc = opaque->Description(); desc.programRevision++;
    Check(PipelineState::Create(desc)->Key() != opaque->Key(), "Program revision participates in identity");
    desc.program.generation++;
    Check(PipelineState::Create(desc)->Key() != opaque->Key(), "Program generation participates in identity");
    desc.program = {};
    Check(!PipelineState::Create(desc), "Null program rejected");
    desc = masked->Description(); desc.alphaCutoff = std::numeric_limits<float>::quiet_NaN();
    Check(!PipelineState::Create(desc), "NaN cutoff rejected");
    desc.alphaCutoff = 1.01f;
    Check(!PipelineState::Create(desc), "Out-of-range cutoff rejected");
    desc = opaque->Description(); desc.cull = static_cast<CullMode>(99);
    Check(!PipelineState::Create(desc), "Invalid state enum rejected");

    AssetPublication publication;
    PipelineRegistry registry(publication);
    PipelineHandle handle;
    { auto scope = publication.BeginPublication(); auto created = registry.Create(scope, *opaque); Check(created, "Publish pipeline"); handle = *created; }
    {
        PipelineView view;
        { auto frame = publication.BeginFrame(); auto acquired = registry.Acquire(frame, handle); Check(acquired, "Resolve pipeline"); view = *acquired; }
        const MaterialTextureValue image{{1, 2, 3}, {4, 5, 6}};
        std::vector<MaterialParameterDecl> parameters{{"z_color", MaterialParameterType::Float4, std::array<float,4>{1,2,3,4}},
            {"a_roughness", MaterialParameterType::Float, -0.f}};
        std::vector<MaterialTextureSlotDecl> textures{{"normal", true, {}}, {"albedo", true, image}, {"detail", false, {}}};
        MaterialTemplateDesc declaration{view, parameters, textures};
        auto material = MaterialTemplate::Create(declaration);
        Check(material, "Valid material declaration");
        Check(material->Pipeline() == handle && material->PipelineRevision() == 1 && material->Alpha() == AlphaMode::Opaque, "Pipeline identity and alpha derived from pinned state");
        Check(material->ParameterBytes() == 20 && material->Parameters()[0].declaration.name == "a_roughness"
            && material->Parameters()[1].byteOffset == 4, "Deterministic parameter layout");
        Check(material->Textures()[0].declaration.name == "albedo" && material->Textures()[2].declaration.name == "normal"
            && material->Textures()[2].bindingIndex == 2, "Stable paired texture/sampler slots");
        const auto missing = material->ValidateBindings({});
        Check(!missing && missing.error().code == MaterialDeclarationCode::MissingBinding && missing.error().binding == "normal", "Missing required binding is structured");
        const MaterialTextureAssignment normal[]{ {"normal", image} };
        Check(material->ValidateBindings(normal), "Explicit binding plus declared defaults");
        const MaterialTextureAssignment duplicate[]{ {"normal",image}, {"normal",image} };
        Check(!material->ValidateBindings(duplicate), "Duplicate assignments rejected");
        const MaterialTextureAssignment unknown[]{ {"unknown", image} };
        Check(!material->ValidateBindings(unknown), "Unknown assignment rejected");
        const MaterialTextureAssignment partial[]{ {"normal", {image.texture,{}}} };
        Check(!material->ValidateBindings(partial), "Missing sampler rejected");
        std::reverse(parameters.begin(), parameters.end()); std::reverse(textures.begin(), textures.end());
        std::get<float>(parameters[0].defaultValue) = 0.f;
        declaration.parameters = parameters; declaration.textures = textures;
        auto equivalent = MaterialTemplate::Create(declaration);
        Check(equivalent && equivalent->Key() == material->Key(), "Equivalent template identity ignores insertion order and signed zero");
        parameters[0].defaultValue = 0.25f;
        Check(MaterialTemplate::Create(declaration)->Key() != material->Key(), "Default value participates in identity");
        parameters[0].defaultValue = std::int32_t(1);
        Check(!MaterialTemplate::Create(declaration), "Mismatched parameter type rejected");
        parameters[0].defaultValue = std::numeric_limits<float>::infinity();
        Check(!MaterialTemplate::Create(declaration), "Non-finite default rejected");
        parameters[0].defaultValue = 0.f;
        parameters[1].name = parameters[0].name;
        Check(!MaterialTemplate::Create(declaration), "Duplicate parameter rejected");
        declaration.parameters = {};
        textures[1].name = textures[0].name;
        Check(!MaterialTemplate::Create(declaration), "Duplicate texture rejected");
        declaration.textures = {};
        declaration.pipeline = {};
        Check(!MaterialTemplate::Create(declaration), "Unresolved pipeline rejected");
        { auto scope = publication.BeginPublication(); Check(registry.Replace(scope, handle, *transparent), "Replace pipeline"); }
        { auto frame = publication.BeginFrame(); auto acquired = registry.Acquire(frame, handle); Check(acquired, "Resolve replacement"); declaration.pipeline = *acquired; }
        Check(material->Alpha() == AlphaMode::Opaque && material->PipelineRevision() == 1, "Published template retains old immutable pipeline version");
        Check(!MaterialTemplate::Create(declaration), "Transparent shadow/depth contradiction rejected");
        declaration.castsShadow = declaration.depthPass = false;
        auto translucent = MaterialTemplate::Create(declaration);
        Check(translucent && translucent->Alpha() == AlphaMode::Transparent && !translucent->State().Depth().write, "Consistent transparent template");
    }
    { auto scope = publication.BeginPublication(); Check(registry.Close(scope), "Pipeline leases retired before registry close"); }
    std::println("[PASS] material-pipeline declarations checks={}", checks);
}
