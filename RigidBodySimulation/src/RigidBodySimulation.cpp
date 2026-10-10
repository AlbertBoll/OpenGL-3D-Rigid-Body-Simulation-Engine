#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Core/RuntimeAssets.h"
#include "Scene/_Scene.h"
#include <format>

#include <new>

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

RigidBodySimulationApp::RigidBodySimulationApp(const Rbs::LaunchConfig& launch)
    : BaseApp(launch.engine), m_Launch(launch)
{
}

RigidBodySimulationApp::~RigidBodySimulationApp()
{
    m_Subscriptions.reset();
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::Subscriptions))
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_APP_RELEASED");
    m_MaterialGallery = {};
    m_ShaderReloadGallery = {};
    m_AsyncTextureGallery = {};
    if (m_AudioSystem)
        m_AudioSystem->Shutdown();
    if (m_SceneResources)
    {
        if (auto current = GetEngineContext().MakeCurrent(); !current)
        {
            ReportPlatformError(current.error());
            std::terminate();
        }
        m_MeshLoads.reset(); // Cancel/join imports before registry or scene retirement.
        m_ActiveScene.reset();
        m_EditorScene.reset();
        m_FrameSubmission.reset();
        m_SceneResources.reset();
    }
#ifdef GENGINE_RBS_MODULARITY_VALIDATION
    Log::GetCoreLogger()->info(
        "PRE_EDITOR_PACKET_A_SHUTDOWN_PASS subscriptions_before_state=true resources_before_context=true");
#endif
}

ApplicationInitializationResult
RigidBodySimulationApp::Initialize(const std::initializer_list<WindowProperties>& properties)
{
    if (auto initialized = InitializeValidationPlatform(properties); !initialized)
        return initialized;
    if (m_Launch.AsyncPerformanceEnabled())
        return {};
    if (auto resources = InitializeResources(); !resources)
        return resources;
    return InitializeScene();
}

ApplicationInitializationResult RigidBodySimulationApp::Initialize(const WindowProperties& prop)
{
    return Initialize(std::initializer_list<WindowProperties>{prop});
}

ApplicationInitializationResult RigidBodySimulationApp::InitializeResources()
{
    m_AudioSystem = CreateScopedPtr<Audio::AudioSystem>();
    if (auto audio = m_AudioSystem->Initialize(); !audio)
        return std::unexpected(audio.error());
    auto resources = SceneRenderResources::Create(GetEngineContext());
    if (!resources)
        return SceneFailure(resources.error());
    m_SceneResources = std::move(*resources);
    auto meshRoot = RuntimeAssets::TryFile("Models");
    if (!meshRoot)
        return std::unexpected(meshRoot.error());
    m_MeshRoot = *meshRoot;
    // Opt-in validation exercises the same action as the File menu.
    m_LoadBarrel = m_Launch.startupMesh == Rbs::StartupMesh::Barrel;
    auto submission = FrameSubmission::Create();
    if (!submission)
    {
        Log::GetCoreLogger()->error("{}", DescribeSubmissionError(submission.error()));
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "frame submission",
                                             DescribeSubmissionError(submission.error())});
    }
    m_FrameSubmission.emplace(std::move(*submission));
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::ConnectApplicationEventHandlers()
{
    auto connections = std::unique_ptr<Rbs::Subscriptions>(new (std::nothrow) Rbs::Subscriptions);
    if (!connections)
        return std::unexpected(
            PlatformError{PlatformErrorCode::Allocation, "RBS subscriptions",
                          "Could not allocate application connection ownership"});
    auto& events = *GetEventManager();
    std::array connected{events.Subscribe(Manager::Event::AppPause,
                                          [this]()
                                          {
                                              m_IsPause = true;
                                          }),
                         events.Subscribe(Manager::Event::AppResume,
                                          [this]()
                                          {
                                              m_IsPause = false;
                                          }),
                         events.Subscribe(Manager::Event::DebugShow,
                                          [this]()
                                          {
                                              m_IsShowDebugBoundingBox = !m_IsShowDebugBoundingBox;
                                          }),
                         events.Subscribe(Manager::Event::ViewportChange,
                                          [this]()
                                          {
                                              m_EditorCamera_.OnViewportViewDirectionChange();
                                          }),
                         events.Subscribe(Manager::Event::MouseScrollWheel,
                                          [this](const MouseScrollWheelParam& wheel)
                                          {
                                              m_EditorCamera_.OnMouseScroll(wheel.Y);
                                          })};
    for (std::size_t i = 0; i < connected.size(); ++i)
    {
        if (!connected[i])
            return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                                                 "RBS subscriptions",
                                                 "Typed subscription rejected"});
        connections->tokens[i] = std::move(*connected[i]);
    }
    m_Subscriptions = std::move(connections);
    return {};
}
