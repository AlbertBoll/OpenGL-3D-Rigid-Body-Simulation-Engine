#pragma once

#include <unordered_map>
#include <Assets/Shaders/Shader.h>
#include"Component/Component.h"



namespace GEngine
{
	//using namespace Graphic;
	//class GEngine::Graphic::Shader;

	class Actor;

	enum class DrawMode
	{
		Points,
		LINES,
		LINE_LOOP,
		LINE_STRIP, 
		TRIANGLES,
		TRIANGLE_STRIP,
		TRIANGLE_FAN
	};

	enum class RenderMode
	{
		Arrays,
		Elements,
		ArraysInstanced,
		ElementsInstanced
	};

	enum class LineType
	{
		Connected,
		Loop,
		Segments
	};

	enum class DrawType
	{
		Point,
		Line,
		Surface
	};

	struct MaterialProperty
	{
		float ShineDamper = 1.0f;
		float Reflectivity = 0.f;
		bool HasTransparency = false;
		bool HasFakeLighting = false;

	};

	struct PointSetting
	{
		float pointSize{ 8.0f };
		bool bRoundedPoints = true;
	};

	struct LineSetting
	{
		float lineWidth = 1.0f;
		LineType lineType;
	};

	struct SurfaceSetting
	{
		float lineWidth = 1.0f;
		bool bDoubleSide = true;
		bool bWireFrame = false;
	};

	union PrimitivesSettings
	{
		PointSetting pointSetting;
		LineSetting lineSetting;
		SurfaceSetting surfaceSetting;
		

		PrimitivesSettings() {}
	};

	struct RenderSetting
	{
		PrimitivesSettings m_PrimitivesSetting;
		RenderMode m_RenderMode = RenderMode::Arrays;
		int m_TexTarget = 0x0DE1;
		DrawMode m_Mode = DrawMode::TRIANGLES;
		
	};

	class Material
	{

	protected:

		friend class Entity;
		friend class SpriteEntity;
		MaterialProperty m_MaterialProp;
		RenderSetting m_RenderSetting;
		Asset::Shader* m_Shader{};
		
		std::unordered_multimap<unsigned int, std::pair<unsigned int, unsigned int>> m_TextureList;

		inline static std::string base_shader_dir = "../GEngine/include/GEngine/Assets/Shaders/";



	public:
		Material() = default;
		virtual ~Material() = default;
		Material(const std::string& vertexFileName, const std::string& fragFileName);

		Material(Material&& other)noexcept;

		Material& operator=(Material&& other)noexcept;

		RenderSetting& GetRenderSetting() { return m_RenderSetting; }

		virtual void UpdateRenderSettings() {}

		void SetTransparency(bool hasTransparency)
		{
			m_MaterialProp.HasTransparency = hasTransparency;
		}

		void SetFakeLighting(bool useFakeLighting)
		{
			m_MaterialProp.HasFakeLighting = useFakeLighting;
		}

		unsigned int GetShaderID()const
		{
			return m_Shader->GetHandle();
		}

		bool IsTransparency()const { return m_MaterialProp.HasTransparency; }

		bool IsFakeLighting()const { return m_MaterialProp.HasFakeLighting; }

		void SetRenderMode(RenderMode mode) { m_RenderSetting.m_RenderMode = mode; }

		MaterialProperty& GetMaterialProperty() { return m_MaterialProp; }




		template <typename T>
		void SetUniforms(const std::map<std::string, T>& uniforms);

		template <typename T>
		void SetUniforms(const std::string& uniformName, const std::vector<T>& uniforms);

		template <typename T>
		void SetUniforms(const std::map<std::string, std::vector<T>>& uniforms);


		virtual void SetRenderSettings(const RenderSetting& settings) { m_RenderSetting = settings; }
		std::unordered_multimap<unsigned, std::pair<unsigned, unsigned>>& GetTextureList() { return m_TextureList; }

		[[nodiscard]] unsigned int GetShaderRef() const;

		[[nodiscard]] int GetTextureTarget() const { return m_RenderSetting.m_TexTarget; }

		void SetTextureTarget(int target) { m_RenderSetting.m_TexTarget = target; }

		void UseProgram()const;

		void SetMaterialProperty(const MaterialProperty& prop)
		{
			m_MaterialProp = prop;
		}


		virtual void UploadUniforms(){}

		void BindTextureUniforms(int TexTarget);
		void BindTextureUniforms();
		virtual void UseUniformBufferObject(const std::string& uniformBlockName) {}
	};

	template<typename T>
	inline void Material::SetUniforms(const std::map<std::string, T>& uniforms)
	{
		//UseProgram();
		for (auto& [uniformName, ele] : uniforms)
		{
			//if (auto name = m_Shader->GetUniformLocations().find(uniformName); name != m_Shader->GetUniformLocations().end())
			//{
			m_Shader->SetUniform(uniformName.c_str(), ele);
				//m_TextureList.emplace(1, 1);
			//}
		}

	}


	template<typename T>
	inline void Material::SetUniforms(const std::map<std::string, std::vector<T>>& uniforms)
	{
		//UseProgram();
		for (auto& [uniformName, ele] : uniforms)
		{
			//if (auto name = m_Shader->GetUniformLocations().find(uniformName); name != m_Shader->GetUniformLocations().end())
			//{
				m_Shader->SetUniform(uniformName.c_str(), ele);
				//m_TextureList.emplace(1, 1);
			//}
		}

		
	}

	template<typename T>
	inline void Material::SetUniforms(const std::string& uniformName, const std::vector<T>& uniforms)
	{
	
		//if (auto name = m_Shader->GetUniformLocations().find(uniformName); name != m_Shader->GetUniformLocations().end())
		//{
		m_Shader->SetUniform(uniformName.c_str(), uniforms);
			
		//}
	

	}


	template <>
	inline void Material::SetUniforms<std::pair<unsigned, std::pair<unsigned, unsigned>>>(
		const std::map<std::string, std::pair<unsigned, std::pair<unsigned, unsigned>>>& uniforms)
	{
		UseProgram();
		for (auto& ele : uniforms)
		{
			m_Shader->SetUniform(ele.first.c_str(), ele.second);
			m_TextureList.emplace(ele.second);
		}

	}


}