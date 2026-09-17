#include "gepch.h"
#include "ShaderBackend.h"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <utility>

namespace GEngine::Asset
{
    namespace
    {
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
        GLint count{};
        glGetProgramInterfaceiv(m_Storage->program, GL_UNIFORM, GL_ACTIVE_RESOURCES, &count);
        constexpr GLenum properties[] = {GL_NAME_LENGTH, GL_LOCATION, GL_BLOCK_INDEX};
        for (GLint i = 0; i < count; ++i)
        {
            GLint values[3]{};
            glGetProgramResourceiv(m_Storage->program, GL_UNIFORM, i, 3, properties, 3, nullptr, values);
            if (values[2] != -1) continue;
            std::string name(static_cast<std::size_t>(values[0]), '\0');
            GLsizei written{};
            glGetProgramResourceName(m_Storage->program, GL_UNIFORM, i, values[0], &written, name.data());
            name.resize(static_cast<std::size_t>(written));
            m_Storage->uniforms.emplace(std::move(name), values[1]);
        }
    }
}
