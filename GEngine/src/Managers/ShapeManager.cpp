#include "gepch.h"
#include "Core/RuntimeAssets.h"
#include "Managers/ShapeManager.h"
#include "Core/GEngine.h"
#include "Scene/_Entity.h"
#include <stdexcept>
#include <Shapes/Box.h>
#include <Shapes/Circle.h>
#include <Shapes/Cone.h>
#include <Shapes/Ellipsoid.h>
#include <Shapes/Hexagon.h>
#include <Shapes/Icosahedron.h>
#include <Shapes/Plane.h>
#include <Shapes/Prism.h>
#include <Shapes/Pyramid.h>
#include <Shapes/Ring.h>
#include <Shapes/SmoothSphere.h>
#include <Shapes/Torus.h>
#include <Shapes/Cylinder.h>
#include <Shapes/Sphere.h>
#include <Shapes/Quad.h>
#include <Shapes/Terrain.h>
#include <Shapes/Diamond.h>
#include <Shapes/GridHelper.h>
#include <Core/RawModel.h>
#include <Animation/AnimatedModel.h>
#include <Sprite/SpriteGeometry.h>
#include "Shapes/AxisHelper.h"
#include "Shapes/PointLightHelper.h"
#include "Shapes/AABBBoundingBox.h"
#include "Shapes/KDTreeVisualizer.h"

namespace GEngine
{
	namespace Manager
	{
		static constexpr RuntimeAssets::Directory model_base_dir{ "Models/" };
		static std::string model_extension = ".obj";

		static constexpr RuntimeAssets::Directory animated_model_base_dir{ "AnimatedModels/" };
		static std::string animated_model_extension = ".dae";

		void ShapeManager::Initialize()
		{
			using namespace Shape;

			RegisterShape(Quad);
			RegisterShape(Box);
			RegisterShape(SkyBox);
			RegisterShape(Circle);
			RegisterShape(Cone);
			RegisterShape(Ellipsoid);
			RegisterShape(Hexagon);
			RegisterShape(Icosahedron);
			RegisterShape(Plane);
			RegisterShape(Prism);
			RegisterShape(Pyramid);
			RegisterShape(Ring);
			RegisterShape(SmoothSphere);
			RegisterShape(Torus);
			RegisterShape(Cylinder);
			RegisterShape(Sphere);
			RegisterShape(Terrain);
			RegisterShape(SpriteGeometry);
			RegisterShape(GridHelper);
			RegisterShape(AxisHelper);
			RegisterShape(Diamond);
			RegisterShape(PointLightHelper);
			ShapeManager::__Register<Shape::AABBBoundingBox>("AABBBoundingBox");
			RegisterShape(KDTreeVisualizer);
			//_RegisterShape(SmoothSphere, EnvironmentSphere, 1000.f);
			//_RegisterShape(SmoothSphere, FloorSphere, 80.f);
			//_RegisterShape(Sphere, FloorBaseSphere, 80.f);
			//_RegisterShape(Sphere, PointLight, 0.5f, 32, 32);
		}

        ShapeManager& ShapeManager::Current() { return EngineContext::Current().Shapes(); }
        ShapeManager::~ShapeManager() = default;

        std::expected<MeshAsset, SceneResourceError> ShapeManager::ExportMesh(std::string_view name) const
        {
            const auto found = m_Shapes.find(std::string(name));
            if (found == m_Shapes.end())
                return std::unexpected(SceneResourceError{std::string(name), SceneResourceCode::MissingShape});
            auto mesh = found->second->ExportCpuMesh();
            if (!mesh) return std::unexpected(SceneResourceError{std::string(name), mesh.error()});
            return std::move(*mesh);
        }

        std::expected<void, SceneResourceError> ShapeManager::AttachPhysicsShape(_Entity& entity, std::string_view name) const
        {
            if (!entity) return std::unexpected(SceneResourceError{"physics authoring", SceneResourceCode::InvalidEntity});
            const auto found = m_Shapes.find(std::string(name));
            if (found == m_Shapes.end())
                return std::unexpected(SceneResourceError{std::string(name), SceneResourceCode::MissingShape});
            entity.AddOrReplaceComponent<Component::MeshComponent>(found->second.get());
            return {};
        }

        bool ShapeManager::Owns(const Geometry* shape) const
        {
            for (const auto& [name, owner] : m_Shapes) if (owner.get() == shape) return true;
            for (const auto& owner : m_Retired) if (owner.get() == shape) return true;
            for (const auto& [name, model] : m_Models)
                for (const auto& owner : model.owners) if (owner.get() == shape) return true;
            return false;
        }

        void ShapeManager::Register(const std::string& name, Geometry* shape)
        {
            auto& self = Current();
            if (!shape) throw std::invalid_argument("Cannot register a null shape");
            if (self.Owns(shape))
            {
                auto it = self.m_Shapes.find(name);
                if (it != self.m_Shapes.end() && it->second.get() == shape) return;
                throw std::invalid_argument("Geometry already belongs to this manager");
            }
            std::unique_ptr<Geometry> candidate(shape);
            // try_emplace leaves candidate owned locally on duplicate or failure.
            self.m_Shapes.try_emplace(name, std::move(candidate));
        }

        void ShapeManager::UnRegister(const std::string& name)
        {
            auto& self = Current();
            if (auto it = self.m_Shapes.find(name); it != self.m_Shapes.end())
            {
                self.m_Retired.push_back(std::move(it->second));
                self.m_Shapes.erase(it);
            }
        }

        Geometry* ShapeManager::GetShape(const std::string& name)
        {
            auto& shapes = Current().m_Shapes;
            if (auto it = shapes.find(name); it != shapes.end()) return it->second.get();
            return nullptr;
        }

        Geometry* ShapeManager::GetModel(const std::string& name)
        {
            auto& shapes = Current().m_Shapes;
            if (auto it = shapes.find(name); it != shapes.end()) return it->second.get();
            const auto path = model_base_dir + name + model_extension;
            if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Model not found: " + path);
            RawModel model(path);
            auto shape = std::unique_ptr<Geometry>(model.GetGeometry(0));
            if (!shape) throw std::runtime_error("Model contains no geometry: " + path);
            auto* result = shape.get();
            shapes.emplace(name, std::move(shape));
            return result;
        }

        const std::vector<Geometry*>& ShapeManager::GetModels(const std::string& name)
        {
            auto& models = Current().m_Models;
            if (auto it = models.find(name); it != models.end()) return it->second.borrowers;
            const auto path = animated_model_base_dir + name + animated_model_extension;
            if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Model not found: " + path);
            AnimatedModel model(path);
            auto& raw = model.GetGeometries();
            // The legacy loader transfers its raw results. Adopt all of them even
            // if allocation of the manager entry fails after the loader returns.
            struct Pending
            {
                std::vector<Geometry*>& raw;
                ~Pending() { for (auto* geometry : raw) delete geometry; }
            } pending{raw};
            ModelEntry entry;
            entry.borrowers = raw;
            entry.owners.reserve(raw.size());
            for (auto*& geometry : raw)
            {
                entry.owners.emplace_back(geometry);
                geometry = nullptr;
            }
            return models.emplace(name, std::move(entry)).first->second.borrowers;
        }
    }
}
