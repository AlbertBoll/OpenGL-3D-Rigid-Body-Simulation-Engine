#pragma once
#include <cstdint>
#include "Core/Utility.h"
#include <Core/Image.h>
#include <Math/Math.h>
#include <optional>
#include <vector>

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
			// Pixel/sample streams are independent of task order and worker count.
			uint64_t Seed = 0;
		};

	public:
		SimpleRenderer() = default;
		~SimpleRenderer() = default;
		NONCOPYABLE(SimpleRenderer);
		SimpleRenderer(SimpleRenderer&&) noexcept = default;
		SimpleRenderer& operator=(SimpleRenderer&&) noexcept = default;

		// Render, resize, settings changes and destruction belong to the context
		// thread. Render joins all CPU tasks before uploading or returning; callers
		// must keep scene/camera inputs unchanged until it returns.
		[[nodiscard]] ImageResult OnResize(uint32_t width, uint32_t height);
		void RenderBegin();
		[[nodiscard]] ImageResult Render(const RayTracingScene& scene, const RayTracingCamera& camera);
		auto& GetFinalImage()const { return m_FinalImage; }
		void SetSphereColor(const Vec3f& color) { m_SphereColor = color; }
		Vec3f& GetSphereColor() { return m_SphereColor; }

		void ResetFrameIndex() { m_FrameIndex = 1; }

		Settings& GetSettings() { return m_Settings; }
		int& GetNumOfThread() { return m_NumberOfThreads; }
		int& GetBounces() { return m_Bounces; }

	private:

		struct HitInfo
		{
			std::optional<float> HitDistance = std::nullopt;
			Vec3f WorldPosition;
			Vec3f WorldNormal;

			int ObjectIndex;
		};

		static Vec4f PerPixel(const RayTracingScene& scene, const RayTracingCamera& camera,
			size_t pixel, uint64_t sample, uint64_t seed, int bounces);
		static HitInfo TraceRay(const RayTracingScene& scene, const Ray& ray);
		static HitInfo ClosestHit(const RayTracingScene& scene, const Ray& ray, float hitDistance, int objectIndex);


	private:
		RefPtr<Image> m_FinalImage;
		std::vector<uint8_t> m_ImageData;
		std::vector<Vec4f> m_AccumulationData;
		bool m_NeedsAllocation = false;
		Vec3f m_SphereColor;
		uint64_t m_FrameIndex = 1;
		Settings m_Settings;
		int m_NumberOfThreads = 12;
		int m_Bounces = 5;
	
	};

}

