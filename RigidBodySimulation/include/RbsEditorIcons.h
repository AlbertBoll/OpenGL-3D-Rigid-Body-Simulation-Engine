#pragma once
#include "RbsEditorWidgets.h"
#include "UI/UICore.h"

namespace Rbs::Editor
{
    enum class Icon { Select, Translate, Rotate, Scale, Play, Pause, Reset, Options,
                      Camera, Light, Rigidbody, Environment, Entity, Sun, Spot, Eye, EyeOff, SpotA, SpotC, Count };
    inline constexpr float IconPixels = 20.f, IconHitPixels = 32.f;
    inline constexpr int AtlasPixels[]{20,25,30,40};
    inline constexpr int BillboardPixels[]{44,55,66,88,110,132,176};

    inline int IconAtlasIndex()
    { return Scale>=1.9f?3:Scale>=1.4f?2:Scale>=1.15f?1:0; }
    inline void DrawIcon(Icon icon,GEngine::Asset::Texture* atlas,int pixels,ImVec2 position,
                         float size,ImVec4 tint)
    {
        const float cell=float(pixels+4),width=cell*int(Icon::Count),x=int(icon)*cell+2;
        GEngine::UI::DrawImage(atlas,position,{position.x+size,position.y+size},
            {x/width,1.f-2.f/cell},{(x+pixels)/width,2.f/cell},tint);
    }

    // The atlas is owned by AssetsManager; the caller retains only its observer.
    // One reusable hit target/state implementation for every Viewport group.
    inline bool IconButton(const char* id, Icon icon, GEngine::Asset::Texture* atlas,
                           int pixels, bool selected, bool enabled, const char* tooltip)
    {
        const auto position = ImGui::GetCursorScreenPos();
        const auto hit = Px(IconHitPixels), glyph = Px(IconPixels);
        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? Color(0x284B70) : Color(0x242930,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(0x39434F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(0x315B85));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Px(4));
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::Button("##Icon", {hit, hit});
        const ImVec2 minimum{std::round(position.x+(hit-glyph)*.5f),std::round(position.y+(hit-glyph)*.5f)};
        DrawIcon(icon,atlas,pixels,minimum,glyph,
            enabled ? (selected ? Color(0xA9D1FF) : Color(0xDCE2EA)) : Color(0x69727E));
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",tooltip);
        ImGui::PopStyleVar();ImGui::PopStyleColor(3);ImGui::PopID();
        return clicked;
    }
}
