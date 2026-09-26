#include "Renderer/FrameScheduler.h"
#include "Core/Renderer.h"
#include "Core/Scene.h"
#include "RayTracing.h"
#include "EntryPoint.h"
#include "Core/Window.h"
#include "UI/FramebufferImage.h"
#include <external/imgui/imgui.h>
#include <Core/Log.h>
#include "Camera/PerspectiveCamera.h"
#include "Core/RandomGenerator.h"
#include <Core/Timer.h>
#include <format>

namespace GEngine
{
	RayTracingAPP::RayTracingAPP() : 
		BaseApp(), m_Camera(45.0f, 0.1f, 100.f)
	{

		MaterialInfo& pinkMaterial = m_RayScene.Materials.emplace_back();
		pinkMaterial.Albedo = { 1.f, 0.f, 1.f };
		pinkMaterial.Roughness = 0.0f;

		MaterialInfo& blueMaterial = m_RayScene.Materials.emplace_back();
		blueMaterial.Albedo = { .2f, .3f, 1.f };
		blueMaterial.Roughness = 0.1f;
		{
			Sphere sphere;
			sphere.Position = { 0.f, 0.f, 0.f };
			sphere.Radius = 1.f;
			sphere.MaterialIndex = 0;
			m_RayScene.Spheres.push_back(sphere);

		}

		{

			Sphere sphere;
			sphere.Position = { 0.f, -101.0f, 0.f };
			sphere.Radius = 100.f;
			sphere.MaterialIndex = 1;
			m_RayScene.Spheres.push_back(sphere);
		}

	}

	RayTracingAPP::~RayTracingAPP()
	{

		//if(m_EditorCamera)
			//delete m_EditorCamera;

		//GENGINE_CORE_INFO("RayTracingAPP destructor was called");
	}

	void RayTracingAPP::Update(Timestep ts)
	{
		if (m_Camera.OnUpdate(ts))
			m_Renderer.ResetFrameIndex();
	}

    void RayTracingAPP::Render()
    {
        m_UIFailure.reset();
        RenderContext context{*GetWindow(),*GetWindowManager()};
        context.editorUI={this,[](void* user)->ScheduleResult {
            auto& app=*static_cast<RayTracingAPP*>(user);
            app.OnUIRender();
            if(app.m_UIFailure) return std::unexpected(*app.m_UIFailure);
            return {};
        }};
        if(auto result=FrameScheduler::Render(context);!result) {
            if(m_UIFailure) {
                const auto& error=result.error();
                auto code=std::format("stage={} cause={}",int(error.stage),error.cause.index());
                std::string operation="RayTracingAPP::OnUIRender";
                if(const auto* image=std::get_if<ImageError>(&error.cause)) {
                    code=std::to_string(int(image->code)); operation=image->operation;
                } else if(const auto* platform=std::get_if<PlatformError>(&error.cause)) {
                    code=std::to_string(int(platform->code)); operation=platform->operation;
                }
                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Ray UI",std::move(code),
                    std::move(operation),std::format("viewport={}x{}",m_Width,m_Height),DescribeScheduleError(error)});
            } else { GENGINE_CORE_ERROR("{}",DescribeScheduleError(result.error())); ShutDown(); }
        }
    }

	ApplicationInitializationResult RayTracingAPP::Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)
	{
		using namespace Camera;

		//Initialize BaseApp , 
		if (auto initialized = BaseApp::Initialize(WindowsPropertyList); !initialized) return initialized;

		GENGINE_CORE_INFO("Initialize 3D Renderer...");
		Renderer::Initialize();

		auto width = WindowsPropertyList.begin()->m_Width;
		auto height = WindowsPropertyList.begin()->m_Height;


		Renderer::SetSurfaceSize(width, height);


		GENGINE_CORE_INFO("Initialize Perspective Camera...");
		m_EditorCamera = new PerspectiveCamera(
			45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 10000.f);
		m_EditorCamera->SetTag("Editor Camera");
		m_EditorCamera->SetActive(true);

		m_Renderer.SetSphereColor(Vec3f{ 1.f, 0.f, 1.f });
	
	    return {};
	}

	ApplicationInitializationResult RayTracingAPP::Initialize(const WindowProperties& prop)
	{
	    return Initialize(std::initializer_list<WindowProperties>{prop});
	}

	void RayTracingAPP::ProcessInput(Timestep ts)
	{
		BaseApp::ProcessInput(ts);
	}

	void RayTracingAPP::OnUIRender()
	{
		static bool show = true;
		//ImGui::ShowDemoWindow(&show);

		static bool opt_fullscreen = true;
		static bool opt_padding = false;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		//// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		//// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}
		else
		{
			dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
		}

		//// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		//// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		//// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		//// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
		//// all active windows docked into it will lose their parent and become undocked.
		//// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
		//// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		if (!opt_padding)
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("DockSpace Demo", &show, window_flags);
		if (!opt_padding)
			ImGui::PopStyleVar();

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		//// Submit the DockSpace
		ImGuiIO& io = ImGui::GetIO();

		auto& style = ImGui::GetStyle();
		float winMinSize = style.WindowMinSize.x;
		style.WindowMinSize.x = 370.f;
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		}

		style.WindowMinSize.x = winMinSize;

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{

				if (ImGui::MenuItem("Exit"))
				{
					ShutDown();
				}

				ImGui::EndMenu();
			}


			ImGui::EndMenuBar();
		}

		ImGui::End();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.f, 0.f });

		ImGui::Begin("Setting");

		ImGui::Text("Render: %.3fms", m_LastRenderTime);

		ImGui::Separator();
		ImGui::SliderInt("Threads", (int*)(&m_Renderer.GetNumOfThread()), 1, 16);
		ImGui::Separator();
		ImGui::Checkbox("Accumulate", &m_Renderer.GetSettings().Acculmate);
		ImGui::Separator();
		if (ImGui::Button("Reset"))
		{
			m_Renderer.ResetFrameIndex();
		}
		ImGui::Separator();
		if (ImGui::SliderInt("Bounces", (int*)(&m_Renderer.GetBounces()), 1, 20))
		{
			m_Renderer.ResetFrameIndex();
		}
		ImGui::End();

		auto num_of_spheres = m_RayScene.Spheres.size();
		auto num_of_materials = m_RayScene.Materials.size();

		ImGui::Begin("Scene");
		for (auto i = 0; i < num_of_spheres; i++)
		{
			Sphere& sphere = m_RayScene.Spheres[i];

			ImGui::PushID(i);
			ImGui::DragFloat3("Position", glm::value_ptr(sphere.Position), 0.1f);
			ImGui::DragFloat("Radius", &sphere.Radius, 0.1f);
			ImGui::DragInt("Material", &sphere.MaterialIndex, 1.0f, 0, (int)num_of_materials - 1);

			ImGui::PopID();
			ImGui::Separator();
		}

		

		for (auto i = 0; i < num_of_materials; i++)
		{
			MaterialInfo& material = m_RayScene.Materials[i];
			ImGui::PushID(i);
			if (ImGui::ColorEdit3("Albedo", glm::value_ptr(material.Albedo)) || 
				ImGui::DragFloat("Roughness", &material.Roughness, 0.01f, 0.f, 1.f) || 
				ImGui::DragFloat("Metallic", &material.Metallic, 0.01f, 0.f, 1.f))
			{
				m_Renderer.ResetFrameIndex();
			}
			
			ImGui::Separator();
			ImGui::PopID();
		}

		ImGui::End();


		const bool viewportVisible = ImGui::Begin("Viewport");
		const auto extent = ImGui::GetContentRegionAvail();
		const bool hasArea = viewportVisible && extent.x >= 1.f && extent.y >= 1.f;
		auto scale = hasArea ? UI::CurrentViewportFramebufferScale() : std::expected<FramebufferScale, PlatformError>(FramebufferScale{});
        if (scale)
        {
            auto sized = SetEditorViewport(hasArea ? EditorViewportLogicalSize{extent.x, extent.y} : EditorViewportLogicalSize{}, *scale);
            if (!sized) { m_UIFailure=ScheduleError{FrameStage::Pass,sized.error()}; ImGui::End(); ImGui::PopStyleVar(); return; }
        }
        else { m_UIFailure=ScheduleError{FrameStage::Pass,scale.error()}; ImGui::End(); ImGui::PopStyleVar(); return; }
        const auto pixels = GetEditorViewportPixelSize();
        m_Width = pixels.Width; m_Height = pixels.Height;
        if (hasArea) {
            if (auto generated=GenerateImage(); !generated) {
                m_UIFailure=ScheduleError{FrameStage::Pass,generated.error()};
                ImGui::End(); ImGui::PopStyleVar(); return;
            }
        }

		auto& image = m_Renderer.GetFinalImage();
        if(hasArea && image) {
            if(auto presented=UI::RasterImage(*image,extent.x,extent.y);!presented) {
                m_UIFailure=ScheduleError{FrameStage::Pass,presented.error()};
                ImGui::End();ImGui::PopStyleVar();return;
            }
        }

		ImGui::End();

		ImGui::PopStyleVar();
		//GenerateImage();
	}

	ImageResult RayTracingAPP::GenerateImage()
	{
		Timer timer;
        if(auto resized=m_Renderer.OnResize(m_Width,m_Height);!resized) return std::unexpected(resized.error());
		if (m_Width == 0 || m_Height == 0)
			return {};
		m_Renderer.RenderBegin();
		if (m_Camera.OnResize(m_Width, m_Height))
		{
			m_Renderer.ResetFrameIndex();
		}
        if(auto rendered=m_Renderer.Render(m_RayScene,m_Camera);!rendered) return std::unexpected(rendered.error());
		m_LastRenderTime = timer.ElapsedMilliSeconds();
        return {};
	}


}

BaseApp* CreateApp()
{
	return new RayTracingAPP();
}
