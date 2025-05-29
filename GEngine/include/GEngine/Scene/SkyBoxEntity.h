#pragma once
#include "Core/Entity.h"
#include "Managers/ShapeManager.h"

namespace GEngine
{

    class SkyBoxMaterial;

    class SkyBoxEntity: public Entity
    {
    private:
        static RefPtr<Material> GetSkyBoxMaterial(const SkyBoxComponent& comp);


    public:
        SkyBoxEntity(const SkyBoxComponent& comp, Geometry* geometry = Manager::ShapeManager::GetShape("SkyBox"), const RefPtr<Material>& material = nullptr);

        void Render(CameraBase* camera);

        void SetSkyBoxComponent(const SkyBoxComponent& comp);
     
        SkyBoxComponent& GetSkyBoxComponent();

    };

}