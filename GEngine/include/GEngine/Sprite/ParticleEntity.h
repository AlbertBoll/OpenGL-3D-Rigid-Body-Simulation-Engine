#include "Core/Actor.h"
#include <Material/Material.h>

namespace GEngine
{
	class Geometry;

	class ParticleEntity : public Actor
	{
	private:
		Geometry* m_Geometry{};
		RefPtr<Material> m_Material{};
		ParticleComponent m_ParticleComp;

	public:
		ParticleEntity();
		ParticleEntity(Geometry* geometry, const RefPtr<Material>& material);

		ParticleEntity(const ParticleEntity& other) = default;
		//operator = ()

		[[nodiscard]] Material* GetMaterial() const { return m_Material.get(); }
		[[nodiscard]] Geometry* GetGeometry() const { return m_Geometry; }

		void BindVAO()const;
		void UnBindVAO()const;
		void UseShaderProgram()const;

		void Update(Timestep ts)override;

		void UpdateRenderSettings() const;
		void ArraysDraw() const;
		void Render(CameraBase* camera)override;

		ParticleEntity& SetParticleComponent(const ParticleComponent& particle)
		{
			m_ParticleComp = particle;
			return *this;
		}

		ParticleComponent& GetParticleComponent() { return m_ParticleComp; }

	};
}