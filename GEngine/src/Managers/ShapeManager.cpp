#include "gepch.h"
#include "Core/RuntimeAssets.h"
#include "Managers/ShapeManager.h"
#include "Geometry/Geometry.h"
#include "Core/GEngine.h"
#include "Scene/_Entity.h"
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

		ShapeManager::RegistrationResult ShapeManager::Initialize()
		{
			using namespace Shape;

            if (auto result = __Register<Quad>("Quad"); !result) return result;
            if (auto result = __Register<Box>("Box"); !result) return result;
            if (auto result = __Register<SkyBox>("SkyBox"); !result) return result;
            if (auto result = __Register<Circle>("Circle"); !result) return result;
            if (auto result = __Register<Cone>("Cone"); !result) return result;
            if (auto result = __Register<Ellipsoid>("Ellipsoid"); !result) return result;
            if (auto result = __Register<Hexagon>("Hexagon"); !result) return result;
            if (auto result = __Register<Icosahedron>("Icosahedron"); !result) return result;
            if (auto result = __Register<Plane>("Plane"); !result) return result;
            if (auto result = __Register<Prism>("Prism"); !result) return result;
            if (auto result = __Register<Pyramid>("Pyramid"); !result) return result;
            if (auto result = __Register<Ring>("Ring"); !result) return result;
            if (auto result = __Register<SmoothSphere>("SmoothSphere"); !result) return result;
            if (auto result = __Register<Torus>("Torus"); !result) return result;
            if (auto result = __Register<Cylinder>("Cylinder"); !result) return result;
            if (auto result = __Register<Sphere>("Sphere"); !result) return result;
            if (auto result = __Register<Terrain>("Terrain"); !result) return result;
            if (auto result = __Register<SpriteGeometry>("SpriteGeometry"); !result) return result;
            if (auto result = __Register<GridHelper>("GridHelper"); !result) return result;
            if (auto result = __Register<AxisHelper>("AxisHelper"); !result) return result;
            if (auto result = __Register<Diamond>("Diamond"); !result) return result;
            if (auto result = __Register<PointLightHelper>("PointLightHelper"); !result) return result;
            if (auto result = __Register<Shape::AABBBoundingBox>("AABBBoundingBox"); !result) return result;
            if (auto result = __Register<KDTreeVisualizer>("KDTreeVisualizer"); !result) return result;
            return {};
			//_RegisterShape(SmoothSphere, EnvironmentSphere, 1000.f);
			//_RegisterShape(SmoothSphere, FloorSphere, 80.f);
			//_RegisterShape(Sphere, FloorBaseSphere, 80.f);
			//_RegisterShape(Sphere, PointLight, 0.5f, 32, 32);
		}

        std::expected<ShapeManager*, PlatformError> ShapeManager::TryCurrent()
        {
            auto* root = EngineContext::TryGet();
            if (!root) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "shape registration", "No live application EngineContext"});
            return root->TryShapes();
        }

        std::expected<ShapeManager*, PlatformError> ShapeManager::Current() { return TryCurrent(); }
        ShapeManager::ShapeManager() = default;
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

        Geometry* ShapeManager::Store(const std::string& name, std::unique_ptr<Geometry> candidate)
        {
            // A duplicate keeps its old borrower; the new candidate retires here.
            return m_Shapes.try_emplace(name, std::move(candidate)).first->second.get();
        }

        ShapeManager::RegistrationResult ShapeManager::Register(const std::string& name, Geometry* shape)
        {
            auto manager = TryCurrent();
            if (!manager) return std::unexpected(manager.error());
            auto& self = **manager;
            if (!shape) return std::unexpected(ShapeRegistrationError{ShapeRegistrationErrorCode::NullGeometry,
                "ShapeManager::Register", name, "Cannot register a null shape"});
            if (self.Owns(shape))
            {
                auto it = self.m_Shapes.find(name);
                if (it != self.m_Shapes.end() && it->second.get() == shape) return {};
                return std::unexpected(ShapeRegistrationError{ShapeRegistrationErrorCode::AlreadyOwned,
                    "ShapeManager::Register", name, "Geometry already belongs to this manager"});
            }
            self.Store(name, std::unique_ptr<Geometry>(shape));
            return {};
        }

        ShapeManager::LookupResult ShapeManager::FindShape(const std::string& name)
        {
            auto manager = TryCurrent();
            if (!manager) {
                auto error = std::move(manager.error());
                error.operation = "ShapeManager::FindShape / " + error.operation;
                return std::unexpected(std::move(error));
            }
            auto& shapes = (*manager)->m_Shapes;
            if (auto it = shapes.find(name); it != shapes.end()) return it->second.get();
            return nullptr;
        }

        PlatformResult ShapeManager::RetireShape(const std::string& name)
        {
            auto manager = TryCurrent();
            if (!manager) {
                auto error = std::move(manager.error());
                error.operation = "ShapeManager::RetireShape / " + error.operation;
                return std::unexpected(std::move(error));
            }
            auto& self = **manager;
            if (auto it = self.m_Shapes.find(name); it != self.m_Shapes.end()) {
                self.m_Retired.push_back(std::move(it->second));
                self.m_Shapes.erase(it);
            }
            return {};
        }

        PlatformResult ShapeManager::UnRegister(const std::string& name)
        {
            return RetireShape(name);
        }

        ShapeManager::LookupResult ShapeManager::GetShape(const std::string& name)
        {
            return FindShape(name);
        }

        ShapeManager::ModelResult ShapeManager::GetModel(const std::string& name)
        {
            auto* root = EngineContext::TryGet();
            if (!root) return std::unexpected(ModelError{PlatformError{PlatformErrorCode::InvalidState,
                "ShapeManager::GetModel", "No live application EngineContext"}});
            auto services = root->SceneServices();
            if (!services) return std::unexpected(ModelError{services.error()});
            auto& shapes = services->shapes.m_Shapes;
            if (auto it = shapes.find(name); it != shapes.end()) return it->second.get();
            auto path = RuntimeAssets::TryFile("Models/" + name + model_extension);
            if (!path) return std::unexpected(ModelError{path.error()});
            std::error_code error;
            if (!std::filesystem::is_regular_file(*path, error) || error)
                return std::unexpected(ModelError{ModelImportError{ModelImportErrorCode::FileRead,
                    "ShapeManager::GetModel", *path, "Model not found: " + *path
                        + (error ? "; " + error.message() : "")}});
            auto model = RawModel::Create(*path);
            if (!model) return std::unexpected(ModelError{model.error()});
            auto geometries = std::move(*model).TakeGeometries();
            // The cache continues to own the first mesh; remaining results retire locally.
            auto shape = std::move(geometries.front());
            auto* result = shape.get();
            shapes.emplace(name, std::move(shape));
            return result;
        }

        ShapeManager::ModelsResult ShapeManager::GetModels(const std::string& name)
        {
            auto* root = EngineContext::TryGet();
            if (!root) return std::unexpected(ModelError{PlatformError{PlatformErrorCode::InvalidState,
                "ShapeManager::GetModels", "No live application EngineContext"}});
            auto services = root->SceneServices();
            if (!services) return std::unexpected(ModelError{services.error()});
            auto& models = services->shapes.m_Models;
            if (auto it = models.find(name); it != models.end()) return std::cref(it->second.borrowers);
            auto path = RuntimeAssets::TryFile("AnimatedModels/" + name + animated_model_extension);
            if (!path) return std::unexpected(ModelError{path.error()});
            std::error_code error;
            if (!std::filesystem::is_regular_file(*path, error) || error)
                return std::unexpected(ModelError{ModelImportError{ModelImportErrorCode::FileRead,
                    "ShapeManager::GetModels", *path, "Model not found: " + *path
                        + (error ? "; " + error.message() : "")}});
            auto model = AnimatedModel::Create(*path);
            if (!model) return std::unexpected(ModelError{model.error()});
            ModelEntry entry;
            entry.owners = std::move(*model).TakeGeometries();
            entry.borrowers.reserve(entry.owners.size());
            for (const auto& geometry : entry.owners) entry.borrowers.push_back(geometry.get());
            return std::cref(models.emplace(name, std::move(entry)).first->second.borrowers);
        }
    }
}
