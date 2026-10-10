#include "gepch.h"
#include "UI/Dockspace.h"
#include "Core/GLContextThread.h"
#include <imgui/imgui.h>

namespace GEngine::UI
{
    PlatformResult BeginDockspaceHost(const DockspaceHostDesc& description)
    {
        GLContextThread::RequireCurrent("dockspace host");
        if (!ImGui::GetCurrentContext() || !description.windowName || !*description.windowName ||
            !description.dockspaceName || !*description.dockspaceName)
        {
            return std::unexpected(
                PlatformError{PlatformErrorCode::UserInterface, "dockspace host",
                              "A UI context and stable nonempty names are required"});
        }
        if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
        {
            return std::unexpected(PlatformError{PlatformErrorCode::UserInterface, "dockspace host",
                                                 "Docking must be enabled by the application"});
        }
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        constexpr auto flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        // A collapsed host must still submit its dockspace to retain docking.
        ImGui::Begin(description.windowName, nullptr, flags);
        ImGui::PopStyleVar(3);
        ImGui::DockSpace(ImGui::GetID(description.dockspaceName), ImVec2(0.0f, 0.0f));
        return {};
    }

    void EndDockspaceHost()
    {
        ImGui::End();
    }
}
