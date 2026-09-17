#include "gepch.h"
#include "ShaderBackend.h"
#include <glm/gtc/type_ptr.hpp>
#include <utility>

namespace GEngine::Asset
{
    void ReportShaderError(const ShaderError& error)
    {
        GENGINE_CORE_ERROR("Shader code={} stage={} source={}: {}", static_cast<int>(error.code),
            error.shaderType ? static_cast<int>(*error.shaderType) : 0, error.source, error.log);
    }
    void ShaderStorage::RequireOwner() const noexcept
    {
        GLContextThread::RequireOwner(thread, "shader owner");
        AssetDetail::RequireInvariant(context && currentContext && context == currentContext());
    }
    void ShaderStorage::RetireStages() noexcept
    {
        for (auto stage : stages) { glDetachShader(program, stage); glDeleteShader(stage); }
        stages.clear();
    }
    ShaderStorage::~ShaderStorage()
    { if (program) { RequireOwner(); RetireStages(); glDeleteProgram(program); } }
    Shader::Shader() noexcept = default;
    Shader::~Shader() = default;
    Shader::Shader(Shader&&) noexcept = default;
    Shader& Shader::operator=(Shader&&) noexcept = default;
    void Shader::Destroy() noexcept { m_Storage.reset(); }
    bool Shader::IsLinked() const noexcept { return m_Storage && m_Storage->linked; }

    void Shader::Bind() const
    { AssetDetail::RequireInvariant(IsLinked()); m_Storage->RequireOwner(); glUseProgram(m_Storage->program); }
    void Shader::UnBind() const
    { AssetDetail::RequireInvariant(IsLinked()); m_Storage->RequireOwner(); glUseProgram(0); }
    bool Shader::HasUniform(std::string_view name) const
    { return IsLinked() && m_Storage->uniforms.contains(std::string(name)); }
    int Shader::GetUniformLocation(const char* name)
    {
        AssetDetail::RequireInvariant(IsLinked() && name);
        m_Storage->RequireOwner();
        if (const auto found = m_Storage->uniforms.find(name); found != m_Storage->uniforms.end()) return found->second;
        const auto location = glGetUniformLocation(m_Storage->program, name);
        m_Storage->uniforms.emplace(name, location);
        return location;
    }
#define SCALAR(TYPE, FN) template<> void Shader::Set<TYPE>(const char* name, const TYPE* data, unsigned int count) \
    { FN(GetUniformLocation(name), static_cast<GLsizei>(count), data); }
    SCALAR(int, glUniform1iv)
    SCALAR(unsigned int, glUniform1uiv)
    SCALAR(float, glUniform1fv)
    SCALAR(double, glUniform1dv)
#undef SCALAR
    template<> void Shader::Set<bool>(const char* name, const bool* data, unsigned int)
    { glUniform1i(GetUniformLocation(name), *data); }
#define VECTOR(TYPE, FN) template<> void Shader::Set<Math::TYPE>(const char* name, const Math::TYPE* data, unsigned int count) \
    { FN(GetUniformLocation(name), static_cast<GLsizei>(count), count ? glm::value_ptr(*data) : nullptr); }
    VECTOR(Vec2i, glUniform2iv) VECTOR(Vec3i, glUniform3iv) VECTOR(Vec4i, glUniform4iv)
    VECTOR(Vec2f, glUniform2fv) VECTOR(Vec3f, glUniform3fv) VECTOR(Vec4f, glUniform4fv)
    VECTOR(Quat, glUniform4fv)
#undef VECTOR
#define MATRIX(TYPE, FN) template<> void Shader::Set<Math::TYPE>(const char* name, const Math::TYPE* data, unsigned int count) \
    { FN(GetUniformLocation(name), static_cast<GLsizei>(count), false, count ? glm::value_ptr(*data) : nullptr); }
    MATRIX(Mat2, glUniformMatrix2fv) MATRIX(Mat3, glUniformMatrix3fv) MATRIX(Mat4, glUniformMatrix4fv)
#undef MATRIX
    void ShaderBackendAccess::BindTexture(Shader& shader, const char* uniform, GLenum target, GLuint name, GLuint unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(target, name);
        if (uniform) shader.SetUniform(uniform, static_cast<int>(unit));
    }
}
