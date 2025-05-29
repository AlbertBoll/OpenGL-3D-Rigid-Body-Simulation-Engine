#include <gepch.h>
#include "Core/SimpleRenderer.h"
#include "Core/RandomGenerator.h"
#include "Camera/RayTracingCamera.h"
#include "Core/Ray.h"
#include "Core/RayTracingScene.h"
#include <numeric>
#include <execution>
#include "tbb/tbb/blocked_range2d.h"
#include "tbb/tbb/parallel_for.h"

namespace GEngine
{

	namespace Utils
	{
		static void FillRGBAToPixel(uint8_t* data, uint32_t index, const Vec4f& pixelColor)
		{
			data[4 * index + 0] = (uint8_t)(pixelColor.r * 255.0f);
			data[4 * index + 1] = (uint8_t)(pixelColor.g * 255.0f);
			data[4 * index + 2] = (uint8_t)(pixelColor.b * 255.0f);
			data[4 * index + 3] = (uint8_t)(pixelColor.a * 255.0f);
		}

		static void MultiSampled(uint8_t* data, uint32_t width, uint32_t height, int samples_per_pixel)
		{
			uint8_t pixel_color = 0;
			tbb::parallel_for(tbb::blocked_range2d<size_t>(0, height, 0, width),
				[&](tbb::blocked_range2d<size_t> r)
				{
					for (size_t y = r.rows().begin(); y < r.rows().end(); y++)
					{
						for (size_t x = r.cols().begin(); x < r.cols().end(); x++)
						{
							if (x != 0 && x != width - 1 && y != 0 && y != height - 1)
							{
								auto center_index = x + y * width;
								auto left_index = (x - 1) + y * width;
								auto right_index = (x + 1) + y * width;
								auto up_index = x + (y - 1) * width;
								auto down_index = x + (y + 1) * width;
								Vec4f left_pixel =  { data[4 * left_index + 0], data[4 * left_index + 1], data[4 * left_index + 2], data[4 * left_index + 3]};
								Vec4f right_pixel = { data[4 * right_index + 0], data[4 * right_index + 1], data[4 * right_index + 2], data[4 * right_index + 3] };
								Vec4f up_pixel = { data[4 * up_index + 0], data[4 * up_index + 1], data[4 * up_index + 2], data[4 * up_index + 3] };
								Vec4f down_pixel = { data[4 * down_index + 0], data[4 * down_index + 1], data[4 * down_index + 2], data[4 * down_index + 3] };
								Vec4f center_pixel = { data[4 * center_index + 0], data[4 * center_index + 1], data[4 * center_index + 2], data[4 * center_index + 3] };
								Vec4f average_pixel = (left_pixel + right_pixel + up_pixel + down_pixel + center_pixel) / 5.f;
								average_pixel = glm::clamp(average_pixel, Vec4f(0.f), Vec4f(1.0f));
								data[4 * center_index + 0] = (uint8_t)(average_pixel.r * 255.0f);
								data[4 * center_index + 1] = (uint8_t)(average_pixel.g * 255.0f);
								data[4 * center_index + 2] = (uint8_t)(average_pixel.b * 255.0f);
								data[4 * center_index + 3] = (uint8_t)(average_pixel.a * 255.0f);

							}
						}

					}

				}
			);
		}
		
	}

	void SimpleRenderer::OnResize(uint32_t width, uint32_t height)
	{
		if (m_FinalImage)
		{
			if (m_FinalImage->GetWidth() == width && m_FinalImage->GetHeight() == height)
			{
				b_IsReAlloc = false;
				return;
			}

			m_FinalImage->Resize(width, height);
			
		}

		else
		{
			m_FinalImage = CreateRefPtr<Image>(width, height, ImageFormat::RGBA);
		}

		delete[] m_ImageData;
		m_ImageData = new uint8_t[width * height * 4];

		delete[] m_AccumulationData;
		m_AccumulationData = new Vec4f[width * height];

		b_IsReAlloc = true;

		/*m_ImageHorizontalIter.resize(width);
		m_ImageVerticalIter.resize(height);
		std::iota(m_ImageHorizontalIter.begin(), m_ImageHorizontalIter.end(), 0);
		std::iota(m_ImageVerticalIter.begin(), m_ImageVerticalIter.end(), 0);*/
	}

	void SimpleRenderer::RenderBegin()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, m_FinalImage->GetWidth(), m_FinalImage->GetHeight());
	}


	void SimpleRenderer::Render(const RayTracingScene& scene, const RayTracingCamera& camera)
	{
		m_ActiveScene = &scene;
		m_ActiveCamera = &camera;

		auto width = m_FinalImage->GetWidth();
		auto height = m_FinalImage->GetHeight();

		if (m_FrameIndex == 1)
		{
			memset(m_AccumulationData, 0, width * height * sizeof(Vec4f));
		}

		Vec4f pixelColor{ 0.f };

//#define STD_FOREACH 1

#if RawMultiThread

		int divide_height = height / m_NumberOfThreads;
		uint32_t start{};
		uint32_t end{};
		for (auto height_division = 0; height_division < m_NumberOfThreads; height_division++)
		{
			start = height_division * divide_height;
			end = (height_division + 1) * divide_height;
			if(m_IsFirstEnter)
				m_Workers.push_back(std::thread(&SimpleRenderer::ProcessDataSet, this, start, end, width));
		}

		ProcessDataSet(end, height, width);

		for (auto& worker : m_Workers)
		{
			worker.join();
		}
	
		m_Workers.clear();

#endif	

	tbb::parallel_for(tbb::blocked_range2d<size_t>(0, height, 0, width),
		[&](tbb::blocked_range2d<size_t> r)
		{
			for (size_t y = r.rows().begin(); y < r.rows().end(); y++)
			{
				for (size_t x = r.cols().begin(); x < r.cols().end(); x++)
				{
						
					pixelColor = PerPixel(x, y);
					m_AccumulationData[x + y * width] += pixelColor;
					Vec4f accumulatedColor = m_AccumulationData[x + y * width] / (float)m_FrameIndex;
					accumulatedColor = glm::clamp(accumulatedColor, Vec4f(0.f), Vec4f(1.0f));
					auto index = x + y * width;
					Utils::FillRGBAToPixel(m_ImageData, index, accumulatedColor);
				}
			}
		}
	);

	




	//std::for_each(std::execution::par, m_ImageVerticalIter.begin(), m_ImageVerticalIter.end(),
	//	[this, width](uint32_t y)
	//	{
	//		//std::for_each(std::execution::par, m_ImageHorizontalIter.begin(), m_ImageHorizontalIter.end(),
	//			//[this, y](uint32_t x)
	//		for (uint32_t x = 0; x < width; x++)
	//		{

	//			auto index = x + y * m_FinalImage->GetWidth();
	//			Vec4f pixelColor = PerPixel(x, y);
	//			m_AccumulationData[x + y * m_FinalImage->GetWidth()] += pixelColor;

	//			Vec4f accumulatedColor = m_AccumulationData[x + y * m_FinalImage->GetWidth()] / (float)m_FrameIndex;

	//			accumulatedColor = glm::clamp(accumulatedColor, Vec4f(0.f), Vec4f(1.0f));
	//			Utils::FillRGBAToPixel(m_ImageData, index, accumulatedColor);


	//		};
	//	}
	//);

//#endif

#if Original
		for (uint32_t y = 0; y < height; y++)
		{
			for (uint32_t x = 0; x < width; x++)
			{

				auto index = x + y * width;
				Vec4f pixelColor = PerPixel(x, y);
				m_AccumulationData[x + y * width] += pixelColor;

				Vec4f accumulatedColor = m_AccumulationData[x + y * width] / (float)m_FrameIndex;

				accumulatedColor = glm::clamp(accumulatedColor, Vec4f(0.f), Vec4f(1.0f));
				Utils::FillRGBAToPixel(m_ImageData, index, accumulatedColor);

			}
		}

#endif



		if (b_IsReAlloc) m_FinalImage->ReAllocateData(m_ImageData);
		else m_FinalImage->UpdateData(m_ImageData);

		if (m_Settings.Acculmate) m_FrameIndex++;
		else ResetFrameIndex();

	}

	Vec4f SimpleRenderer::PerPixel(uint32_t x, uint32_t y)
	{
		Ray ray{ .Origin = m_ActiveCamera->GetPosition(), 
			     .Direction = m_ActiveCamera->GetRayDirections()[x + y * m_FinalImage->GetWidth()]};

		//int bounces = 5;
		Vec3f color(0.f);
		float multiplier = 1.f;

		for (int i = 0; i < m_Bounces; i++)
		{
			auto hit_info = TraceRay(ray);

			if (!hit_info.HitDistance)
			{
				Vec3f skyColor(0.6f, 0.7f, 0.9f);
				color += skyColor * multiplier;
				break;
			}

			const Sphere& sphere = m_ActiveScene->Spheres[hit_info.ObjectIndex];
			const MaterialInfo& material = m_ActiveScene->Materials[sphere.MaterialIndex];

			Vec3f lightDir = glm::normalize(Vec3f{ -1.f, -1.f, -1.f });

			float intensity = glm::max(glm::dot(hit_info.WorldNormal, -lightDir), 0.f);

			Vec3f sphereColor = material.Albedo;
			sphereColor *= intensity * multiplier;

			color += sphereColor;
			multiplier *= 0.5f;

			ray.Origin = hit_info.WorldPosition + hit_info.WorldNormal * 0.0001f;
			ray.Direction = glm::reflect(ray.Direction, hit_info.WorldNormal + material.Roughness * RandomGenerator::Vec3(-0.5f, 0.5f));
			
		}

		return Vec4f(color, 1.0f);

	}


	SimpleRenderer::HitInfo SimpleRenderer::TraceRay(const Ray& ray)
	{

		int closestSphere = -1;
		float hitDistance = std::numeric_limits<float>::max();

		auto size = m_ActiveScene->Spheres.size();

		for (size_t i = 0; i < size; ++i)
		{

			const Sphere& sphere = m_ActiveScene->Spheres[i];

			Vec3f origin = ray.Origin - sphere.Position;

			float a = glm::length2(ray.Direction);
			float b = 2.f * glm::dot(origin, ray.Direction);
			float c = glm::length2(origin) - sphere.Radius * sphere.Radius;



			float discriminant = b * b - 4.f * a * c;


			if (discriminant < 0.f) continue;

			float t_0 = (-b - glm::sqrt(discriminant)) / (2.f * a);

			//float t_1 = (-b + glm::sqrt(discriminant)) / (2.f * a);

			if (t_0 > 0.f && t_0 < hitDistance)
			{
				hitDistance = t_0;
				closestSphere = (int)i;
			}

		}

		if (closestSphere < 0) return Miss(ray);

		return ClosestHit(ray, hitDistance, closestSphere);


	}

	SimpleRenderer::HitInfo SimpleRenderer::ClosestHit(const Ray& ray, float hitDistance, int objectIndex)
	{
		const Sphere& closestSphere = m_ActiveScene->Spheres[objectIndex];

		Vec3f origin = ray.Origin - closestSphere.Position;

		Vec3f first_hit = origin + hitDistance * ray.Direction;
		//Vec3f last_hit = rayOrigin + t_1 * rayDirection;
		Vec3f normal = glm::normalize(first_hit);

	
		return HitInfo{ .HitDistance = hitDistance,
					    .WorldPosition = first_hit + closestSphere.Position,
					    .WorldNormal = normal,
					    .ObjectIndex = objectIndex };
	}

	SimpleRenderer::HitInfo SimpleRenderer::Miss(const Ray& ray)
	{
		return HitInfo{};
	}

	void SimpleRenderer::ProcessDataSet(uint32_t start, uint32_t end, uint32_t width)
	{
		for (uint32_t y = start; y < end; y++)
		{
			for (uint32_t x = 0; x < width; x++)
			{
				auto index = x + y * width;
				Vec4f pixelColor = PerPixel(x, y);
				m_AccumulationData[x + y * width] += pixelColor;

				Vec4f accumulatedColor = m_AccumulationData[x + y * width] / (float)m_FrameIndex;

				accumulatedColor = glm::clamp(accumulatedColor, Vec4f(0.f), Vec4f(1.0f));
				Utils::FillRGBAToPixel(m_ImageData, index, accumulatedColor);

			}
		}
	}

	
		

}
