#pragma once

#include "Assets/AssetHandle.h"
#include "Math/Math.h"
#include <glm/gtc/type_ptr.hpp>
#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GEngine::Asset
{
    enum class ShaderStage { Vertex = 1, Fragment, Geometry, TessellationControl, TessellationEvaluation, Compute };
    using ShaderType = ShaderStage;
    inline constexpr auto VERTEX = ShaderStage::Vertex, FRAGMENT = ShaderStage::Fragment,
        GEOMETRY = ShaderStage::Geometry, TESS_CONTROL = ShaderStage::TessellationControl,
        TESS_EVALUATION = ShaderStage::TessellationEvaluation, COMPUTE = ShaderStage::Compute;

    enum class ShaderErrorCode { InvalidInput, InvalidState, FileRead, ProgramAllocation, ShaderAllocation,
        Compile, Link, ContextUnavailable, WrongThread, Validation, UniformRange,
        MissingBinding, BindingType, VariantNotAdmitted, Capacity };
    struct ShaderError
    {
        ShaderErrorCode code;
        std::optional<ShaderStage> shaderType;
        std::string source;
        std::string log;
    };
    using ShaderCreationCode = ShaderErrorCode;
    using ShaderCreationError = ShaderError;
    using ShaderResult = std::expected<void, ShaderError>;
    void ReportShaderError(const ShaderError& error);
    struct ShaderSource { ShaderStage type; std::string_view source; std::string_view label; };
    struct ShaderProgramDesc { std::span<const ShaderSource> stages; };

    enum class ShaderValueType { Unknown, Boolean, Integer, UnsignedInteger, Float,
        Float2, Float3, Float4, Matrix4, Sampler2D, SamplerCube, Sampler2DArray, UInt4 };
    enum class ShaderSemantic { Parameter, Texture, Model, View, Projection, Position, TexCoord, Normal, Color };
    struct ShaderBindingDecl
    {
        std::string name;
        ShaderValueType type = ShaderValueType::Float;
        ShaderSemantic semantic = ShaderSemantic::Parameter;
        bool required = true;
        bool operator==(const ShaderBindingDecl&) const = default;
    };
    struct ShaderDefine
    {
        std::string name;
        std::int32_t value = 0;
        bool operator==(const ShaderDefine&) const = default;
    };
    struct ShaderVariantKey { std::uint32_t value = 0; bool operator==(const ShaderVariantKey&) const = default; };
    struct ShaderVariant { ShaderVariantKey key; std::vector<ShaderDefine> defines; };
    struct OwnedShaderSource { ShaderStage type; std::string source, label; };
    struct ShaderVariantSource
    {
        ShaderVariantKey key;
        std::vector<OwnedShaderSource> stages;
        // Exact, length-delimited canonical bytes. Labels and instance values are excluded.
        std::string identity;
    };
    struct ShaderDescriptionDesc
    {
        std::span<const ShaderSource> stages;
        std::span<const ShaderBindingDecl> bindings;
        std::span<const ShaderVariant> variants; // Empty means the single default key 0.
    };
    // Owned CPU description; no context work. Only the explicitly admitted variants
    // are canonicalized once. Selection never generates a permutation or source.
    class ShaderDescription final
    {
    public:
        static std::expected<ShaderDescription, ShaderError> Create(const ShaderDescriptionDesc&);
        std::expected<const ShaderVariantSource*, ShaderError> Select(ShaderVariantKey = {}) const;
        std::span<const ShaderBindingDecl> Bindings() const noexcept { return m_Bindings; }
        std::size_t VariantCount() const noexcept { return m_Variants.size(); }
    private:
        std::vector<ShaderBindingDecl> m_Bindings;
        std::vector<ShaderVariantSource> m_Variants;
    };
    enum class ShaderResourceKind { Uniform, VertexInput, FragmentOutput, UniformBlock, StorageBlock, BlockMember };
    struct ShaderReflection
    {
        std::string name, block;
        ShaderResourceKind kind = ShaderResourceKind::Uniform;
        ShaderValueType type = ShaderValueType::Unknown;
        std::uint32_t elements = 1, byteOffset = 0, arrayStride = 0, matrixStride = 0, blockBytes = 0;
        std::optional<ShaderSemantic> semantic;
        bool active = true;
    };
    struct ShaderCreationWork
    {
        std::uint64_t descriptions = 0, stageCompilations = 0, links = 0, reflectionPasses = 0;
        bool operator==(const ShaderCreationWork&) const = default;
    };

    template<class T>
    concept ShaderUniform = std::same_as<T, int> || std::same_as<T, unsigned int>
        || std::same_as<T, float> || std::same_as<T, double> || std::same_as<T, bool>
        || std::same_as<T, Math::Vec1i> || std::same_as<T, Math::Vec2i>
        || std::same_as<T, Math::Vec3i> || std::same_as<T, Math::Vec4i>
        || std::same_as<T, Math::Vec1f> || std::same_as<T, Math::Vec2f>
        || std::same_as<T, Math::Vec3f> || std::same_as<T, Math::Vec4f>
        || std::same_as<T, Math::Quat> || std::same_as<T, Math::Mat2>
        || std::same_as<T, Math::Mat3> || std::same_as<T, Math::Mat4>;

    struct ShaderStorage;
    struct ShaderBackendAccess;
    // Move-only program owner. Compilation/binding/destruction belong to its context thread.
    // A linked owner cannot be incrementally modified; replacements use a fresh candidate.
    class Shader
    {
    public:
        Shader() noexcept;
        ~Shader();
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&&) noexcept;
        Shader& operator=(Shader&&) noexcept;
        [[nodiscard]] static std::expected<Shader, ShaderError> Create(const ShaderProgramDesc&);
        [[nodiscard]] static std::expected<Shader, ShaderError> Create(const ShaderDescription&, ShaderVariantKey = {});
        // Explicit creation-time inspection. Callers retain the returned CPU interface.
        [[nodiscard]] std::expected<std::vector<ShaderReflection>, ShaderError>
            Reflect(std::span<const ShaderBindingDecl> = {}) const;
        static ShaderCreationWork CreationWork() noexcept;
        [[nodiscard]] ShaderResult CompileShader(const char* file);
        [[nodiscard]] ShaderResult CompileShader(const char* file, ShaderStage stage);
        [[nodiscard]] ShaderResult CompileShader(const std::string& source, ShaderStage stage, const char* label);
        [[nodiscard]] ShaderResult Link();
        [[nodiscard]] ShaderResult Validate() const;
        [[nodiscard]] bool IsLinked() const noexcept;
        [[nodiscard]] bool HasUniform(std::string_view name) const;
        void Bind() const;
        void UnBind() const;
        void Destroy() noexcept;
        template<ShaderUniform T>
        void SetUniform(const char* name, const T& data) { Set(name, &data, 1); }
        template<ShaderUniform T> requires (!std::same_as<T, bool>)
        [[nodiscard]] ShaderResult SetUniform(const char* name, const std::vector<T>& data)
        {
            if (data.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                return std::unexpected(ShaderError{ShaderErrorCode::UniformRange, {}, name ? name : "",
                    "Uniform array exceeds the supported element count"});
            Set(name, data.data(), static_cast<unsigned int>(data.size()));
            return {};
        }
    private:
        friend struct ShaderBackendAccess;
        std::unique_ptr<ShaderStorage> m_Storage;
        int GetUniformLocation(const char* name);
        void FindUniformLocations();
        template<ShaderUniform T> void Set(const char*, const T*, unsigned int);
    };
    using ShaderProgram = Shader;
    using ShaderCreationResult = std::expected<ShaderProgram, ShaderError>;
    [[nodiscard]] ShaderCreationResult CreateShaderProgram(std::span<const ShaderSource> sources);
    [[nodiscard]] ShaderCreationResult CreateShaderProgramFromFiles(std::span<const std::string> files);
}
