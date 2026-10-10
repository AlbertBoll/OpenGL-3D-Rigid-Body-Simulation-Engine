#pragma once

#include "Events/Event.h"
#include "Material/MaterialTemplate.h"
#include "Renderer/SceneRenderResources.h"
#include "Assets/Textures/AsyncTexture.h"
#include "Scene/_Entity.h"
#include <array>
#include <chrono>
#include <optional>
#include <string>

namespace Rbs
{
    struct Subscriptions
    {
        std::array<GEngine::Subscription, 5> tokens;
        bool checked = false;
    };

    struct AsyncTextureGallery
    {
        GEngine::MaterialHandle clone;
        std::optional<GEngine::Asset::UploadTicket> pending;
        std::string source;
        std::string message = "Choose an image. Last-valid material remains visible while loading.";
        GEngine::Asset::AsyncAssetState state = GEngine::Asset::AsyncAssetState::Ready;
        bool request = false;
        bool cancel = false;
        bool meshTurn = false;
        bool checked = false;
        unsigned frames = 0;
    };

    struct MaterialGallery
    {
        std::array<GEngine::_Entity, 6> uv, triplanar;
        GEngine::_Entity parent;
        GEngine::MaterialHandle uvMaterial, triplanarMaterial;
        int comparison = 0, inspectedGeometry = 0;
        bool animate = false;
        float time = 0;
        bool presentationRecorded = false;
        unsigned validationFrames = 0;
        std::chrono::steady_clock::time_point validationStart{};
        int validationStage = -1, validationSample = -1, recordedSample = -1;
        bool captureReady = false;
    };

    struct ShaderReloadGallery
    {
        GEngine::MaterialHandle material, clone;
        std::optional<GEngine::MaterialShaderDescription> original, edited, invalid, incompatible;
        std::optional<GEngine::ShaderReloadTicket> ticket;
        std::string message = "Ready. Both cubes share a shader; clone brightness is independent.";
        int request = 0;
        unsigned frames = 0;
    };

    // Temporary scene-construction handles and borrowed texture views. Resource
    // ownership stays with SceneRenderResources/EngineContext; no parallel state.
    struct SceneAssets
    {
        GEngine::Asset::MeshHandle smoothSphereGeo, DiamondGeo, boxMesh, authoringMesh;
        GEngine::MaterialHandle sphereMaterial, floorMaterial, wallMaterial, boxMaterial,
            pointMaterial;
        GEngine::Asset::Texture* sphere_albedo = nullptr;
        GEngine::Asset::Texture* sphere_normal = nullptr;
        GEngine::Asset::Texture* sphere_metallic = nullptr;
        GEngine::Asset::Texture* sphere_roughness = nullptr;
        GEngine::Asset::Texture* sphere_ao = nullptr;
        GEngine::Asset::Texture* floor_albedo = nullptr;
        GEngine::Asset::Texture* floor_normal = nullptr;
        GEngine::Asset::Texture* floor_metallic = nullptr;
        GEngine::Asset::Texture* floor_roughness = nullptr;
        GEngine::Asset::Texture* floor_ao = nullptr;
        std::size_t normalPhysicsBodies = 0;
        std::size_t galleryFixtures = 0;
    };
}
