// Compile-only semantic retained-renderer contracts; never an executable workload.
#include "Core/Renderer2D.h"
#include <type_traits>
using namespace GEngine;
static_assert(requires(Renderer2D::Groups& groups, const Material& material,
    std::vector<std::vector<SpriteEntity*>> sprites) { groups.Set(material, sprites); });
template<class T> concept NativeSpriteMap = requires(const T& value, CameraBase* camera) { Renderer2D::Render(value, camera); };
static_assert(!NativeSpriteMap<std::unordered_map<unsigned, std::vector<std::vector<SpriteEntity*>>>>);
static_assert(NativeSpriteMap<Renderer2D::Groups>);

#include "Core/Scene.h"
template<class T> concept NativeMaterialProgram = requires(const T& value) { value.GetShaderID(); };
template<class T> concept NativeMaterialRef = requires(const T& value) { value.GetShaderRef(); };
template<class T> concept NativeGroupProgram = requires(const T& value) { value.GetProgramID(); };
static_assert(!NativeMaterialProgram<Material> && !NativeMaterialRef<Material>);
static_assert(!NativeGroupProgram<Group<Entity>>);
static_assert(requires(Scene& scene, Group<Entity>* group) { scene.Push(group); });

#include "Core/RenderSystem.h"
template<class T> concept PublicNativeGroups = requires(T& scene) { scene.GetGroupEntities(); };
template<class T> concept PublicNativeLights = requires(T& scene) { scene.GetLightEntities(); };
template<class T> concept PublicNativeLightKey = requires(const T& scene) { scene.GetLightEntitiesWithRenderID(1u); };
template<class T> concept PublicNativeDraw = requires { T::ElementsDraw(4u, 3, 0x1405u, nullptr); };
static_assert(!PublicNativeGroups<_Scene> && !PublicNativeLights<_Scene> && !PublicNativeLightKey<_Scene>);
static_assert(!PublicNativeDraw<RenderSystem>);

template<class T> concept PublicTextureTarget = requires(T& value) { value.GetTextureTarget(); value.SetTextureTarget(0u); };
template<class T> concept PublicTextureList = requires(T& value) { value.GetTextureList(); };
template<class T> concept PublicRenderTargetToken = requires(T& value) { value.m_TexTarget; };
template<class T> concept PublicNativeTextureOverride = requires(T& value) { value.BindTexture(0x0DE1); };
template<class T> concept PublicNativeUniformTuple = requires(T& value) {
    value.template SetUniforms<std::pair<unsigned, std::pair<unsigned, unsigned>>>(
        std::map<std::string, std::pair<unsigned, std::pair<unsigned, unsigned>>>{}); };
static_assert(!PublicTextureTarget<Material> && !PublicTextureList<Material>);
static_assert(!PublicRenderTargetToken<RenderSetting> && !PublicNativeTextureOverride<Entity>);
static_assert(!PublicNativeUniformTuple<Material>);
static_assert(requires(Entity& entity, Material& material) { entity.BindTextures(); material.BindTextureUniforms(); });

#include "Material/AAScreenMaterial.h"
static_assert(std::is_same_v<decltype(AAScreenMaterial::Create(std::declval<const Asset::TextureView&>())),
    AAScreenMaterial::CreationResult>);
static_assert(!std::is_constructible_v<AAScreenMaterial, Material::Construction&, unsigned>);
static_assert(!std::is_constructible_v<AAScreenMaterial, Material::Construction&, const std::string&, const std::string&>);

#include "Sprite/SpriteEntity.h"
static_assert(!PublicNativeUniformTuple<Entity> && !PublicNativeUniformTuple<SpriteEntity>);
static_assert(requires(Entity& entity, SpriteEntity& sprite) {
    entity.SetUniforms(std::map<std::string, Math::Mat4>{});
    sprite.SetUniforms(std::map<std::string, Math::Vec4f>{});
});

#include "Managers/ShapeManager.h"
#include "Scene/SkyBoxEntity.h"
static_assert(std::is_same_v<decltype(Manager::ShapeManager::FindShape(std::string{})),
    std::expected<Geometry*, PlatformError>>);
static_assert(std::is_same_v<decltype(Manager::ShapeManager::RetireShape(std::string{})), PlatformResult>);
static_assert(requires(const SkyBoxComponent& desc) { SkyBoxEntity::Create(desc); });

static_assert(std::is_same_v<decltype(Component::DebugKDTreeVisualizer::Create()), std::expected<Component::DebugKDTreeVisualizer, PlatformError>>);
static_assert(std::is_same_v<decltype(Component::DebugAABBBoundingBoxMeshComponent::Create()), std::expected<Component::DebugAABBBoundingBoxMeshComponent, PlatformError>>);
static_assert(!std::is_default_constructible_v<Component::DebugKDTreeVisualizer> && !std::is_default_constructible_v<Component::DebugAABBBoundingBoxMeshComponent>);

#include "Core/GEngine.h"
static_assert(std::is_same_v<decltype(EngineContext::TryCurrent()), std::expected<EngineContext*, PlatformError>>);
static_assert(std::is_same_v<decltype(std::declval<EngineContext&>().Assets()), std::expected<Manager::AssetsManager*, PlatformError>>);
static_assert(std::is_same_v<decltype(std::declval<EngineContext&>().Shapes()), std::expected<Manager::ShapeManager*, PlatformError>>);
static_assert(std::is_same_v<decltype(std::declval<EngineContext&>().AssetPublications()), std::expected<Asset::AssetPublication*, PlatformError>>);
static_assert(std::is_same_v<decltype(Manager::ShapeManager::GetShape("Box")), Manager::ShapeManager::LookupResult>);
static_assert(std::is_same_v<decltype(UnRegisterShape(Box)), PlatformResult>);
