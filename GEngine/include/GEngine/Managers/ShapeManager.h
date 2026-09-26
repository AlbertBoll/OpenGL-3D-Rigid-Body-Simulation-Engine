#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <concepts>
#include <new>
#include <expected>
#include <variant>
#include <string_view>
#include <vector>
#include "Core/Utility.h"
#include "Assets/ModelImportError.h"
#include "Renderer/SceneRenderResources.h"

#define RegisterShape(class_name) Manager::ShapeManager::__Register<class_name>(#class_name);
#define _RegisterShape(class_name, key_name, ...) Manager::ShapeManager::__Register<class_name>(#key_name, __VA_ARGS__);
#define UnRegisterShape(class_name) (Manager::ShapeManager::UnRegister(#class_name))

namespace GEngine
{
    enum class ShapeRegistrationErrorCode { NullGeometry, AlreadyOwned, Allocation };
    struct ShapeRegistrationError
    {
        ShapeRegistrationErrorCode code;
        std::string_view operation;
        std::string name;
        std::string message;
    };
    class EngineContext;
    class Geometry;
    namespace Manager
    {
        class ShapeManager
        {
        public:
            ~ShapeManager();
            NONCOPYMOVABLE(ShapeManager);

            // Transfers a newly allocated shape on the owning thread. A duplicate
            // key keeps the existing borrower valid and destroys the new candidate.
            // Re-registering an already owned pointer never transfers it twice.
            using RegistrationError = std::variant<PlatformError, ShapeRegistrationError>;
            using RegistrationResult = std::expected<void, RegistrationError>;
            using ShapeResult = std::expected<Geometry*, RegistrationError>;
            // On typed failure ownership stays with the caller (or its existing manager).
            [[nodiscard]] static RegistrationResult Register(const std::string& shape_name, Geometry* new_shape);
            // Removes lookup only; old borrowers survive until root shutdown.
            [[nodiscard]] static PlatformResult UnRegister(const std::string& shape_name);
            [[nodiscard]] static std::expected<Geometry*, PlatformError> GetShape(const std::string& shape_name);
            // A successful missing lookup returns nullptr; service failures stay typed.
            using LookupResult = std::expected<Geometry*, PlatformError>;
            [[nodiscard]] static LookupResult FindShape(const std::string& shape_name);
            // Removes lookup only; all existing borrowers remain alive until root teardown.
            [[nodiscard]] static PlatformResult RetireShape(const std::string& shape_name);
            [[nodiscard]] std::expected<MeshAsset, SceneResourceError> ExportMesh(std::string_view name) const;
            [[nodiscard]] std::expected<void, SceneResourceError> AttachPhysicsShape(_Entity&, std::string_view name) const;
            using ModelError = std::variant<ModelImportError, PlatformError>;
            using ModelResult = std::expected<Geometry*, ModelError>;
            [[nodiscard]] static ModelResult GetModel(const std::string& modelName);
            using ModelsResult = std::expected<std::reference_wrapper<const std::vector<Geometry*>>, ModelError>;
            [[nodiscard]] static ModelsResult GetModels(const std::string& modelName);

            template<typename T, typename... Args>
                requires std::derived_from<T, Geometry> && std::constructible_from<T, Args...>
            [[nodiscard]] static ShapeResult _GetShape(const std::string& shape_name, Args&&... args)
            {
                auto manager = TryCurrent();
                if (!manager) return std::unexpected(manager.error());
                auto& self = **manager;
                if (auto found = self.m_Shapes.find(shape_name); found != self.m_Shapes.end())
                    return found->second.get();
                std::unique_ptr<T> shape(new (std::nothrow) T(std::forward<Args>(args)...));
                if (!shape) return std::unexpected(ShapeRegistrationError{ShapeRegistrationErrorCode::Allocation,
                    "ShapeManager::_GetShape", shape_name, "Shape owner allocation failed"});
                return self.Store(shape_name, std::move(shape));
            }

            template<typename T, typename... Args>
                requires std::derived_from<T, Geometry> && std::constructible_from<T, Args...>
            [[nodiscard]] static RegistrationResult __Register(const std::string& shape_name, Args&&... args)
            {
                auto shape = _GetShape<T>(shape_name, std::forward<Args>(args)...);
                if (!shape) return std::unexpected(shape.error());
                return {};
            }

        private:
            friend class ::GEngine::EngineContext;
            ShapeManager();
            RegistrationResult Initialize();
            static std::expected<ShapeManager*, PlatformError> TryCurrent();
            Geometry* Store(const std::string& name, std::unique_ptr<Geometry> shape);
            static std::expected<ShapeManager*, PlatformError> Current();
            bool Owns(const Geometry* shape) const;
            struct ModelEntry
            {
                std::vector<std::unique_ptr<Geometry>> owners;
                std::vector<Geometry*> borrowers;
            };
            std::unordered_map<std::string, std::unique_ptr<Geometry>> m_Shapes;
            std::unordered_map<std::string, ModelEntry> m_Models;
            std::vector<std::unique_ptr<Geometry>> m_Retired;
        };
    }
}
