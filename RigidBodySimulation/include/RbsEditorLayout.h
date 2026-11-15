#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <cmath>

namespace Rbs::Editor
{
    inline constexpr int CurrentLayoutVersion=4;
    // Recognize only the shipped pre-contract default, not merely an Inspector
    // on the right. IDs, topology, role bindings AND split ratios must agree.
    inline bool IsRecognizedLegacyDefault(ImGuiID dock)
    {
        const auto* root=ImGui::DockBuilderGetNode(dock);
        const auto* top=ImGui::DockBuilderGetNode(1);
        const auto* bottom=ImGui::DockBuilderGetNode(2);
        const auto* left=ImGui::DockBuilderGetNode(3);
        const auto* rest=ImGui::DockBuilderGetNode(4);
        const auto* view=ImGui::DockBuilderGetNode(5);
        const auto* right=ImGui::DockBuilderGetNode(6);
        if(!root || !top || !bottom || !left || !rest || !view || !right)return false;
        if(root->SplitAxis!=ImGuiAxis_Y || top->SplitAxis!=ImGuiAxis_X || rest->SplitAxis!=ImGuiAxis_X ||
            root->ChildNodes[0]!=top || root->ChildNodes[1]!=bottom || top->ChildNodes[0]!=left ||
            top->ChildNodes[1]!=rest || rest->ChildNodes[0]!=view || rest->ChildNodes[1]!=right ||
            !left->IsLeafNode() || !view->IsLeafNode() || !right->IsLeafNode() || !bottom->IsLeafNode())return false;
        const auto bound=[](const char* name,ImGuiID node)
        {const auto* s=ImGui::FindWindowSettings(ImHashStr(name));return s && s->DockId==node;};
        if(!bound("Hierarchy",3) || !bound("Viewport",5) ||
            !(bound("Inspector",6)||bound("Properties",6)) ||
            !(bound("Console / Diagnostics",2)||bound("Content Browser / Diagnostics",2)))return false;
        // Original 18% left / 22% right / 23% bottom at 1280x720.
        // Use SizeRef: actual node sizes may already reflect a resized monitor.
        const auto close=[](float n,float d,float expected)
        {return d>0 && std::abs(n/d-expected)<.002f;};
        return close(left->SizeRef.x,top->SizeRef.x,.18f) &&
            close(right->SizeRef.x,top->SizeRef.x,.22f) &&
            close(bottom->SizeRef.y,top->SizeRef.y+bottom->SizeRef.y+2.f,.23f);
    }
}
