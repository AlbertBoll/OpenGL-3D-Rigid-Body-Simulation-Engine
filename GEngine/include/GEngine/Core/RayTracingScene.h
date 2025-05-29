#pragma once
#include <Math/Math.h>
#include <vector>



namespace GEngine
{


	using namespace Math;

	struct MaterialInfo
	{
		Vec3f Albedo{ 1.0f };
		float Roughness = 1.0f;
		float Metallic = 0.0f;
	};

	struct Sphere
	{
		Vec3f Position{ 0.f };
		float Radius = 0.5f;

		//MaterialInfo Mat;
		int MaterialIndex = 0;
		//Vec3f Albedo{ 1.0f };
	};

	struct RayTracingScene
	{
		std::vector<Sphere> Spheres;
		std::vector<MaterialInfo> Materials;
	};
}