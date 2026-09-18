#include "gepch.h"
#include "Scene/_Scene.h"
#include "Component/Component.h"
#include "Core/RenderTarget.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace GEngine
{
    using namespace Component;
    namespace
    {
		using VersionKey = std::array<std::uint64_t, 4>;
		template<class Tag> VersionKey Version(Asset::AssetHandle<Tag> id, std::uint64_t revision)
		{ return {id.index, id.generation, id.registry, revision}; }
		struct CachedRenderEntity
		{
			Mat4 world{1.f};
			WorldBounds bounds;
			std::optional<Component::MeshRendererComponent> mesh;
			std::optional<Component::VisibilityComponent> visibility;
			std::optional<Component::RenderCameraComponent> camera;
			std::optional<Component::RenderLightComponent> light;
			std::uint64_t meshVersion{};
			std::vector<VersionKey> materialVersions;
			std::optional<MaterialBindingCode> materialFailure;
			RenderRevisions revisions;
		};
		struct CachedRenderScene
		{
			std::unordered_map<EntityRenderId, CachedRenderEntity> entities;
			RenderRevisions revisions;
			RenderTargetRevision target;
		};
		void Advance(std::uint64_t& revision)
		{
			Asset::AssetDetail::RequireInvariant(revision != UINT64_MAX);
			++revision;
		}
		template<Component::RenderDataComponent T>
		std::optional<T> ReadRenderComponent(const entt::registry& registry, entt::entity entity)
		{
			if (const auto* value = registry.try_get<T>(entity)) return *value;
			return {};
		}

        bool FiniteMatrix(const Mat4& matrix)
        {
            for (int column = 0; column != 4; ++column)
                for (int row = 0; row != 4; ++row)
                    if (!std::isfinite(matrix[column][row])) return false;
            return true;
        }
    }

	RenderTargetRevision CaptureRenderTargetRevision(RenderTargetHandle identity, std::uint64_t publication, const RenderTarget& target)
	{
		const auto& description = target.Description();
		return {identity, publication, target.ReallocationCount(), description.Storage, description.SizeSource,
			description.Usage};
	}

	WorldBounds TransformBounds(const LocalBounds& local, const Mat4& world) noexcept
	{
		WorldBounds result;
		result.status = BoundsStatus::Invalid;
		if (!FiniteMatrix(world) || world[0][3] != 0 || world[1][3] != 0
			|| world[2][3] != 0 || world[3][3] != 1) return result;
		if (local.empty) { result.status = BoundsStatus::Empty; return result; }
		for (int axis = 0; axis != 3; ++axis)
			if (!std::isfinite(local.minimum[axis]) || !std::isfinite(local.maximum[axis])
				|| local.minimum[axis] > local.maximum[axis]) return result;
		// Affine interval transform encloses every corner, including reflected scales
		// and shear from rotated non-uniform ancestors. Round each operation outward.
		const double infinity = std::numeric_limits<double>::infinity();
		for (int row = 0; row != 3; ++row)
		{
			double lower = world[3][row], upper = lower;
			for (int column = 0; column != 3; ++column)
			{
				const double a = double(world[column][row]) * local.minimum[column];
				const double b = double(world[column][row]) * local.maximum[column];
				lower = std::nextafter(lower + std::nextafter((std::min)(a, b), -infinity), -infinity);
				upper = std::nextafter(upper + std::nextafter((std::max)(a, b), infinity), infinity);
			}
			if (!std::isfinite(lower) || !std::isfinite(upper)) return WorldBounds{BoundsStatus::Invalid};
			result.minimum[row] = lower; result.maximum[row] = upper;
			result.sphereCenter[row] = lower * .5 + upper * .5;
		}
		std::array<double, 3> extent;
		for (int axis = 0; axis != 3; ++axis)
			extent[axis] = std::nextafter((std::max)(result.maximum[axis] - result.sphereCenter[axis],
				result.sphereCenter[axis] - result.minimum[axis]), infinity);
		result.sphereRadius = std::nextafter(std::hypot(extent[0], extent[1], extent[2]), infinity);
		if (!std::isfinite(result.sphereRadius)) return WorldBounds{BoundsStatus::Invalid};
		result.status = BoundsStatus::Valid;
		return result;
	}

	std::expected<SceneRenderState, TransformError> _Scene::UpdateRenderState(const RenderStateResources& resources)
	{
		auto authoritative = UpdateWorldTransforms();
		if (!authoritative) return std::unexpected(authoritative.error());
		const CachedRenderScene empty;
		const auto* previous = m_Registry.ctx().find<CachedRenderScene>();
		const auto& old = previous ? *previous : empty;
		CachedRenderScene next;
		next.revisions = old.revisions;
		SceneRenderState result;
		result.target = resources.target;
		next.target = resources.target;
		bool transformChanged = false, meshChanged = false, materialChanged = false;
		bool lightChanged = false, cameraChanged = false, boundsChanged = false, sceneChanged = !previous;
		for (const auto& world : authoritative->transforms)
		{
			const auto entity = *m_RenderData.Resolve(world.entity); // Validated by world evaluation.
			const auto prior = old.entities.find(world.entity);
			const bool added = prior == old.entities.end();
			const CachedRenderEntity blank;
			const auto& before = added ? blank : prior->second;
			CachedRenderEntity cache;
			cache.revisions = before.revisions;
			cache.world = SampleRenderMatrix(entity);
			const auto* parent = m_Registry.try_get<RelationshipComponent>(entity);
			if (parent && parent->ParentHandle != 0 && !m_Registry.all_of<RigidBody3DComponent>(entity))
				cache.world = next.entities.at(parent->ParentIdentity).world * cache.world;
			const auto uuid = m_Registry.get<IDComponent>(entity).ID;
			if (!FiniteMatrix(cache.world))
				return std::unexpected(TransformError{TransformErrorCode::NonFiniteTransform, uuid});
			cache.mesh = ReadRenderComponent<MeshRendererComponent>(m_Registry, entity);
			cache.visibility = ReadRenderComponent<VisibilityComponent>(m_Registry, entity);
			cache.camera = ReadRenderComponent<RenderCameraComponent>(m_Registry, entity);
			cache.light = ReadRenderComponent<RenderLightComponent>(m_Registry, entity);
			if ((cache.camera && (!std::isfinite(cache.camera->verticalFovRadians)
				|| !std::isfinite(cache.camera->orthographicHeight) || !std::isfinite(cache.camera->nearPlane)
				|| !std::isfinite(cache.camera->farPlane)))
				|| (cache.light && (!std::isfinite(cache.light->intensity) || !std::isfinite(cache.light->range)
				|| !std::isfinite(cache.light->innerConeRadians) || !std::isfinite(cache.light->outerConeRadians)
				|| !std::all_of(cache.light->color.begin(), cache.light->color.end(), [](float v) { return std::isfinite(v); }))))
				return std::unexpected(TransformError{TransformErrorCode::NonFiniteRenderData, uuid});
			EntityRenderState entry;
			entry.entity = world.entity; entry.world = cache.world; entry.light = cache.light;
			if (cache.mesh)
			{
				auto mesh = resources.meshes.Acquire(resources.access, cache.mesh->mesh);
				if (mesh && **mesh)
				{
					entry.mesh = *mesh;
					cache.meshVersion = mesh->Revision();
					cache.bounds = TransformBounds((*mesh)->Bounds(), cache.world);
				}
				else entry.meshError = mesh ? Asset::RegistryError::InvalidHandle : mesh.error();
				auto material = resources.materials.Acquire(resources.access, cache.mesh->material);
				cache.materialVersions.push_back(Version(cache.mesh->material, material ? material->Revision() : 0));
				if (material)
				{
					cache.materialVersions.push_back(Version((*material)->Template(), (*material)->TemplateRevision()));
					cache.materialVersions.push_back(Version(cache.mesh->material, (*material)->Revision()));
					auto binding = PreparedMaterialBinding::Prepare(*material, resources.access, resources.bindings);
					if (binding)
					{
						cache.materialVersions.push_back(Version(binding->Program().Identity(), binding->Program().Revision()));
						for (const auto& texture : binding->Textures())
						{
							cache.materialVersions.push_back(Version(texture.texture.Identity(), texture.texture.Revision()));
							cache.materialVersions.push_back(Version(texture.sampler.Identity(), texture.sampler.Revision()));
						}
						entry.material = std::move(*binding);
					}
					else entry.materialError = binding.error();
				}
				else entry.materialError = MaterialBindingError{MaterialBindingCode::InvalidInstance, {},
					"Material instance unavailable", material.error()};
				if (entry.materialError) cache.materialFailure = entry.materialError->code;
			}
			const bool transform = added || cache.world != before.world;
			// Material assignment has its own category; mesh flags remain mesh intent.
			auto meshIntent = cache.mesh, previousIntent = before.mesh;
			if (meshIntent) meshIntent->material = {};
			if (previousIntent) previousIntent->material = {};
			const bool mesh = meshIntent != previousIntent || cache.meshVersion != before.meshVersion;
			const bool material = cache.materialVersions != before.materialVersions || cache.materialFailure != before.materialFailure;
			const bool light = cache.light != before.light || (transform && cache.light.has_value());
			const bool camera = cache.camera != before.camera || (transform && cache.camera.has_value());
			const bool bounds = cache.bounds != before.bounds || ((transform || mesh) && cache.mesh.has_value());
			const bool changed = transform || mesh || material || light || camera || bounds || cache.visibility != before.visibility;
			if (transform) Advance(cache.revisions.transform);
			if (mesh) Advance(cache.revisions.mesh);
			if (material) Advance(cache.revisions.material);
			if (light) Advance(cache.revisions.light);
			if (camera) Advance(cache.revisions.camera);
			if (bounds) Advance(cache.revisions.bounds);
			if (changed) Advance(cache.revisions.scene);
			transformChanged |= transform; meshChanged |= mesh; materialChanged |= material;
			lightChanged |= light; cameraChanged |= camera; boundsChanged |= bounds; sceneChanged |= changed;
			entry.bounds = cache.bounds; entry.revisions = cache.revisions;
			result.entities.push_back(std::move(entry));
			next.entities.emplace(world.entity, std::move(cache));
		}
		for (const auto& [id, removed] : old.entities)
			if (!next.entities.contains(id))
			{
				sceneChanged = transformChanged = true;
				meshChanged |= removed.mesh.has_value(); materialChanged |= removed.mesh.has_value();
				boundsChanged |= removed.mesh.has_value(); lightChanged |= removed.light.has_value(); cameraChanged |= removed.camera.has_value();
			}
		const bool targetChanged = next.target != old.target;
		if (transformChanged) Advance(next.revisions.transform);
		if (meshChanged) Advance(next.revisions.mesh);
		if (materialChanged) Advance(next.revisions.material);
		if (lightChanged) Advance(next.revisions.light);
		if (cameraChanged) Advance(next.revisions.camera);
		if (boundsChanged) Advance(next.revisions.bounds);
		if (targetChanged) Advance(next.revisions.target);
		if (sceneChanged || targetChanged) Advance(next.revisions.scene);
		result.revisions = next.revisions;
		if (previous) m_Registry.ctx().erase<CachedRenderScene>();
		m_Registry.ctx().emplace<CachedRenderScene>(std::move(next));
		return result;
	}

}
