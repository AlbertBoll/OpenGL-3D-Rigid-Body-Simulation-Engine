// Compile-only consumer contract. Never built or launched as a runtime probe.
#include "Core/RenderBaseline.h"
#include "Core/Window.h"
#include <concepts>
#include <type_traits>
using namespace GEngine::RenderBaseline;
static_assert(std::same_as<decltype(Session::Create()), std::expected<Session, Error>>);
static_assert(std::same_as<decltype(std::declval<Session&>().End()), std::expected<bool, Error>>);
static_assert(std::same_as<decltype(std::declval<Session&>().CaptureScene(nullptr)), Result>);
static_assert(!std::is_copy_constructible_v<Session> && std::is_nothrow_move_constructible_v<Session>);
static_assert(std::is_nothrow_destructible_v<Session>);
static_assert(Session::Warmup == 120 && Session::Samples == 240);

#include "Core/RuntimeAssets.h"
#include "Shapes/Terrain.h"
#include "Audio/AudioSystem.h"
#include "Material/LightTextureMaterial.h"
#include "Material/SkyBoxMaterial.h"
#include "Material/TerrainLightMaterial.h"
static_assert(std::same_as<decltype(GEngine::RuntimeAssets::Initialize("RigidBodySimulation")), GEngine::PlatformResult>);
static_assert(std::same_as<decltype(GEngine::RuntimeAssets::TryFile("Images")), std::expected<std::string, GEngine::PlatformError>>);
static_assert(std::same_as<decltype(GEngine::Terrain::Create()), std::expected<std::unique_ptr<GEngine::Terrain>, GEngine::PlatformError>>);
static_assert(!std::is_default_constructible_v<GEngine::Terrain>);
static_assert(std::same_as<decltype(std::declval<GEngine::Audio::AudioSystem&>().Initialize()), GEngine::PlatformResult>);

static_assert(std::same_as<decltype(GEngine::RuntimeAssets::ResolvePath("optional.png")), std::expected<std::string, GEngine::PlatformError>>);

#include <print>
// Compile-only C++23 facility proof; the maintained RBS target links std::println.
void CompileConsoleFacilities() { std::print("{}", 23); std::println("{}", 23); }
