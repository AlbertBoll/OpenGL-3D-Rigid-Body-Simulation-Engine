#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <Geometry/Geometry.h>

#define RegisterShape(class_name) Manager::ShapeManager::__Register<class_name>(#class_name);
#define _RegisterShape(class_name, key_name, ...) Manager::ShapeManager::__Register<class_name>(#key_name, __VA_ARGS__);
#define UnRegisterShape(class_name) Manager::ShapeManager::UnRegister(#class_name);

namespace GEngine
{
    class EngineContext;
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
            static void Register(const std::string& shape_name, Geometry* new_shape);
            // Removes lookup only; old borrowers survive until root shutdown.
            static void UnRegister(const std::string& shape_name);
            static Geometry* GetShape(const std::string& shape_name);
            static Geometry* GetModel(const std::string& modelName);
            static const std::vector<Geometry*>& GetModels(const std::string& modelName);

            template<typename T, typename... Args>
            static Geometry* _GetShape(const std::string& shape_name, Args&&... args)
            {
                if (auto* existing = GetShape(shape_name)) return existing;
                auto shape = std::make_unique<T>(std::forward<Args>(args)...);
                auto* result = shape.get();
                Register(shape_name, shape.release());
                return result;
            }

            template<typename T, typename... Args>
            static void __Register(const std::string& shape_name, Args&&... args)
            {
                (void)_GetShape<T>(shape_name, std::forward<Args>(args)...);
            }

        private:
            friend class ::GEngine::EngineContext;
            ShapeManager() = default;
            void Initialize();
            static ShapeManager& Current();
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
