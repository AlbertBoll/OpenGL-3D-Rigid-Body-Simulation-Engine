#include "gepch.h"
#include "ShaderBackend.h"
#include "Material/MaterialTemplate.h"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <utility>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace GEngine::Asset
{
    namespace
    {
        thread_local ShaderCreationWork creationWork;
        bool Identifier(std::string_view name)
        {
            const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
            return !name.empty() && letter(name.front()) && std::all_of(name.begin(), name.end(),
                [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
        }
        ShaderValueType ValueType(GLenum type)
        {
            switch (type)
            {
            case GL_BOOL: return ShaderValueType::Boolean;
            case GL_INT: return ShaderValueType::Integer;
            case GL_UNSIGNED_INT: return ShaderValueType::UnsignedInteger;
            case GL_FLOAT: return ShaderValueType::Float;
            case GL_FLOAT_VEC2: return ShaderValueType::Float2;
            case GL_FLOAT_VEC3: return ShaderValueType::Float3;
            case GL_FLOAT_VEC4: return ShaderValueType::Float4;
            case GL_FLOAT_MAT4: return ShaderValueType::Matrix4;
            case GL_SAMPLER_2D: return ShaderValueType::Sampler2D;
            case GL_SAMPLER_CUBE: return ShaderValueType::SamplerCube;
            case GL_SAMPLER_2D_ARRAY: return ShaderValueType::Sampler2DArray;
            case GL_UNSIGNED_INT_VEC4: return ShaderValueType::UInt4;
            default: return ShaderValueType::Unknown;
            }
        }
        GLenum NativeStage(ShaderStage stage)
        {
            switch (stage)
            {
            case VERTEX: return GL_VERTEX_SHADER;
            case FRAGMENT: return GL_FRAGMENT_SHADER;
            case GEOMETRY: return GL_GEOMETRY_SHADER;
            case TESS_CONTROL: return GL_TESS_CONTROL_SHADER;
            case TESS_EVALUATION: return GL_TESS_EVALUATION_SHADER;
            case COMPUTE: return GL_COMPUTE_SHADER;
            default: return 0;
            }
        }
        std::optional<ShaderStage> FileStage(std::string_view file)
        {
            constexpr std::pair<std::string_view, ShaderStage> extensions[] = {
                {".vs", VERTEX}, {".vert", VERTEX}, {"_vert.glsl", VERTEX}, {".vert.glsl", VERTEX},
                {".fs", FRAGMENT}, {".frag", FRAGMENT}, {"_frag.glsl", FRAGMENT}, {".frag.glsl", FRAGMENT},
                {".gs", GEOMETRY}, {".geom", GEOMETRY}, {".geom.glsl", GEOMETRY},
                {".tcs", TESS_CONTROL}, {".tcs.glsl", TESS_CONTROL},
                {".tes", TESS_EVALUATION}, {".tes.glsl", TESS_EVALUATION},
                {".cs", COMPUTE}, {".cs.glsl", COMPUTE}};
            for (const auto& [extension, stage] : extensions) if (file.ends_with(extension)) return stage;
            return {};
        }
        std::string CreationLog(GLuint name, bool program)
        {
            GLint length{};
            if (program) glGetProgramiv(name, GL_INFO_LOG_LENGTH, &length);
            else glGetShaderiv(name, GL_INFO_LOG_LENGTH, &length);
            if (length <= 1) return {};
            std::string log(static_cast<std::size_t>(length), '\0');
            GLsizei written{};
            if (program) glGetProgramInfoLog(name, length, &written, log.data());
            else glGetShaderInfoLog(name, length, &written, log.data());
            log.resize(static_cast<std::size_t>(written));
            return log;
        }
        struct StageOwner { GLuint name; ~StageOwner() { if (name) glDeleteShader(name); } };
        // Rollback also handles standard allocation unwinding, without reclassifying it.
        struct BuildGuard
        {
            Shader& shader;
            bool committed = false;
            ~BuildGuard() { if (!committed && !shader.IsLinked()) shader.Destroy(); }
        };
        auto Failure(ShaderErrorCode code, std::optional<ShaderStage> stage, std::string_view source, std::string log)
        { return std::unexpected(ShaderError{code, stage, std::string(source), std::move(log)}); }
    }
    ShaderResult Shader::CompileShader(const char* file)
    {
        BuildGuard rollback{*this};
        if (!file || !*file) return Failure(ShaderErrorCode::InvalidInput, {}, {}, "Shader filename is empty");
        const auto stage = FileStage(file);
        if (!stage) return Failure(ShaderErrorCode::InvalidInput, {}, file, "Unrecognized shader extension");
        auto result = CompileShader(file, *stage);
        rollback.committed = result.has_value();
        return result;
    }
    ShaderResult Shader::CompileShader(const char* file, ShaderStage stage)
    {
        BuildGuard rollback{*this};
        if (!file || !*file) return Failure(ShaderErrorCode::InvalidInput, stage, {}, "Shader filename is empty");
        if (IsLinked()) return Failure(ShaderErrorCode::InvalidState, stage, file, "Build a new Shader to replace a linked program");
        std::ifstream input(file, std::ios::in | std::ios::binary);
        if (!input) return Failure(ShaderErrorCode::FileRead, stage, file, "Unable to open shader source");
        std::stringstream code;
        code << input.rdbuf();
        if (input.bad() || code.bad()) return Failure(ShaderErrorCode::FileRead, stage, file, "Unable to read shader source");
        auto result = CompileShader(code.str(), stage, file);
        rollback.committed = result.has_value();
        return result;
    }
    ShaderResult Shader::CompileShader(const std::string& source, ShaderStage stage, const char* label)
    {
        BuildGuard rollback{*this};
        const std::string_view sourceLabel = label ? label : "";
        if (IsLinked()) return Failure(ShaderErrorCode::InvalidState, stage, sourceLabel, "Build a new Shader to replace a linked program");
        const auto nativeStage = NativeStage(stage);
        if (!nativeStage) return Failure(ShaderErrorCode::InvalidInput, stage, sourceLabel, "Unsupported shader stage");
        if (source.empty() || source.size() > static_cast<std::size_t>((std::numeric_limits<GLint>::max)()))
            return Failure(ShaderErrorCode::InvalidInput, stage, sourceLabel, "Shader source is empty or exceeds the supported length");
        if (!SDL_GL_GetCurrentContext()) return Failure(ShaderErrorCode::ContextUnavailable, stage, sourceLabel, "No current shader context");
        if (!m_Storage)
        {
            m_Storage = std::make_unique<ShaderStorage>();
            m_Storage->context = SDL_GL_GetCurrentContext();
            m_Storage->currentContext = SDL_GL_GetCurrentContext;
            m_Storage->thread = std::this_thread::get_id();
            m_Storage->program = glCreateProgram();
            if (!m_Storage->program) return Failure(ShaderErrorCode::ProgramAllocation, stage, sourceLabel, "Unable to create shader program");
        }
        m_Storage->RequireOwner();
        StageOwner shader{glCreateShader(nativeStage)};
        if (!shader.name) return Failure(ShaderErrorCode::ShaderAllocation, stage, sourceLabel, "Unable to create shader object");
        const char* text = source.data();
        const auto length = static_cast<GLint>(source.size());
        glShaderSource(shader.name, 1, &text, &length);
        ++creationWork.stageCompilations;
        glCompileShader(shader.name);
        GLint status{};
        glGetShaderiv(shader.name, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) return Failure(ShaderErrorCode::Compile, stage, sourceLabel, "Shader compilation failed:\n" + CreationLog(shader.name, false));
        m_Storage->stages.push_back(shader.name);
        glAttachShader(m_Storage->program, std::exchange(shader.name, 0));
        rollback.committed = true;
        return {};
    }
    ShaderResult Shader::Link()
    {
        if (IsLinked()) return {};
        BuildGuard rollback{*this};
        if (!m_Storage || !m_Storage->program || m_Storage->stages.empty())
            return Failure(ShaderErrorCode::InvalidState, {}, {}, "Program has no compiled shader stages");
        m_Storage->RequireOwner();
        ++creationWork.links;
        glLinkProgram(m_Storage->program);
        GLint status{};
        glGetProgramiv(m_Storage->program, GL_LINK_STATUS, &status);
        if (status != GL_TRUE) return Failure(ShaderErrorCode::Link, {}, {}, "Program link failed:\n" + CreationLog(m_Storage->program, true));
        m_Storage->RetireStages();
        FindUniformLocations();
        m_Storage->linked = true;
        rollback.committed = true;
        return {};
    }
    std::expected<Shader, ShaderError> Shader::Create(const ShaderProgramDesc& desc)
    { return CreateShaderProgram(desc.stages); }
    ShaderCreationResult CreateShaderProgram(std::span<const ShaderSource> sources)
    {
        if (sources.empty()) return Failure(ShaderErrorCode::InvalidInput, {}, {}, "Shader program requires source stages");
        Shader candidate;
        for (const auto& source : sources)
        {
            const std::string label(source.label);
            auto compiled = candidate.CompileShader(std::string(source.source), source.type, label.c_str());
            if (!compiled) return std::unexpected(std::move(compiled.error()));
        }
        if (auto linked = candidate.Link(); !linked) return std::unexpected(std::move(linked.error()));
        return candidate;
    }
    ShaderCreationResult CreateShaderProgramFromFiles(std::span<const std::string> files)
    {
        if (files.empty()) return Failure(ShaderErrorCode::InvalidInput, {}, {}, "Shader program requires source files");
        Shader candidate;
        for (const auto& file : files)
            if (auto compiled = candidate.CompileShader(file.c_str()); !compiled) return std::unexpected(std::move(compiled.error()));
        if (auto linked = candidate.Link(); !linked) return std::unexpected(std::move(linked.error()));
        return candidate;
    }
    ShaderResult Shader::Validate() const
    {
        if (!IsLinked()) return Failure(ShaderErrorCode::InvalidState, {}, {}, "Program is not linked");
        m_Storage->RequireOwner();
        glValidateProgram(m_Storage->program);
        GLint status{};
        glGetProgramiv(m_Storage->program, GL_VALIDATE_STATUS, &status);
        if (status != GL_TRUE) return Failure(ShaderErrorCode::Validation, {}, {}, CreationLog(m_Storage->program, true));
        return {};
    }
    void Shader::FindUniformLocations()
    {
        ++creationWork.reflectionPasses;
        GLint count{};
        glGetProgramInterfaceiv(m_Storage->program, GL_UNIFORM, GL_ACTIVE_RESOURCES, &count);
        constexpr GLenum properties[] = {GL_NAME_LENGTH, GL_LOCATION, GL_BLOCK_INDEX, GL_ARRAY_SIZE};
        for (GLint i = 0; i < count; ++i)
        {
            GLint values[4]{};
            glGetProgramResourceiv(m_Storage->program, GL_UNIFORM, i, 4, properties, 4, nullptr, values);
            if (values[2] != -1) continue;
            std::string name(static_cast<std::size_t>(values[0]), '\0');
            GLsizei written{};
            glGetProgramResourceName(m_Storage->program, GL_UNIFORM, i, values[0], &written, name.data());
            name.resize(static_cast<std::size_t>(written));
            m_Storage->uniforms.emplace(name, values[1]);
            const auto index = name.rfind("[0]");
            if (index != std::string::npos)
            {
                // Arrays need not have arithmetically contiguous native locations.
                // Resolve every admitted alias now; submission never queries a miss.
                if (index + 3 == name.size()) m_Storage->uniforms.emplace(name.substr(0, index), values[1]);
                for (GLint element = 1; element < values[3]; ++element)
                {
                    auto alias = name;
                    alias.replace(index, 3, "[" + std::to_string(element) + "]");
                    const auto location = glGetUniformLocation(m_Storage->program, alias.c_str());
                    m_Storage->uniforms.emplace(std::move(alias), location);
                }
            }
        }
    }

    ShaderCreationWork Shader::CreationWork() noexcept { return creationWork; }

    std::expected<ShaderDescription, ShaderError> ShaderDescription::Create(const ShaderDescriptionDesc& desc)
    {
        ++creationWork.descriptions;
        if (desc.stages.empty() || desc.stages.size() > 6)
            return Failure(ShaderErrorCode::InvalidInput, {}, {}, "Description requires one to six distinct stages");
        std::vector<ShaderSource> stages(desc.stages.begin(), desc.stages.end());
        std::sort(stages.begin(), stages.end(), [](const auto& a, const auto& b) { return a.type < b.type; });
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            const auto& stage = stages[i];
            const auto first = stage.source.find_first_not_of(" \t\r\n");
            if (!NativeStage(stage.type) || (i && stages[i-1].type == stage.type) ||
                first == std::string_view::npos || !stage.source.substr(first).starts_with("#version ") ||
                stage.source.find('\n', first) == std::string_view::npos || stage.source.find('\0') != std::string_view::npos)
                return Failure(ShaderErrorCode::InvalidInput, stage.type, stage.label,
                    "Distinct supported stages with a leading #version line and non-NUL source are required");
        }
        if (stages.back().type == COMPUTE && stages.size() != 1)
            return Failure(ShaderErrorCode::InvalidInput, COMPUTE, {}, "Compute cannot share a graphics program");
        ShaderDescription result;
        result.m_Bindings.assign(desc.bindings.begin(), desc.bindings.end());
        std::sort(result.m_Bindings.begin(), result.m_Bindings.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        for (std::size_t i = 0; i < result.m_Bindings.size(); ++i)
        {
            const auto& binding = result.m_Bindings[i];
            if (!Identifier(binding.name) || binding.type <= ShaderValueType::Unknown || binding.type > ShaderValueType::UInt4 ||
                binding.semantic < ShaderSemantic::Parameter || binding.semantic > ShaderSemantic::Color ||
                (i && result.m_Bindings[i-1].name == binding.name))
                return Failure(ShaderErrorCode::InvalidInput, {}, binding.name, "Invalid or duplicate semantic binding");
        }
        std::vector<ShaderVariant> variants(desc.variants.begin(), desc.variants.end());
        if (variants.empty()) variants.push_back({});
        if (variants.size() > 16)
            return Failure(ShaderErrorCode::Capacity, {}, {}, "At most 16 explicitly admitted variants");
        std::sort(variants.begin(), variants.end(), [](const auto& a, const auto& b) { return a.key.value < b.key.value; });
        if (variants.front().key.value != 0)
            return Failure(ShaderErrorCode::VariantNotAdmitted, {}, {}, "The default variant key 0 must be admitted");
        for (std::size_t i = 0; i < variants.size(); ++i)
        {
            auto& variant = variants[i];
            if (variant.defines.size() > 8 || (i && variants[i-1].key == variant.key))
                return Failure(ShaderErrorCode::InvalidInput, {}, {}, "Duplicate variant key or more than eight defines");
            std::sort(variant.defines.begin(), variant.defines.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
            std::string defines;
            for (std::size_t j = 0; j < variant.defines.size(); ++j)
            {
                const auto& define = variant.defines[j];
                if (!Identifier(define.name) || define.name.starts_with("GL_") || define.name.starts_with("GENGINE_") ||
                    define.name.find("__") != std::string::npos || (j && variant.defines[j-1].name == define.name))
                    return Failure(ShaderErrorCode::InvalidInput, {}, define.name, "Invalid, reserved or duplicate variant define");
                defines += "#define " + define.name + " " + std::to_string(define.value) + "\n";
            }
            ShaderVariantSource selected{variant.key, {}, "GEngine-shader-description-v1;"};
            const auto field = [&](std::string_view value) { selected.identity += std::to_string(value.size()) + ":"; selected.identity += value; };
            // Semantic names/types and requirements participate; parameter values and labels do not.
            field(std::to_string(result.m_Bindings.size()));
            for (const auto& binding : result.m_Bindings)
            {
                field(binding.name); field(std::to_string(static_cast<int>(binding.type)));
                field(std::to_string(static_cast<int>(binding.semantic))); field(binding.required ? "1" : "0");
            }
            field(std::to_string(stages.size()));
            for (const auto& stage : stages)
            {
                std::string source(stage.source);
                const auto line = source.find('\n', source.find_first_not_of(" \t\r\n"));
                if (!defines.empty()) source.insert(line + 1, defines + "#line 2\n");
                field(std::to_string(static_cast<int>(stage.type))); field(source);
                selected.stages.push_back({stage.type, std::move(source), std::string(stage.label)});
            }
            result.m_Variants.push_back(std::move(selected));
        }
        return result;
    }

    std::expected<const ShaderVariantSource*, ShaderError> ShaderDescription::Select(ShaderVariantKey key) const
    {
        for (const auto& variant : m_Variants) if (variant.key == key) return &variant;
        return Failure(ShaderErrorCode::VariantNotAdmitted, {}, std::to_string(key.value), "Variant key was not explicitly admitted");
    }

    std::expected<Shader, ShaderError> Shader::Create(const ShaderDescription& desc, ShaderVariantKey key)
    {
        auto selected = desc.Select(key);
        if (!selected) return std::unexpected(selected.error());
        std::vector<ShaderSource> stages;
        for (const auto& stage : (*selected)->stages) stages.push_back({stage.type, stage.source, stage.label});
        auto shader = CreateShaderProgram(stages);
        if (!shader) return std::unexpected(shader.error());
        if (auto reflected = shader->Reflect(desc.Bindings()); !reflected) return std::unexpected(reflected.error());
        return std::move(*shader);
    }

    std::expected<std::vector<ShaderReflection>, ShaderError> Shader::Reflect(std::span<const ShaderBindingDecl> bindings) const
    {
        if (!IsLinked()) return Failure(ShaderErrorCode::InvalidState, {}, {}, "Reflection requires a linked owner");
        if (m_Storage->thread != std::this_thread::get_id())
            return Failure(ShaderErrorCode::WrongThread, {}, {}, "Reflection requires the owning context thread");
        if (m_Storage->context != SDL_GL_GetCurrentContext())
            return Failure(ShaderErrorCode::ContextUnavailable, {}, {}, "Reflection requires the owning current context");
        ++creationWork.reflectionPasses;
        const auto program = m_Storage->program;
        const auto name = [&](GLenum interfaceType, GLuint index) {
            const GLenum property = GL_NAME_LENGTH;
            GLint length = 0;
            glGetProgramResourceiv(program, interfaceType, index, 1, &property, 1, nullptr, &length);
            std::string value(static_cast<std::size_t>((std::max)(length, 1)), '\0');
            GLsizei written = 0;
            glGetProgramResourceName(program, interfaceType, index, length, &written, value.data());
            value.resize(static_cast<std::size_t>(written)); return value;
        };
        std::vector<ShaderReflection> result;
        for (const GLenum interfaceType : {GL_UNIFORM_BLOCK, GL_SHADER_STORAGE_BLOCK})
        {
            GLint count = 0; glGetProgramInterfaceiv(program, interfaceType, GL_ACTIVE_RESOURCES, &count);
            for (GLint i = 0; i < count; ++i)
            {
                const GLenum property = GL_BUFFER_DATA_SIZE;
                GLint bytes = 0; glGetProgramResourceiv(program, interfaceType, i, 1, &property, 1, nullptr, &bytes);
                ShaderReflection value;
                value.name = name(interfaceType, i);
                value.kind = interfaceType == GL_UNIFORM_BLOCK ? ShaderResourceKind::UniformBlock : ShaderResourceKind::StorageBlock;
                value.blockBytes = static_cast<std::uint32_t>(bytes); result.push_back(std::move(value));
            }
        }
        for (const GLenum interfaceType : {GL_UNIFORM, GL_BUFFER_VARIABLE, GL_PROGRAM_INPUT, GL_PROGRAM_OUTPUT})
        {
            GLint count = 0; glGetProgramInterfaceiv(program, interfaceType, GL_ACTIVE_RESOURCES, &count);
            for (GLint i = 0; i < count; ++i)
            {
                const GLenum base[]{GL_TYPE, GL_ARRAY_SIZE}; GLint values[2]{};
                glGetProgramResourceiv(program, interfaceType, i, 2, base, 2, nullptr, values);
                ShaderReflection value;
                value.name = name(interfaceType, i); value.type = ValueType(values[0]); value.elements = values[1];
                if (interfaceType == GL_PROGRAM_INPUT || interfaceType == GL_PROGRAM_OUTPUT)
                {
                    value.kind = interfaceType == GL_PROGRAM_INPUT ? ShaderResourceKind::VertexInput : ShaderResourceKind::FragmentOutput;
                    const GLenum property = GL_LOCATION; GLint location = -1;
                    glGetProgramResourceiv(program, interfaceType, i, 1, &property, 1, nullptr, &location);
                    if (interfaceType == GL_PROGRAM_INPUT)
                    {
                        if (location == 0) value.semantic = ShaderSemantic::Position;
                        if (location == 1) value.semantic = ShaderSemantic::TexCoord;
                        if (location == 2) value.semantic = ShaderSemantic::Normal;
                    }
                    else if (location == 0) value.semantic = ShaderSemantic::Color;
                }
                else
                {
                    const GLenum props[]{GL_BLOCK_INDEX, GL_OFFSET, GL_ARRAY_STRIDE, GL_MATRIX_STRIDE}; GLint layout[4]{};
                    glGetProgramResourceiv(program, interfaceType, i, 4, props, 4, nullptr, layout);
                    if (layout[0] >= 0)
                    {
                        value.kind = ShaderResourceKind::BlockMember;
                        value.block = name(interfaceType == GL_UNIFORM ? GL_UNIFORM_BLOCK : GL_SHADER_STORAGE_BLOCK, layout[0]);
                        value.byteOffset = layout[1]; value.arrayStride = layout[2]; value.matrixStride = layout[3];
                        if (interfaceType == GL_BUFFER_VARIABLE)
                        {
                            const GLenum property = GL_TOP_LEVEL_ARRAY_STRIDE; GLint stride = 0;
                            glGetProgramResourceiv(program, interfaceType, i, 1, &property, 1, nullptr, &stride);
                            if (stride) value.arrayStride = stride;
                        }
                    }
                }
                result.push_back(std::move(value));
            }
        }
        for (const auto& binding : bindings)
        {
            const bool input = binding.semantic == ShaderSemantic::Position || binding.semantic == ShaderSemantic::TexCoord || binding.semantic == ShaderSemantic::Normal;
            const auto kind = input ? ShaderResourceKind::VertexInput : binding.semantic == ShaderSemantic::Color ? ShaderResourceKind::FragmentOutput : ShaderResourceKind::Uniform;
            auto found = std::find_if(result.begin(), result.end(), [&](const auto& item) { return item.name == binding.name && item.kind == kind; });
            if (found == result.end())
            {
                if (binding.required) return Failure(ShaderErrorCode::MissingBinding, {}, binding.name, "Required semantic is absent or inactive");
                ShaderReflection inactive; inactive.name = binding.name; inactive.kind = kind;
                inactive.type = binding.type; inactive.semantic = binding.semantic; inactive.active = false;
                result.push_back(std::move(inactive));
            }
            else
            {
                if (found->type != binding.type || found->elements != 1 ||
                    ((input || binding.semantic == ShaderSemantic::Color) && found->semantic != binding.semantic))
                    return Failure(ShaderErrorCode::BindingType, {}, binding.name, "Reflected type, extent or mesh/output semantic disagrees with description");
                found->semantic = binding.semantic;
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return std::tie(a.kind, a.block, a.name) < std::tie(b.kind, b.block, b.name); });
        return result;
    }
}

namespace GEngine
{
    std::expected<MaterialShaderDescription, Asset::ShaderError> MaterialShaderDescription::Create(const MaterialShaderDesc& desc)
    {
        using namespace Asset;
        const auto failure = [](std::string source, std::string log) {
            return std::unexpected(ShaderError{ShaderErrorCode::InvalidInput, {}, std::move(source), std::move(log)});
        };
        if (desc.stages.size() != 2 ||
            std::count_if(desc.stages.begin(), desc.stages.end(), [](const auto& s) { return s.type == VERTEX; }) != 1 ||
            std::count_if(desc.stages.begin(), desc.stages.end(), [](const auto& s) { return s.type == FRAGMENT; }) != 1)
            return failure({}, "Custom materials require a vertex/fragment pair");
        MaterialShaderDescription result;
        std::vector<ShaderBindingDecl> bindings{
            {"u_model", ShaderValueType::Matrix4, ShaderSemantic::Model},
            {"u_view", ShaderValueType::Matrix4, ShaderSemantic::View},
            {"u_projection", ShaderValueType::Matrix4, ShaderSemantic::Projection},
            {"aPos", ShaderValueType::Float3, ShaderSemantic::Position},
            {"aTexCoords", ShaderValueType::Float2, ShaderSemantic::TexCoord, false},
            {"aNormal", ShaderValueType::Float3, ShaderSemantic::Normal, false},
            {"FragColor", ShaderValueType::Float4, ShaderSemantic::Color}};
        const auto reserved = [](std::string_view name) {
            return name.starts_with("ge") || name.starts_with("gl_") || name.starts_with("frame") || name.starts_with("u_") ||
                name == "aPos" || name == "aTexCoords" || name == "aNormal" || name == "FragColor" ||
                name == "viewPos" || name == "lightDir" || name == "lightPos" || name == "reverse_normals" ||
                name == "farPlane" || name == "far_plane" || name == "pointShadowfarPlane" || name == "cascadeCount" ||
                name == "cascadePlaneDistances" || name == "lightSpaceMatrices" || name == "shadowMatrices" ||
                name == "directionallightColor" || name == "pointlightColor" || name.starts_with("spot");
        };
        constexpr ShaderValueType types[]{ShaderValueType::Boolean, ShaderValueType::Integer, ShaderValueType::UnsignedInteger,
            ShaderValueType::Float, ShaderValueType::Float2, ShaderValueType::Float3, ShaderValueType::Float4, ShaderValueType::Matrix4};
        for (const auto& entry : desc.parameters)
        {
            const auto& parameter = entry.declaration;
            const auto type = static_cast<std::size_t>(parameter.type);
            if (reserved(parameter.name) || type >= std::size(types) || parameter.defaultValue.index() != type)
                return failure(parameter.name, "Material schema type/default mismatch or reserved engine semantic");
            const bool finite = std::visit([](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::integral<T>) return true;
                else if constexpr (std::same_as<T, float>) return std::isfinite(value);
                else return std::all_of(value.begin(), value.end(), [](float v) { return std::isfinite(v); });
            }, parameter.defaultValue);
            if (!finite) return failure(parameter.name, "Material defaults must be finite");
            result.m_Parameters.push_back(parameter);
            bindings.push_back({parameter.name, types[type], ShaderSemantic::Parameter, entry.required});
        }
        for (const auto& texture : desc.textures)
        {
            if (reserved(texture.name) || (texture.defaultValue && (!texture.defaultValue->texture || !texture.defaultValue->sampler)))
                return failure(texture.name, "Invalid texture default or reserved engine semantic");
            result.m_Textures.push_back(texture);
            bindings.push_back({texture.name, ShaderValueType::Sampler2D, ShaderSemantic::Texture, texture.required});
        }
        std::sort(result.m_Parameters.begin(), result.m_Parameters.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        std::sort(result.m_Textures.begin(), result.m_Textures.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        auto program = ShaderDescription::Create({desc.stages, bindings, desc.variants});
        if (!program) return std::unexpected(program.error());
        result.m_Program = std::move(*program);
        return result;
    }
}
