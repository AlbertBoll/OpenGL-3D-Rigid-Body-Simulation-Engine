#pragma once
#include "Assets/ModelImportError.h"
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace GEngine
{
    class Geometry;

    class RawModel
    {
    public:
        [[nodiscard]] static std::expected<RawModel, ModelImportError> Create(const std::string& path);
        RawModel(const RawModel&) = delete;
        RawModel& operator=(const RawModel&) = delete;
        RawModel(RawModel&&) noexcept;
        RawModel& operator=(RawModel&&) noexcept;
        ~RawModel();
        // Transfer imported meshes to the caller; untransferred meshes retire here.
        [[nodiscard]] std::vector<std::unique_ptr<Geometry>> TakeGeometries() &&;
    private:
        RawModel() = default;
        std::vector<std::unique_ptr<Geometry>> m_Geometries;
    };
}
