#include "gepch.h"
#include "../Assets/ShaderBackend.h"
#include <Assets/Shaders/Shader.h>
#include "Material/Material.h"
#include "Managers/ShaderManager.h"
#include "Managers/AssetsManager.h"
#include <Core/Actor.h>


namespace GEngine
{
	using namespace Manager;
    RenderSetting::RenderSetting() : m_TexTarget(GL_TEXTURE_2D) {}
	Material::Material(Construction& construction, const std::string& vertexFileName, const std::string& fragFileName)
	{
		auto resolve = [](const std::string& file, Asset::ShaderStage stage) -> std::expected<std::string, Asset::ShaderError>
        {
            if (!file.starts_with("Shaders/")) return file;
            auto path = RuntimeAssets::TryFile(file);
            if (!path) return std::unexpected(Asset::ShaderError{Asset::ShaderErrorCode::FileRead, stage, file, path.error().message});
            return std::move(*path);
        };
        auto vertex = resolve(vertexFileName, Asset::VERTEX);
        if (!vertex) { construction.result = std::unexpected(std::move(vertex.error())); return; }
        auto fragment = resolve(fragFileName, Asset::FRAGMENT);
        if (!fragment) { construction.result = std::unexpected(std::move(fragment.error())); return; }
        auto shader = ShaderManager::GetShaderProgram({ *vertex, *fragment });
        if (!shader) { construction.result = std::unexpected(std::move(shader.error())); return; }
        m_Shader = *shader;

		RenderSetting setting;

		SetRenderSettings(setting);

	}

	unsigned int Material::GetShaderRef() const
	{
		return Asset::ShaderBackendAccess::Program(*m_Shader);
	}

	void Material::UseProgram() const
	{
		m_Shader->Bind();
	}

    std::expected<void, Asset::SamplingError> Material::SetTextureBinding(const std::string& uniform,
        const Asset::TextureView& view, std::uint32_t unit)
    {
        auto binding = AssetsManager::SampleTexture(view);
        if (!binding) return std::unexpected(binding.error());
        return SetSampledTextureBinding(uniform, *binding, unit);
    }

    std::expected<void, Asset::SamplingError> Material::SetSampledTextureBinding(const std::string& uniform,
        const Asset::SampledTextureBinding& binding, std::uint32_t unit)
    {
        auto result = binding.Bind(unit);
        if (!result) return result;
        // Unit order is stable regardless of descriptor/cache insertion order.
        auto position = std::lower_bound(m_ImageBindings.begin(), m_ImageBindings.end(), unit,
            [](const auto& entry, std::uint32_t value) { return entry.second < value; });
        if (position != m_ImageBindings.end() && position->second == unit) position->first = binding;
        else m_ImageBindings.insert(position, {binding, unit});
        m_Shader->SetUniform(uniform.c_str(), static_cast<int>(unit));
        return {};
    }

	void Material::BindTextureUniforms(int TexTarget)
	{
        for (const auto& [view, unit] : m_ImageBindings)
            if (auto bound = view.Bind(unit); !bound)
                GENGINE_CORE_ERROR("Texture binding: {}", bound.error().message);

		for (auto& ele : m_TextureList)
		{
			glBindSampler(ele.second.second, 0); // Legacy target bindings retain their authored texture state.
			Asset::ShaderBackendAccess::BindTexture(*m_Shader, nullptr, TexTarget, ele.second.first, ele.second.second);
			
		}
	}

	void Material::BindTextureUniforms()
	{
        for (const auto& [view, unit] : m_ImageBindings)
            if (auto bound = view.Bind(unit); !bound)
                GENGINE_CORE_ERROR("Texture binding: {}", bound.error().message);

		for (auto& ele : m_TextureList)
		{
			glBindSampler(ele.second.second, 0);
			Asset::ShaderBackendAccess::BindTexture(*m_Shader, nullptr, ele.first, ele.second.first, ele.second.second);
		}
	}

	Material::Material(Material&& other)
	{
		m_RenderSetting = other.m_RenderSetting;
		m_Shader = std::move(other.m_Shader);
        m_ImageBindings.swap(other.m_ImageBindings);
		
		other.m_Shader = nullptr;
	}
	Material& Material::operator=(Material&& other) noexcept
	{
		if (this != &other)
		{
			m_Shader = other.m_Shader;
            m_ImageBindings.clear(); m_ImageBindings.swap(other.m_ImageBindings);
			other.m_Shader = nullptr;
			m_RenderSetting = other.m_RenderSetting;
		}

		return *this;
	}



    unsigned int Material::GetShaderID() const { return GetShaderRef(); }

}
