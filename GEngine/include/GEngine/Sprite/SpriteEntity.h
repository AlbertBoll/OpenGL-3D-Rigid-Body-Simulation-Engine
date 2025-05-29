#pragma once
#include "Core/Actor.h"
#include <Material/Material.h>

namespace GEngine
{


	class Geometry;

	class SpriteEntity: public Actor
	{
	protected:
		Geometry* m_Geometry{};
		RefPtr<Material> m_Material{};
		SpriteComponent m_SpriteComp;

		bool m_IsDestroy{false};
		bool m_IsSolid{false};

	public:
		//NONCOPYABLE(SpriteEntity);
		SpriteEntity();
		SpriteEntity(Geometry* geometry, const RefPtr<Material>& material);

		SpriteEntity(const SpriteEntity& other) = default;
		//operator = ()

		[[nodiscard]] Material* GetMaterial() const { return m_Material.get(); }
		[[nodiscard]] Geometry* GetGeometry() const { return m_Geometry; }

		void BindVAO()const;
		void UnBindVAO()const;
		void UseShaderProgram()const;

		void UpdateRenderSettings() const;
		void ArraysDraw() const;
		void Render(CameraBase* camera)override;

		bool GetIsDestroy()const { return m_IsDestroy; }
		void SetIsDestroy(bool destroy) { m_IsDestroy = destroy; }

		bool GetIsSolid()const { return m_IsSolid; }
		void SetIsSolid(bool solid) { m_IsSolid = solid; }

		SpriteEntity& SetSpriteComponent(const SpriteComponent& sprite)
		{
			m_SpriteComp = sprite;
			return *this;
		}

		SpriteComponent& GetSpriteComponent() { return m_SpriteComp; }

		void Set2DTransform(const Vec2f& pos, const Vec2f& size, float rotation = 0);

		template <typename T>
		void SetUniforms(const std::map<std::string, T>& uniforms)const
		{
			m_Material->SetUniforms(uniforms);
		}

	};

}

