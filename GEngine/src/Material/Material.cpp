#include "gepch.h"
#include <Assets/Shaders/Shader.h>
#include "Material/Material.h"
#include "Managers/ShaderManager.h"
#include "Managers/AssetsManager.h"
#include <Core/Actor.h>


namespace GEngine
{
	using namespace Manager;
	Material::Material(const std::string& vertexFileName, const std::string& fragFileName)
	{
		m_Shader = ShaderManager::GetShaderProgram({ vertexFileName, fragFileName });

		RenderSetting setting;

		SetRenderSettings(setting);

	}

	unsigned int Material::GetShaderRef() const
	{
		return m_Shader->GetHandle();
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
			m_Shader->BindTextureUniform(ele.second.first, ele.second.second, TexTarget);
			
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
			m_Shader->BindTextureUniform(ele.second.first, ele.second.second, ele.first);
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



}
