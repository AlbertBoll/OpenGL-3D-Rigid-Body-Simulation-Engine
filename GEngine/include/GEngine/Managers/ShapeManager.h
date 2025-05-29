#pragma once
#include <string>
#include <unordered_map>
#include <Geometry/Geometry.h>

#define RegisterShape(class_name) Manager::ShapeManager::Register(#class_name, new class_name);
#define _RegisterShape(class_name, key_name, ...) Manager::ShapeManager::__Register<class_name>(#key_name, __VA_ARGS__);
#define UnRegisterShape(class_name) Manager::ShapeManager::UnRegister(#class_name);

namespace GEngine
{
	namespace Manager
	{
		class ShapeManager
		{
		public:

			static void Initialize();
			static void Register(const std::string& shape_name, Geometry* new_shape);


			template<typename T, typename...Args>
			static void __Register(const std::string& shape_name, Args&&...args);

			static void UnRegister(const std::string& shape_name);


			static Geometry* GetShape(const std::string& shape_name);


			static Geometry* GetModel(const std::string& modelName);

			static std::vector<Geometry*>& GetModels(const std::string& modelName);

			static void FreeShape();

			template<typename T, typename...Args>
			static Geometry* _GetShape(const std::string& shape_name, Args&&...args);

			/*template<typename T, typename...Args>
			static void __Register(const std::string& shape_name, Args&&...args);
			*/

		private:
			inline static std::unordered_map<std::string, Geometry*> m_Shapes;
			inline static std::unordered_map<std::string, std::vector<Geometry*>> _m_Shapes;
		};



		template<typename T, typename ...Args>
		inline Geometry* ShapeManager::_GetShape(const std::string& shape_name, Args && ...args)
		{
			if (auto it = m_Shapes.find(shape_name); it != m_Shapes.end())
				//delete it->second;
				return it->second;

			auto new_shape = new T(std::forward<Args>(args)...);
			m_Shapes.emplace(shape_name, new_shape);
			return new_shape;

		}

		template<typename T, typename ...Args>
		inline void ShapeManager::__Register(const std::string& shape_name, Args&& ...args)
		{
			if (auto it = m_Shapes.find(shape_name); it == m_Shapes.end())
			{
				auto new_shape = new T(std::forward<Args>(args)...);
				m_Shapes.emplace(shape_name, new_shape);
			}
		}

	}
}