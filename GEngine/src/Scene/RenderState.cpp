#include "gepch.h"
#include "Scene/_Scene.h"
#include "Component/Component.h"
#include "Core/RenderTarget.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include "../Renderer/RenderCpuBacking.h"

namespace GEngine
{
    using namespace Component;
    namespace
    {
        using VersionKey = std::array<std::uint64_t,4>;
        using VersionList = std::vector<VersionKey>;
        template<class Tag> VersionKey Version(Asset::AssetHandle<Tag> id,std::uint64_t revision)
        { return {id.index,id.generation,id.registry,revision}; }
        bool SameFloat(float a,float b) { return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b); }
        template<class T> bool SameScalars(const T& a,const T& b,std::size_t n) {
            for(std::size_t i=0;i<n;++i) if(!SameFloat(a[i],b[i]))return false;
            return true;
        }
        bool SameMatrix(const Mat4&a,const Mat4&b) {
            for(int i=0;i<4;++i)if(!SameScalars(a[i],b[i],4))return false;
            return true;
        }
        bool SameInput(const RenderPresentationInput&a,const RenderPresentationInput&b) {
            return a.interpolate==b.interpolate && SameFloat(a.alpha,b.alpha)
                && SameScalars(a.translation,b.translation,3)&&SameScalars(a.scale,b.scale,3)
                &&SameScalars(a.rotation,b.rotation,4)&&SameScalars(a.previousTranslation,b.previousTranslation,3)
                &&SameScalars(a.currentTranslation,b.currentTranslation,3)&&SameScalars(a.previousRotation,b.previousRotation,4)
                &&SameScalars(a.currentRotation,b.currentRotation,4);
        }
        // Comparison metadata only. No resolved resource owner is stored here.
        struct DependencyStamp : RenderCpu::CpuObject
        {
            RenderCpu::CpuArray<VersionKey> orderedKeys;
            std::uint64_t lookupHash{};
        };
        using DependencyOwner = RenderCpu::CpuOwner<const DependencyStamp>;
        struct MaterialStamp
        {
            std::uint64_t publicationRevision{}, authoredRevision{};
            DependencyOwner dependencies;
        };
        struct MaterialRelationship
        {
            Asset::MaterialInstanceHandle handle;
            MaterialStamp stamp;
        };
        struct DependencySlot
        {
            std::uint64_t hash{};
            DependencyOwner value;
        };
        static_assert(sizeof(MaterialStamp)==24 && sizeof(MaterialRelationship)==48);
        static_assert(sizeof(DependencyStamp)==40 && sizeof(DependencySlot)==16);
        TransformError MetadataError(RenderCpu::StorageFailure error) noexcept
        {
            return {error==RenderCpu::StorageFailure::Allocation
                ?TransformErrorCode::AllocationFailed:TransformErrorCode::CapacityOverflow};
        }
        bool SameDependencies(const DependencyOwner& a,const DependencyOwner& b) noexcept
        {
            if(a.get()==b.get())return true;
            if(!a||!b)return false;
            return a->lookupHash==b->lookupHash && a->orderedKeys.size()==b->orderedKeys.size()
                &&std::equal(a->orderedKeys.begin(),a->orderedKeys.end(),b->orderedKeys.begin());
        }
        bool SameMaterial(const MaterialRelationship* old,const MaterialRelationship* current,
            const MaterialStamp* fresh) noexcept
        {
            if(!old||!current)return !old&&!current;
            return old->handle==current->handle && old->stamp.publicationRevision==fresh->publicationRevision
                &&old->stamp.authoredRevision==fresh->authoredRevision
                &&SameDependencies(old->stamp.dependencies,fresh->dependencies);
        }
        std::size_t DependencyCount(const VersionList& versions) noexcept
        {
            Asset::AssetDetail::RequireInvariant(versions.size()==1||versions.size()>=3);
            return versions.size()==1?0:versions.size()-2;
        }
        const VersionKey& DependencyAt(const VersionList& versions,std::size_t i) noexcept
        {
            return versions[i==0?1:i+2]; // Skip both repeated material-identity keys.
        }
        std::uint64_t DependencyHash(const VersionList& versions) noexcept
        {
            std::uint64_t value=1469598103934665603ull;
            for(std::size_t i=0;i<DependencyCount(versions);++i)
                for(auto word:DependencyAt(versions,i)) {
                    // Integer hashing is only an index accelerator; exact equality follows.
                    value^=word;value*=1099511628211ull;
                }
            value^=DependencyCount(versions);value*=1099511628211ull;
            return value;
        }
        bool SameDependencyKeys(const DependencyStamp& stamp,const VersionList& versions) noexcept
        {
            if(stamp.orderedKeys.size()!=DependencyCount(versions))return false;
            for(std::size_t i=0;i<stamp.orderedKeys.size();++i)
                if(stamp.orderedKeys[i]!=DependencyAt(versions,i))return false;
            return true;
        }
        struct DependencyIndex
        {
            RenderCpu::CpuArray<DependencySlot> slots;
            static std::expected<DependencyIndex,RenderCpu::StorageFailure> Create(
                std::size_t oldCount,std::size_t currentCount) noexcept
            {
                const auto maximum=static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)())/sizeof(DependencySlot);
                if(oldCount>maximum||currentCount>maximum-oldCount||(oldCount+currentCount)>maximum/2)
                    return std::unexpected(RenderCpu::StorageFailure::Capacity);
                const auto required=2*(oldCount+currentCount);
                DependencyIndex result;
                if(!required)return result;
                std::size_t capacity=1;
                while(capacity<required) {
                    if(capacity>maximum/2)return std::unexpected(RenderCpu::StorageFailure::Capacity);
                    capacity*=2;
                }
                if(auto sized=result.slots.Resize(capacity);!sized)return std::unexpected(sized.error());
                return result;
            }
            void Seed(const DependencyOwner& value) noexcept
            {
                if(!value)return;
                Asset::AssetDetail::RequireInvariant(slots.size()!=0);
                auto i=static_cast<std::size_t>(value->lookupHash)&(slots.size()-1);
                while(slots[i].value) {
                    if(slots[i].hash==value->lookupHash&&SameDependencies(slots[i].value,value))return;
                    i=(i+1)&(slots.size()-1);
                }
                slots[i]={value->lookupHash,value};
            }
            std::expected<DependencyOwner,RenderCpu::StorageFailure> Intern(const VersionList& versions) noexcept
            {
                if(!DependencyCount(versions))return DependencyOwner{};
                const auto hash=DependencyHash(versions);
                Asset::AssetDetail::RequireInvariant(slots.size()!=0);
                auto i=static_cast<std::size_t>(hash)&(slots.size()-1);
                while(slots[i].value) {
                    if(slots[i].hash==hash&&SameDependencyKeys(*slots[i].value,versions))return slots[i].value;
                    i=(i+1)&(slots.size()-1);
                }
                auto created=RenderCpu::CpuOwner<DependencyStamp>::Create();
                if(!created)return std::unexpected(created.error());
                if(auto sized=(*created)->orderedKeys.Resize(DependencyCount(versions));!sized)return std::unexpected(sized.error());
                for(std::size_t j=0;j<DependencyCount(versions);++j)(*created)->orderedKeys[j]=DependencyAt(versions,j);
                (*created)->lookupHash=hash;
                DependencyOwner immutable(std::move(*created));
                slots[i]={hash,immutable};
                return immutable;
            }
        };
        struct CachedRenderEntity
        {
            EntityRenderId identity{},parent{};
            std::size_t parentOrdinal=SIZE_MAX,meshGroup=SIZE_MAX,materialGroup=SIZE_MAX;
            RenderPresentationInput input;
            Mat4 sample{1.f},world{1.f},parentWorld{1.f};
            bool anchor{};
            WorldBounds bounds;
            std::optional<MeshRendererComponent> mesh;
            std::optional<VisibilityComponent> visibility;
            std::optional<RenderCameraComponent> camera;
            std::optional<RenderLightComponent> light;
            std::uint64_t meshVersion{};
            std::optional<MaterialBindingCode> materialFailure;
            RenderRevisions revisions;
        };
        template<class Handle> struct Relationship
        {
            Handle handle;
            std::vector<std::size_t> dependents;
            std::shared_ptr<const VersionList> versions; // Used by materials; no resource owner.
        };
        struct CachedRenderScene
        {
            std::vector<CachedRenderEntity> entities;
            std::unordered_map<EntityRenderId,std::size_t> index;
            std::vector<Relationship<Asset::MeshHandle>> meshes;
            std::vector<MaterialRelationship> materials;
            std::size_t relationships{};
            RenderRevisions revisions;
            RenderTargetRevision target;
            std::shared_ptr<const RenderCpu::SceneSnapshot> snapshot;
            std::shared_ptr<RenderCpu::Domain> domain;
            std::uint64_t semanticEpoch{};
        };
        bool SameCamera(const std::optional<RenderCameraComponent>&a,const std::optional<RenderCameraComponent>&b) {
            if(bool(a)!=bool(b))return false;if(!a)return true;
            return a->projection==b->projection&&SameFloat(a->verticalFovRadians,b->verticalFovRadians)
                &&SameFloat(a->orthographicHeight,b->orthographicHeight)&&SameFloat(a->nearPlane,b->nearPlane)
                &&SameFloat(a->farPlane,b->farPlane)&&a->visibleLayers==b->visibleLayers&&a->primary==b->primary;
        }
        bool SameLight(const std::optional<RenderLightComponent>&a,const std::optional<RenderLightComponent>&b) {
            if(bool(a)!=bool(b))return false;if(!a)return true;
            return a->kind==b->kind&&SameScalars(a->color,b->color,3)&&SameFloat(a->intensity,b->intensity)
                &&SameFloat(a->range,b->range)&&SameFloat(a->innerConeRadians,b->innerConeRadians)
                &&SameFloat(a->outerConeRadians,b->outerConeRadians)&&a->castShadows==b->castShadows;
        }
        void Advance(std::uint64_t& revision) {
            Asset::AssetDetail::RequireInvariant(revision!=UINT64_MAX);++revision;
        }
        template<RenderDataComponent T> std::optional<T> ReadRenderComponent(const entt::registry& registry,entt::entity entity) {
            if(const auto*value=registry.try_get<T>(entity))return *value;
            return {};
        }
        bool FiniteMatrix(const Mat4& matrix) {
            for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(matrix[c][r]))return false;
            return true;
        }
        std::size_t CpuBytes(const CachedRenderScene& c) {
            std::size_t n=sizeof(c)+c.entities.capacity()*sizeof(CachedRenderEntity)+c.index.size()*sizeof(decltype(c.index)::value_type)
                +c.index.bucket_count()*sizeof(void*)+c.meshes.capacity()*sizeof(decltype(c.meshes)::value_type)
                +c.materials.capacity()*sizeof(decltype(c.materials)::value_type);
            for(const auto&m:c.meshes)n+=m.dependents.capacity()*sizeof(std::size_t);
            for(std::size_t i=0;i<c.materials.size();++i) {
                const auto* dependency=c.materials[i].stamp.dependencies.get();
                if(!dependency)continue;
                bool counted=false;
                for(std::size_t j=0;j<i;++j)if(c.materials[j].stamp.dependencies.get()==dependency){counted=true;break;}
                if(!counted)n+=sizeof(DependencyStamp)+dependency->orderedKeys.capacity()*sizeof(VersionKey);
            }
            return n; // excludes allocator book-keeping; peak/RSS measured separately.
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


    std::expected<SceneRenderState,TransformError> _Scene::UpdateRenderState(const RenderStateResources& resources)
    {

        auto authoritative=UpdateWorldTransforms();
        if(!authoritative)return std::unexpected(authoritative.error());
        const auto count=authoritative->transforms.size();
        auto* previous=m_Registry.ctx().find<CachedRenderScene>();
        const CachedRenderScene empty;
        const auto& old=previous?*previous:empty;

        struct Observed {
            entt::entity entity;
            RenderPresentationInput input;
            EntityRenderId parent;
            bool anchor{};
            std::optional<MeshRendererComponent> mesh;
            std::optional<VisibilityComponent> visibility;
            std::optional<RenderCameraComponent> camera;
            std::optional<RenderLightComponent> light;
        };
        std::vector<Observed> observations;observations.reserve(count);
        bool rebuild=!previous||old.entities.size()!=count;
        for(std::size_t i=0;i<count;++i)
        {
            const auto id=authoritative->transforms[i].entity;
            const auto entity=*m_RenderData.Resolve(id);
            Observed o{entity,ObserveRenderPresentation(entity)};
            if(const auto* parent=m_Registry.try_get<RelationshipComponent>(entity);parent&&parent->ParentHandle!=0)o.parent=parent->ParentIdentity;
            o.anchor=m_Registry.all_of<RigidBody3DComponent>(entity);
            o.mesh=ReadRenderComponent<MeshRendererComponent>(m_Registry,entity);
            o.visibility=ReadRenderComponent<VisibilityComponent>(m_Registry,entity);
            o.camera=ReadRenderComponent<RenderCameraComponent>(m_Registry,entity);
            o.light=ReadRenderComponent<RenderLightComponent>(m_Registry,entity);
            if(!rebuild) {
                const auto& before=old.entities[i];
                if(before.identity!=id || bool(before.mesh)!=bool(o.mesh)
                    || (o.mesh&&(before.mesh->mesh!=o.mesh->mesh||before.mesh->material!=o.mesh->material)))rebuild=true;
            }
            observations.push_back(std::move(o));
        }

        CachedRenderScene rebuilt;
        const CachedRenderScene* graph=&old;
        if(rebuild)
        {
            rebuilt.entities.resize(count);rebuilt.revisions=old.revisions;rebuilt.target=old.target;
            std::map<Asset::MeshHandle,std::size_t> meshes;
            std::map<Asset::MaterialInstanceHandle,std::size_t> materials;
            std::set<std::pair<Asset::MeshHandle,Asset::MaterialInstanceHandle>> pairs;
            for(std::size_t i=0;i<count;++i)
            {
                const auto id=authoritative->transforms[i].entity;
                auto& row=rebuilt.entities[i];
                if(const auto found=old.index.find(id);found!=old.index.end())row=old.entities[found->second];
                row.identity=id;row.meshGroup=row.materialGroup=SIZE_MAX;
                rebuilt.index.emplace(id,i);
                if(const auto& mesh=observations[i].mesh)
                {
                    auto [mi,newMesh]=meshes.emplace(mesh->mesh,rebuilt.meshes.size());
                    if(newMesh)rebuilt.meshes.push_back({mesh->mesh});
                    row.meshGroup=mi->second;rebuilt.meshes[row.meshGroup].dependents.push_back(i);
                    auto [ma,newMaterial]=materials.emplace(mesh->material,rebuilt.materials.size());
                    if(newMaterial)
                    {
                        MaterialRelationship relation{mesh->material};
                        for(const auto& prior:old.materials)if(prior.handle==mesh->material){relation.stamp=prior.stamp;break;}
                        rebuilt.materials.push_back(std::move(relation));
                    }
                    row.materialGroup=ma->second;
                    pairs.emplace(mesh->mesh,mesh->material);
                }
            }
            rebuilt.relationships=pairs.size();graph=&rebuilt;

        }

        // All fresh comparison metadata stays private until the existing cache commit.
        RenderCpu::CpuArray<MaterialStamp> materialStamps;
        if(auto sized=materialStamps.Resize(graph->materials.size());!sized)return std::unexpected(MetadataError(sized.error()));
        auto dependencyIndex=DependencyIndex::Create(old.materials.size(),graph->materials.size());
        if(!dependencyIndex)return std::unexpected(MetadataError(dependencyIndex.error()));
        for(const auto& relation:old.materials)dependencyIndex->Seed(relation.stamp.dependencies);

        // Fresh resource owners are confined to this call's envelope.
        auto bindings=std::make_shared<RenderCpu::CallBindings>();
        bindings->meshes.resize(graph->meshes.size());bindings->materials.resize(graph->materials.size());
        auto& meshes=bindings->meshes;auto& materials=bindings->materials;
        for(std::size_t i=0;i<meshes.size();++i)
        {
            auto mesh=resources.meshes.Acquire(resources.access,graph->meshes[i].handle);
            RW_COUNT(meshAcquire,1);
            if(mesh&&**mesh) {
                meshes[i].mesh=FrameMeshReference(std::make_shared<const MeshView>(std::move(*mesh)));
                RW_COUNT(ownerGroups,1);
            } else meshes[i].error=mesh?Asset::RegistryError::InvalidHandle:mesh.error();
        }
        for(std::size_t i=0;i<materials.size();++i)
        {
            const auto& relation=graph->materials[i];auto& out=materials[i];
            auto material=resources.materials.Acquire(resources.access,relation.handle);
            RW_COUNT(materialAcquire,1);
            VersionList versions;versions.push_back(Version(relation.handle,material?material->Revision():0));
            if(material)
            {
                versions.push_back(Version((*material)->Template(),(*material)->TemplateRevision()));
                versions.push_back(Version(relation.handle,(*material)->Revision()));
                auto binding=PreparedMaterialBinding::Prepare(*material,resources.access,resources.bindings);
                RW_COUNT(prepare,1);
                if(binding)
                {
                    versions.push_back(Version(binding->Program().Identity(),binding->Program().Revision()));
                    for(const auto& texture:binding->Textures()) {
                        versions.push_back(Version(texture.texture.Identity(),texture.texture.Revision()));
                        versions.push_back(Version(texture.sampler.Identity(),texture.sampler.Revision()));
                    }
                    out.material=std::make_shared<const PreparedMaterialBinding>(std::move(*binding));
                    RW_COUNT(ownerGroups,1);
                } else out.error=binding.error();
            } else out.error=MaterialBindingError{MaterialBindingCode::InvalidInstance,{},"Material instance unavailable",material.error()};
            auto dependency=dependencyIndex->Intern(versions);
            if(!dependency)return std::unexpected(MetadataError(dependency.error()));
            materialStamps[i]={versions[0][3],versions.size()==1?0:versions[2][3],std::move(*dependency)};
        }
        SceneRenderState result;result.target=resources.target;
        auto domain=old.domain?old.domain:std::make_shared<RenderCpu::Domain>();
        std::vector<const CachedRenderEntity*> evaluated(count);
        std::shared_ptr<RenderCpu::SceneSnapshot> staged;
        std::vector<std::shared_ptr<RenderCpu::SemanticPage>> stagedPages((count+127)/128);
        auto semanticEpoch=old.semanticEpoch;
        std::size_t transformedRows{};
        std::vector<std::pair<std::size_t,CachedRenderEntity>> changes; changes.reserve(count);
        bool transformChanged=false,meshChanged=false,materialChanged=false,lightChanged=false,cameraChanged=false,boundsChanged=false,sceneChanged=!previous;
        const CachedRenderEntity blank;
        for(std::size_t i=0;i<count;++i)
        {
            const auto& o=observations[i];const auto id=authoritative->transforms[i].entity;
            const auto oldIt=old.index.find(id);
            const bool added=oldIt==old.index.end();
            const auto& before=added?blank:old.entities[oldIt->second];
            const auto mg=graph->entities[i].meshGroup, ag=graph->entities[i].materialGroup;
            const auto parentOrdinal=o.parent&&!o.anchor?graph->index.at(o.parent):SIZE_MAX;
            const Mat4& parentWorld=parentOrdinal==SIZE_MAX?blank.parentWorld:evaluated[parentOrdinal]->world;
            const auto meshVersion=mg==SIZE_MAX?0:meshes[mg].mesh.Revision();
            const auto* priorMaterial=before.materialGroup==SIZE_MAX?nullptr:&old.materials[before.materialGroup];
            Asset::AssetDetail::RequireInvariant(!priorMaterial||(before.mesh&&priorMaterial->handle==before.mesh->material));
            const auto* currentMaterial=ag==SIZE_MAX?nullptr:&graph->materials[ag];
            const bool sameVersions=SameMaterial(priorMaterial,currentMaterial,ag==SIZE_MAX?nullptr:&materialStamps[ag]);
            const auto failure=ag!=SIZE_MAX&&materials[ag].error?std::optional(materials[ag].error->code):std::nullopt;
            const bool exactSame=!added&&SameInput(o.input,before.input)&&o.parent==before.parent&&o.anchor==before.anchor
                &&SameMatrix(parentWorld,before.parentWorld)&&o.mesh==before.mesh&&o.visibility==before.visibility
                &&SameCamera(o.camera,before.camera)&&SameLight(o.light,before.light)
                &&meshVersion==before.meshVersion&&sameVersions&&failure==before.materialFailure
                &&mg==before.meshGroup&&ag==before.materialGroup;
            if(exactSame&&!rebuild) { evaluated[i]=&before;continue; }
            CachedRenderEntity cache=before;
            cache.identity=id;cache.meshGroup=graph->entities[i].meshGroup;cache.materialGroup=graph->entities[i].materialGroup;
            cache.input=o.input;cache.parent=o.parent;cache.anchor=o.anchor;
            const bool sampleChanged=added||!SameInput(cache.input,before.input);
            if(sampleChanged){cache.sample=EvaluateRenderPresentation(cache.input);}
            cache.parentOrdinal=cache.parent&&!cache.anchor?graph->index.at(cache.parent):SIZE_MAX;
            cache.parentWorld=parentWorld;
            if(sampleChanged||cache.parent!=before.parent||cache.anchor!=before.anchor||!SameMatrix(cache.parentWorld,before.parentWorld))
                cache.world=cache.parentOrdinal==SIZE_MAX?cache.sample:cache.parentWorld*cache.sample;
            const auto uuid=m_Registry.get<IDComponent>(o.entity).ID;
            if(!FiniteMatrix(cache.world))return std::unexpected(TransformError{TransformErrorCode::NonFiniteTransform,uuid});
            cache.mesh=o.mesh;cache.visibility=o.visibility;cache.camera=o.camera;cache.light=o.light;
            if((cache.camera&&(!std::isfinite(cache.camera->verticalFovRadians)||!std::isfinite(cache.camera->orthographicHeight)
                ||!std::isfinite(cache.camera->nearPlane)||!std::isfinite(cache.camera->farPlane)))
                ||(cache.light&&(!std::isfinite(cache.light->intensity)||!std::isfinite(cache.light->range)
                ||!std::isfinite(cache.light->innerConeRadians)||!std::isfinite(cache.light->outerConeRadians)
                ||!std::all_of(cache.light->color.begin(),cache.light->color.end(),[](float v){return std::isfinite(v);}))))
                return std::unexpected(TransformError{TransformErrorCode::NonFiniteRenderData,uuid});

            cache.meshVersion=0;cache.materialFailure.reset();
            cache.bounds=WorldBounds{};
            if(cache.mesh)
            {
                const auto& mesh=meshes[cache.meshGroup];const auto& material=materials[cache.materialGroup];
                if(mesh.mesh) {
                    cache.meshVersion=mesh.mesh.Revision();
                    if(!added&&before.mesh&&before.mesh->mesh==cache.mesh->mesh&&before.meshVersion==cache.meshVersion&&SameMatrix(before.world,cache.world))
                        cache.bounds=before.bounds;
                    else {cache.bounds=TransformBounds(mesh.mesh->Bounds(),cache.world);RW_COUNT(bounds,1);}
                }
                if(material.error)cache.materialFailure=material.error->code;

            }
            const bool transform=added||cache.world!=before.world;
            if(transform)++transformedRows;
            auto meshIntent=cache.mesh,previousIntent=before.mesh;
            if(meshIntent)meshIntent->material={};if(previousIntent)previousIntent->material={};
            const bool mesh=meshIntent!=previousIntent||cache.meshVersion!=before.meshVersion;
            const bool material=!sameVersions||cache.materialFailure!=before.materialFailure;
            const bool light=cache.light!=before.light||(transform&&cache.light.has_value());
            const bool camera=cache.camera!=before.camera||(transform&&cache.camera.has_value());
            const bool bounds=cache.bounds!=before.bounds||((transform||mesh)&&cache.mesh.has_value());
            const bool changed=transform||mesh||material||light||camera||bounds||cache.visibility!=before.visibility;
            if(transform)Advance(cache.revisions.transform);
            if(mesh)Advance(cache.revisions.mesh);
            if(material)Advance(cache.revisions.material);
            if(light)Advance(cache.revisions.light);
            if(camera)Advance(cache.revisions.camera);
            if(bounds)Advance(cache.revisions.bounds);
            if(changed)Advance(cache.revisions.scene);
            transformChanged|=transform;meshChanged|=mesh;materialChanged|=material;lightChanged|=light;
            cameraChanged|=camera;boundsChanged|=bounds;sceneChanged|=changed;

            changes.emplace_back(i,std::move(cache));
            evaluated[i]=&changes.back().second;
            if(!staged) {
                staged=std::make_shared<RenderCpu::SceneSnapshot>();
                if(!rebuild&&old.snapshot)staged->pages=old.snapshot->pages;
                staged->size=count;staged->pages.resize((count+127)/128);
            }
            const auto page=i/RenderCpu::PageSize;
            if(!stagedPages[page]) {
                stagedPages[page]=std::make_shared<RenderCpu::SemanticPage>();
                if(!rebuild&&staged->pages[page])*stagedPages[page]=*staged->pages[page];
                staged->pages[page]=stagedPages[page];RW_COUNT(pagesCloned,1);
            }
            const auto& row=*evaluated[i];auto& cpu=stagedPages[page]->rows[i%RenderCpu::PageSize];
            cpu={row.identity,row.world,row.bounds,row.revisions,row.light,row.mesh,row.visibility,row.camera,
                row.meshGroup,row.materialGroup,RenderCpu::Advance(semanticEpoch)};
            RW_COUNT(semanticRows,1);

        }
        for(const auto& removed:old.entities)if(!graph->index.contains(removed.identity))
        {
            sceneChanged=transformChanged=true;meshChanged|=removed.mesh.has_value();materialChanged|=removed.mesh.has_value();
            boundsChanged|=removed.mesh.has_value();lightChanged|=removed.light.has_value();cameraChanged|=removed.camera.has_value();
        }
        auto revisions=old.revisions;const bool targetChanged=resources.target!=old.target;
        if(transformChanged)Advance(revisions.transform);if(meshChanged)Advance(revisions.mesh);if(materialChanged)Advance(revisions.material);
        if(lightChanged)Advance(revisions.light);if(cameraChanged)Advance(revisions.camera);if(boundsChanged)Advance(revisions.bounds);
        if(targetChanged)Advance(revisions.target);if(sceneChanged||targetChanged)Advance(revisions.scene);
        result.revisions=revisions;
        // No render-cache changes were installed before all entity validation completed.
        if(rebuild) {
            for(auto& [i,row]:changes)rebuilt.entities[i]=std::move(row);
            for(std::size_t i=0;i<materials.size();++i)rebuilt.materials[i].stamp=materialStamps[i];
            rebuilt.revisions=revisions;rebuilt.target=resources.target;
            rebuilt.snapshot=staged;rebuilt.domain=domain;rebuilt.semanticEpoch=semanticEpoch;
            if(previous)*previous=std::move(rebuilt);
            else previous=&m_Registry.ctx().emplace<CachedRenderScene>(std::move(rebuilt));
        } else {
            for(auto& [i,row]:changes)previous->entities[i]=std::move(row);
            for(std::size_t i=0;i<materials.size();++i)previous->materials[i].stamp=materialStamps[i];
            previous->revisions=revisions;previous->target=resources.target;
            if(staged)previous->snapshot=staged;previous->semanticEpoch=semanticEpoch;
        }
        result.entities=SceneEntityViews(previous->snapshot,std::move(bindings),count);
        result.cpuDomain=domain;result.fullTransformChange=count&&transformedRows==count;RW_COUNT(scanned,count);RW_COUNT(world,authoritative->recomputed);
        #ifdef GENGINE_RENDER_WORLD_DIAGNOSTICS
        RenderCpu::counts.sceneCpuBytes=CpuBytes(*previous);
        RenderCpu::counts.semanticBytes=sizeof(RenderCpu::SceneSnapshot)
            +(previous->snapshot?previous->snapshot->pages.capacity()*sizeof(std::shared_ptr<const RenderCpu::SemanticPage>)
                +previous->snapshot->pages.size()*sizeof(RenderCpu::SemanticPage):0);
#endif
        return result;
    }
    namespace {
        const RenderSemanticRecord EmptyRecord;
        const FrameMeshReference EmptyMesh;
        const FrameMaterialReference EmptyMaterial;
        const std::optional<Asset::RegistryError> EmptyMeshError;
        const std::optional<MaterialBindingError> EmptyMaterialError;
    }
    EntityRenderState::EntityRenderState() noexcept
        : EntityRenderState(EmptyRecord,EmptyMesh,EmptyMaterial,EmptyMeshError,EmptyMaterialError) {}
    EntityRenderState::EntityRenderState(const RenderSemanticRecord& value,const FrameMeshReference& m,
        const FrameMaterialReference& a,const std::optional<Asset::RegistryError>& me,
        const std::optional<MaterialBindingError>& ae) noexcept
        : cpu(value),entity(value.entity),world(value.world),bounds(value.bounds),revisions(value.revisions),
          light(value.light),mesh(m),material(a),meshError(me),materialError(ae),
          meshGroup(value.meshGroup),materialGroup(value.materialGroup) {}
    EntityRenderState& EntityRenderState::operator=(const EntityRenderState& other) noexcept {
        if(this!=&other){std::destroy_at(this);std::construct_at(this,other);}return *this;
    }
    EntityRenderState EntityRenderState::WithMaterial(const FrameMaterialReference& value) const noexcept
    { EntityRenderState result=*this;result.material=value;return result; }
    SceneEntityViews::SceneEntityViews(std::shared_ptr<const RenderCpu::SceneSnapshot> snapshot,
        std::shared_ptr<RenderCpu::CallBindings> bindings,std::size_t count) noexcept
        :m_Snapshot(std::move(snapshot)),m_Bindings(std::move(bindings)),m_Count(count) {}
    EntityRenderState SceneEntityViews::operator[](std::size_t i) const noexcept {
        Asset::AssetDetail::RequireInvariant(i<m_Count);
        const auto& row=m_Snapshot->At(i);
        if(row.meshGroup==SIZE_MAX) {
            EntityRenderState result{row,EmptyMesh,EmptyMaterial,EmptyMeshError,EmptyMaterialError};
            result.snapshotOwner=m_Snapshot;result.bindingOwner=m_Bindings;return result;
        }
        const auto& m=m_Bindings->meshes[row.meshGroup];const auto& a=m_Bindings->materials[row.materialGroup];
        EntityRenderState result{row,m.mesh,a.material,m.error,a.error};
        result.snapshotOwner=m_Snapshot;result.bindingOwner=m_Bindings;return result;
    }

}
