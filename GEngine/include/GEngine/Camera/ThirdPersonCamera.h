#pragma once
#include "Camera/PerspectiveCamera.h"

namespace GEngine
{
    class Character;
}

namespace GEngine::Camera
{

    class ThirdPersonCamera: public PerspectiveCamera
    {
    public:
        ThirdPersonCamera(float field_of_view = 45.f, float aspect_ratio = 1.0f, float near_field = 0.01f, float far_field = 1000.f, float zoom_level = 1.0f);
        void SetPlayer(Character* player);
        virtual void OnScroll(float new_zoom_level) override;


    private:
        Character* m_Player;
        float m_Distance = 20.f;
    };

}