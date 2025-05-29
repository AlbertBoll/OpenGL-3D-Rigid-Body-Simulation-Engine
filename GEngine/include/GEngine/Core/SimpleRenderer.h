#pragma once
#include <cstdint>
#include "Core/Utility.h"
#include <Core/Image.h>
#include <Math/Math.h>
#include <optional>
#include <thread>

namespace GEngine
{
	class RayTracingCamera;
	struct RayTracingScene;
	struct Ray;
	

	using namespace Math;
	class SimpleRenderer
	{

	public:
		struct Settings
		{
			bool Acculmate = false;
		};

	public:
		SimpleRenderer() = default;
		~SimpleRenderer() { if (m_ImageData) delete[] m_ImageData; }

		void OnResize(uint32_t width, uint32_t height);
		void RenderBegin();
		void Render(const RayTracingScene& scene, const RayTracingCamera& camera);
		auto& GetFinalImage()const { return m_FinalImage; }
		void SetSphereColor(const Vec3f& color) { m_SphereColor = color; }
		Vec3f& GetSphereColor() { return m_SphereColor; }

		void ResetFrameIndex() { m_FrameIndex = 1; }

		Settings& GetSettings() { return m_Settings; }
		int& GetNumOfThread() { return m_NumberOfThreads; }
		int& GetBounces() { return m_Bounces; }

		//void SetNumOfWorker(int workers);

	private:

		struct HitInfo
		{
			std::optional<float> HitDistance = std::nullopt;
			Vec3f WorldPosition;
			Vec3f WorldNormal;

			int ObjectIndex;
		};

		Vec4f PerPixel(uint32_t x, uint32_t y);
		//Vec4f TraceRay(const RayTracingScene& scene, const Ray& ray);
		HitInfo TraceRay(const Ray& ray);
		HitInfo ClosestHit(const Ray& ray, float hitDistance, int objectIndex);
		HitInfo Miss(const Ray& ray);
		
		void ProcessDataSet(uint32_t start, uint32_t end, uint32_t width);


	private:
		RefPtr<Image> m_FinalImage;
		uint8_t* m_ImageData{};
		Vec4f* m_AccumulationData{};
		const RayTracingScene* m_ActiveScene{};
		const RayTracingCamera* m_ActiveCamera{};
		bool b_IsReAlloc = false;
		Vec3f m_SphereColor;
		uint32_t m_FrameIndex = 1;
		Settings m_Settings;
		std::vector<std::thread> m_Workers;
		std::vector<uint32_t> m_ImageHorizontalIter;
		std::vector<uint32_t> m_ImageVerticalIter;
		int m_NumberOfThreads = 12;
		bool m_IsFirstEnter = true;
		int m_Bounces = 5;
		int m_SamplesPerPixel = 12;
	
	};

}

