#pragma once
#include "Core/Entity.h"
#include "Core/RenderTarget.h"
#include "Managers/ShapeManager.h"

namespace GEngine
{

    class SkyBoxMaterial;

    class SkyBoxEntity: public Entity
    {
    private:
        static std::expected<RefPtr<Material>, ApplicationInitializationError> GetSkyBoxMaterial(const SkyBoxComponent& comp);
        SkyBoxComponent m_Description;


    public:
        [[nodiscard]] static std::expected<std::unique_ptr<SkyBoxEntity>, ApplicationInitializationError> Create(
            const SkyBoxComponent& comp);
        [[nodiscard]] static std::expected<std::unique_ptr<SkyBoxEntity>, ApplicationInitializationError> Create(
            const SkyBoxComponent& comp, Geometry* geometry);
    private:
        SkyBoxEntity(const SkyBoxComponent& comp, Geometry* geometry, const RefPtr<Material>& material);
    public:

        void Render(CameraBase* camera);

        void SetSkyBoxComponent(const SkyBoxComponent& comp);
     
        SkyBoxComponent& GetSkyBoxComponent();

    };

}