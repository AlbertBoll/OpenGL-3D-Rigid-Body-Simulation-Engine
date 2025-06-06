#include "gepch.h"
#include "Managers/ShapeManager.h"
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

namespace GEngine
{
	namespace Manager
	{
		static std::string model_base_dir = "../GEngine/include/GEngine/Assets/Models/";
		static std::string model_extension = ".obj";

		static std::string animated_model_base_dir = "../GEngine/include/GEngine/Assets/AnimatedModels/";
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
			_RegisterShape(SmoothSphere, EnvironmentSphere, 1000.f);
			_RegisterShape(SmoothSphere, FloorSphere, 80.f);
			_RegisterShape(Box, RectangularPlane, 100.f, 1.f, 100.f);
			_RegisterShape(Sphere, FloorBaseSphere, 80.f);
			_RegisterShape(Sphere, PointLight, 0.5f, 32, 32);
		}

		void ShapeManager::Register(const std::string& shape_name, Geometry* new_shape)
		{
			if (auto it = m_Shapes.find(shape_name); it == m_Shapes.end())
			{
				m_Shapes.emplace(shape_name, new_shape);
			}
		}

		void ShapeManager::UnRegister(const std::string& shape_name)
		{
			if (auto it = m_Shapes.find(shape_name); it != m_Shapes.end())
			{
				delete it->second;
				m_Shapes.erase(it->first);
			}
		}

		Geometry* ShapeManager::GetShape(const std::string& shape_name)
		{
			if (auto it = m_Shapes.find(shape_name); it != m_Shapes.end())
				return it->second;

			return nullptr;
		}

		Geometry* ShapeManager::GetModel(const std::string& modelName)
		{
			if (auto it = m_Shapes.find(modelName); it != m_Shapes.end())
				return it->second;


			RawModel model(model_base_dir + modelName + model_extension);

			//auto geo = model.GetGeometry(0);
			//geo->ApplyTransform(Matrix::MakeRotationY(Math::PiOver2), 0, false);
			m_Shapes.emplace(modelName, model.GetGeometry(0));
			return m_Shapes[modelName];

		}

		std::vector<Geometry*>& ShapeManager::GetModels(const std::string& modelName)
		{
			if (auto it = _m_Shapes.find(modelName); it != _m_Shapes.end())
				return it->second;

			AnimatedModel model(animated_model_base_dir + modelName + animated_model_extension);

			//auto geo = model.GetGeometry(0);
			//geo->ApplyTransform(Matrix::MakeRotationY(Math::PiOver2), 0, false);
			_m_Shapes.emplace(modelName, model.GetGeometries());
			return _m_Shapes[modelName];
		}

		void ShapeManager::FreeShape()
		{
			for (auto& [name, instance] : m_Shapes)
			{
				if (instance)
				{
					delete instance;
					instance = nullptr;
				}
			}

			for (auto& [name, instances] : _m_Shapes)
			{
				for (auto& instance : instances)
				{
					if (instance)
					{
						delete instance;
						instance = nullptr;
					}

				}
			}

			m_Shapes.clear();
			_m_Shapes.clear();
		}
	}
	
}
