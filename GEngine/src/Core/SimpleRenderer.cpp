#include <gepch.h>
#include "Core/SimpleRenderer.h"
#include "Camera/RayTracingCamera.h"
#include "Core/Ray.h"
#include "Core/RayTracingScene.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include "tbb/tbb/blocked_range2d.h"
#include "tbb/tbb/parallel_for.h"
#include "tbb/tbb/task_arena.h"

namespace GEngine
{
    namespace
    {
        // SplitMix64: unsigned arithmetic and an explicit 24-bit float mapping.
        // One stream per (seed, linear pixel, accumulation sample), never per worker.
        uint64_t Mix(uint64_t value)
        {
            value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
            value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
            return value ^ (value >> 31);
        }

        struct PixelRandom
        {
            uint64_t state;
            PixelRandom(uint64_t seed, size_t pixel, uint64_t sample)
                : state(Mix(seed) ^ Mix(static_cast<uint64_t>(pixel) + UINT64_C(0x9e3779b97f4a7c15))
                    ^ Mix(sample + UINT64_C(0xd1b54a32d192ed03))) {}
            float Next()
            {
                state += UINT64_C(0x9e3779b97f4a7c15);
                return static_cast<float>(Mix(state) >> 40) * (1.0f / 16777216.0f) - 0.5f;
            }
            Vec3f Roughness()
            {
                // Explicit order avoids compiler-dependent argument evaluation order.
                const float x = Next(), y = Next(), z = Next();
                return Vec3f(x, y, z);
            }
        };

        void FillRGBAToPixel(uint8_t* data, size_t index, const Vec4f& color)
        {
            data[4 * index + 0] = static_cast<uint8_t>(color.r * 255.0f);
            data[4 * index + 1] = static_cast<uint8_t>(color.g * 255.0f);
            data[4 * index + 2] = static_cast<uint8_t>(color.b * 255.0f);
            data[4 * index + 3] = static_cast<uint8_t>(color.a * 255.0f);
        }
    }

    ImageResult SimpleRenderer::OnResize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) {
            m_FinalImage.reset();
            std::vector<uint8_t>().swap(m_ImageData);
            std::vector<Vec4f>().swap(m_AccumulationData);
            m_NeedsAllocation = false;
            ResetFrameIndex();
            return {};
        }
        if (m_FinalImage && m_FinalImage->GetWidth() == width && m_FinalImage->GetHeight() == height)
            return {}; // Do not discard a pending first upload or reset active accumulation.
        const size_t pixels = static_cast<size_t>(width) * height;
        if (width > static_cast<uint32_t>((std::numeric_limits<GLsizei>::max)()) ||
            height > static_cast<uint32_t>((std::numeric_limits<GLsizei>::max)()) ||
            pixels > m_ImageData.max_size() / 4 || pixels > m_AccumulationData.max_size())
            return std::unexpected(ImageError{.code=ImageErrorCode::InvalidExtent,
                .operation="SimpleRenderer::OnResize", .message="Ray image dimensions exceed supported storage",
                .width=width, .height=height, .format=ImageFormat::RGBA,
                .sourceWidth=m_FinalImage ? m_FinalImage->GetWidth() : 0,
                .sourceHeight=m_FinalImage ? m_FinalImage->GetHeight() : 0,
                .expectedElements=(std::min)(m_ImageData.max_size()/4, m_AccumulationData.max_size()),
                .actualElements=pixels});

        // Allocate both CPU buffers before replacing the current, usable image.
        std::vector<uint8_t> imageData(pixels * 4);
        std::vector<Vec4f> accumulationData(pixels, Vec4f(0.0f));
        if (m_FinalImage) {
            if(auto resized=m_FinalImage->Resize(width,height);!resized) return std::unexpected(resized.error());
        } else {
            auto image=Image::Create(width,height,ImageFormat::RGBA);
            if(!image) return std::unexpected(image.error());
            m_FinalImage=CreateRefPtr<Image>(std::move(*image));
        }
        m_ImageData.swap(imageData);
        m_AccumulationData.swap(accumulationData);
        m_NeedsAllocation = true;
        ResetFrameIndex();
        return {};
    }

    void SimpleRenderer::RenderBegin()
    {
        if (!m_FinalImage) return;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_FinalImage->GetWidth(), m_FinalImage->GetHeight());
    }

    ImageResult SimpleRenderer::Render(const RayTracingScene& scene, const RayTracingCamera& camera)
    {
        if (!m_FinalImage) return {};
        const auto width = m_FinalImage->GetWidth();
        const auto height = m_FinalImage->GetHeight();
        if (camera.GetViewportWidth() != width || camera.GetViewportHeight() != height ||
            camera.GetRayDirections().size() != m_AccumulationData.size())
            return std::unexpected(ImageError{.code=ImageErrorCode::CameraMismatch,
                .operation="SimpleRenderer::Render", .message="Ray camera must be resized before rendering",
                .width=width, .height=height, .format=ImageFormat::RGBA,
                .sourceWidth=camera.GetViewportWidth(), .sourceHeight=camera.GetViewportHeight(),
                .expectedElements=m_AccumulationData.size(), .actualElements=camera.GetRayDirections().size()});
        const bool accumulate = m_Settings.Acculmate;
        if (!accumulate) ResetFrameIndex();
        const auto sample = m_FrameIndex;
        const auto seed = m_Settings.Seed;
        const auto bounces = m_Bounces;
        const int workers = (std::max)(1, m_NumberOfThreads);
        if (sample == 1)
            std::fill(m_AccumulationData.begin(), m_AccumulationData.end(), Vec4f(0.0f));

        const auto renderRange = [&](const tbb::blocked_range2d<size_t>& range) {
            for (size_t y = range.rows().begin(); y < range.rows().end(); ++y) {
                for (size_t x = range.cols().begin(); x < range.cols().end(); ++x) {
                    const size_t pixel = x + y * width;
                    const Vec4f pixelColor = PerPixel(scene, camera, pixel, sample, seed, bounces);
                    // Each task owns disjoint pixels; all reductions stay within that pixel.
                    m_AccumulationData[pixel] += pixelColor;
                    const Vec4f color = glm::clamp(m_AccumulationData[pixel] / static_cast<float>(sample),
                        Vec4f(0.0f), Vec4f(1.0f));
                    FillRGBAToPixel(m_ImageData.data(), pixel, color);
                }
            }
        };
        const tbb::blocked_range2d<size_t> imageRange(0, height, 0, width);
        if (workers == 1) renderRange(imageRange);
        else {
            tbb::task_arena arena(workers);
            arena.execute([&] { tbb::parallel_for(imageRange, renderRange); });
        }

        // parallel_for/execute are synchronous. Only the owning context thread uploads.
        auto uploaded=m_NeedsAllocation ? m_FinalImage->ReAllocateData(m_ImageData) : m_FinalImage->UpdateData(m_ImageData);
        if(!uploaded) return std::unexpected(uploaded.error());
        m_NeedsAllocation = false;
        if (accumulate && m_FrameIndex != (std::numeric_limits<uint64_t>::max)()) ++m_FrameIndex;
        else ResetFrameIndex();
        return {};
    }

	Vec4f SimpleRenderer::PerPixel(const RayTracingScene& scene, const RayTracingCamera& camera,
		size_t pixel, uint64_t sample, uint64_t seed, int bounces)
	{
		Ray ray{ .Origin = camera.GetPosition(),
			     .Direction = camera.GetRayDirections()[pixel]};

		PixelRandom random(seed, pixel, sample);
		Vec3f color(0.f);
		float multiplier = 1.f;

		for (int i = 0; i < bounces; i++)
		{
			auto hit_info = TraceRay(scene, ray);

			if (!hit_info.HitDistance)
			{
				Vec3f skyColor(0.6f, 0.7f, 0.9f);
				color += skyColor * multiplier;
				break;
			}

			const Sphere& sphere = scene.Spheres[hit_info.ObjectIndex];
			const MaterialInfo& material = scene.Materials[sphere.MaterialIndex];

			Vec3f lightDir = glm::normalize(Vec3f{ -1.f, -1.f, -1.f });

			float intensity = glm::max(glm::dot(hit_info.WorldNormal, -lightDir), 0.f);

			Vec3f sphereColor = material.Albedo;
			sphereColor *= intensity * multiplier;

			color += sphereColor;
			multiplier *= 0.5f;

			ray.Origin = hit_info.WorldPosition + hit_info.WorldNormal * 0.0001f;
			ray.Direction = glm::reflect(ray.Direction, hit_info.WorldNormal + material.Roughness * random.Roughness());
			
		}

		return Vec4f(color, 1.0f);

	}


	SimpleRenderer::HitInfo SimpleRenderer::TraceRay(const RayTracingScene& scene, const Ray& ray)
	{

		int closestSphere = -1;
		float hitDistance = std::numeric_limits<float>::max();

		auto size = scene.Spheres.size();

		for (size_t i = 0; i < size; ++i)
		{

			const Sphere& sphere = scene.Spheres[i];

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

		if (closestSphere < 0) return HitInfo{};

		return ClosestHit(scene, ray, hitDistance, closestSphere);


	}

	SimpleRenderer::HitInfo SimpleRenderer::ClosestHit(const RayTracingScene& scene, const Ray& ray, float hitDistance, int objectIndex)
	{
		const Sphere& closestSphere = scene.Spheres[objectIndex];

		Vec3f origin = ray.Origin - closestSphere.Position;

		Vec3f first_hit = origin + hitDistance * ray.Direction;
		//Vec3f last_hit = rayOrigin + t_1 * rayDirection;
		Vec3f normal = glm::normalize(first_hit);

	
		return HitInfo{ .HitDistance = hitDistance,
					    .WorldPosition = first_hit + closestSphere.Position,
					    .WorldNormal = normal,
					    .ObjectIndex = objectIndex };
	}

}
