#pragma once
#include "Core/Entity.h"

namespace GEngine
{

    class Grid: public Entity
    {
        static Geometry* CreateGeometry(float size, int divisions, 
            const Vec3f grid_color, const Vec3f& center_color_x, const Vec3f& center_color_z);

    public:
        [[nodiscard]] static std::expected<std::unique_ptr<Grid>, Asset::ShaderError> Create(float size = 10.f, int divisions = 10, float line_width = 1.0f,
             const Vec3f& grid_color = { .2f, .2f, .2f },
             const Vec3f& center_color_x = { .4f, 0.0f, 0.0f }, 
             const Vec3f& center_color_z = { 0.0f, .4f, 0.0f });
    private:
        Grid(RefPtr<Material> material, float size, int divisions, float line_width,
            const Vec3f& grid_color, const Vec3f& center_color_x, const Vec3f& center_color_z);
    public:



        ~Grid() override;
        void Render(CameraBase* camera) override;
    };

}
