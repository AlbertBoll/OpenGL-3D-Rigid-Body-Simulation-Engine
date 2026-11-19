#pragma once
#include "Core/RuntimeAssets.h"
#include "RbsNumericInput.h"
#include "RbsEditorState.h"
#include "RbsSnapSettings.h"
#include "Physics/PhysicsBackend.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <string_view>
#include <limits>
#include <optional>
#ifdef GENGINE_PLATFORM_WINDOWS
#include <Windows.h>
#endif

// RBS presentation only. Physics policy and mutation ownership remain with Scene/World.
namespace Rbs::Editor
{
    inline constexpr float FontSize = 16.f;
    inline constexpr std::array<float, 4> Scales{1.f, 1.25f, 1.5f, 2.f};
    inline std::array<ImFont*, 4> Regular{}, Bold{};
    inline float Scale = 1.f, AppliedScale = -1.f;
    inline bool Active = false;
    inline std::optional<GEngine::PhysicsBackend> SavedBackend;
    inline int ScaleChoice = 0; // Auto, then the four supported raster sizes.
    inline int LayoutVersion = 0;
    // Persisted presentation only; never changes camera, light or shadow state.
    inline int CameraFrustums=1,LightGizmos=1; // Off / Selected / All.
    inline bool SelectThrough=false;
    inline int TransformPivot=0,TransformOrientation=0; // Center / active Pivot / Individual; World / Local.
    inline bool SceneIcons=true,SpotInner=true,SpotOuter=true,SpotRange=true;
    inline int SceneIconPercent=100,SpotArtwork=1;
    inline int ThumbnailWidth = 160; // Preferred logical pixels; actual cells adapt to width.
    inline ImVec4 Color(unsigned rgb, float alpha = 1.f)
    {
        return {float((rgb >> 16) & 255) / 255.f, float((rgb >> 8) & 255) / 255.f,
                float(rgb & 255) / 255.f, alpha};
    }
    inline float Px(float value) { return value * Scale; }

    // Logical ImGui coordinates: the current font/style already include UI scaling.
    // Framebuffer DPI is applied by the renderer, not a second time by this layout.
    struct PilotControlsLayout
    {
        static constexpr const char* Title="Pilot Camera";
        static constexpr const char* Hint="Navigate in the Viewport";
        ImVec2 size{},button{};
        float minimumWidth{};
        bool stacked{};
    };
    inline PilotControlsLayout MeasurePilotControls(float availableWidth)
    {
        const auto& style=ImGui::GetStyle();
        const float labelWidth=std::max(ImGui::CalcTextSize("Commit Pilot").x,ImGui::CalcTextSize("Cancel Pilot").x);
        const float buttonWidth=std::ceil(labelWidth+2.f*style.FramePadding.x);
        const float minimum=std::ceil(std::max(buttonWidth,ImGui::CalcTextSize(PilotControlsLayout::Title).x));
        const float rowWidth=2.f*buttonWidth+style.ItemSpacing.x;
        const float preferred=std::max(rowWidth,ImGui::CalcTextSize(PilotControlsLayout::Hint).x);
        PilotControlsLayout layout;
        layout.minimumWidth=minimum;
        layout.size.x=std::floor(std::min(availableWidth,preferred));
        layout.stacked=layout.size.x<rowWidth;
        layout.button={layout.stacked?layout.size.x:std::floor((layout.size.x-style.ItemSpacing.x)*.5f),ImGui::GetFrameHeight()};
        const auto hint=ImGui::CalcTextSize(PilotControlsLayout::Hint,nullptr,false,std::max(1.f,layout.size.x));
        layout.size.y=std::ceil(ImGui::GetTextLineHeight()+hint.y+2.f*style.ItemSpacing.y+
            layout.button.y*(layout.stacked?2.f:1.f)+(layout.stacked?style.ItemSpacing.y:0.f));
        return layout;
    }

    inline void Initialize()
    {
        Numeric::Reset();SelectThrough=false;TransformPivot=TransformOrientation=0;Snap={};SavedBackend.reset();InstallEntityPreferences();SceneIconPercent=100;SpotArtwork=1;
        AppliedScale = -1.f;
        ScaleChoice = 0; LayoutVersion = 0; CameraFrustums=LightGizmos=1; SceneIcons=SpotInner=SpotOuter=SpotRange=true;
        auto regular = GEngine::RuntimeAssets::TryFile("Fonts/OpenSans-Regular.ttf");
        auto bold = GEngine::RuntimeAssets::TryFile("Fonts/OpenSans-Bold.ttf");
        auto& io = ImGui::GetIO();
        for (std::size_t i = 0; i < Scales.size(); ++i)
        {
            Regular[i] = regular ? io.Fonts->AddFontFromFileTTF(regular->c_str(), FontSize * Scales[i]) : io.FontDefault;
            Bold[i] = bold ? io.Fonts->AddFontFromFileTTF(bold->c_str(), FontSize * Scales[i]) : Regular[i];
        }
        io.FontDefault = Regular[0];
        ImGuiSettingsHandler settings;
        settings.TypeName = "RbsEditor";
        settings.TypeHash = ImHashStr(settings.TypeName);
        settings.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void*
        { return &ScaleChoice; };
        settings.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
        {
            int choice = 0;
            const std::string_view text(line);
            if(text=="Backend=native"){SavedBackend=GEngine::PhysicsBackend::Native;return;}
            if(text=="Backend=jolt"){SavedBackend=GEngine::PhysicsBackend::Jolt;return;}
            for(const auto& pref:std::array<std::pair<std::string_view,bool*>,5>{{{"GridVisible=",&Snap.visible},{"GridSnap=",&Snap.grid},{"SurfaceSnap=",&Snap.surface},{"AlignSurface=",&Snap.align},{"ResolvePenetration=",&Snap.resolve}}})
                if(text.starts_with(pref.first)) {
                    const auto parsed=std::from_chars(text.data()+pref.first.size(),text.data()+text.size(),choice);
                    if(parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && choice>=0 && choice<=1)*pref.second=choice!=0;
                    return;
                }
            for(const auto& pref:std::array<std::pair<std::string_view,float*>,3>{{{"SnapIncrement=",&Snap.increment},{"SurfaceOffset=",&Snap.offset},{"SnapDistance=",&Snap.distance}}})
                if(text.starts_with(pref.first)) {
                    float value{};const auto parsed=std::from_chars(text.data()+pref.first.size(),text.data()+text.size(),value);
                    if(parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && std::isfinite(value) && (pref.second==&Snap.offset || value>0))*pref.second=value;
                    return;
                }

            for(const auto& preference:std::array<std::pair<std::string_view,int*>,2>{{{"CameraFrustums=",&CameraFrustums},{"LightGizmos=",&LightGizmos}}})
                if(text.starts_with(preference.first)) {
                    const auto result=std::from_chars(text.data()+preference.first.size(),text.data()+text.size(),choice);
                    if(result.ec==std::errc{} && result.ptr==text.data()+text.size() && choice>=0 && choice<=2)*preference.second=choice;
                    return;
                }
            for(const auto& preference:std::array<std::pair<std::string_view,bool*>,4>{{{"SceneIcons=",&SceneIcons},{"SpotInner=",&SpotInner},{"SpotOuter=",&SpotOuter},{"SpotRange=",&SpotRange}}})
                if(text.starts_with(preference.first)) {
                    const auto result=std::from_chars(text.data()+preference.first.size(),text.data()+text.size(),choice);
                    if(result.ec==std::errc{} && result.ptr==text.data()+text.size() && choice>=0 && choice<=1)*preference.second=choice!=0;
                    return;
                }
            if(text.starts_with("TransformPivot=") || text.starts_with("TransformOrientation=")) {
                const auto split=text.find('=');const auto value=std::from_chars(text.data()+split+1,text.data()+text.size(),choice);
                const bool pivot=text.starts_with("TransformPivot=");
                if(value.ec==std::errc{} && value.ptr==text.data()+text.size() && choice>=0 && choice<=(pivot?2:1))
                    (pivot?TransformPivot:TransformOrientation)=choice;
                return;
            }
            if(text=="SelectThrough=1"){SelectThrough=true;return;}
            if(text=="SelectThrough=0"){SelectThrough=false;return;}
            if(text.starts_with("SceneIconPercent=") || text.starts_with("SpotArtwork=")) {
                const auto split=text.find('=');const auto result=std::from_chars(text.data()+split+1,text.data()+text.size(),choice);
                if(result.ec==std::errc{} && result.ptr==text.data()+text.size()) {
                    if(text.starts_with("SceneIconPercent=") && choice>=75 && choice<=200)SceneIconPercent=choice;
                    if(text.starts_with("SpotArtwork=") && choice>=0 && choice<=2)SpotArtwork=choice;
                }return;
            }
            if(text.starts_with("LayoutVersion="))
            {
                const auto result=std::from_chars(text.data()+14,text.data()+text.size(),choice);
                if(result.ec==std::errc{} && result.ptr==text.data()+text.size() && choice>=0)LayoutVersion=choice;
                return;
            }
            if(text.starts_with("ThumbnailWidth="))
            {
                const auto result=std::from_chars(text.data()+15,text.data()+text.size(),choice);
                if(result.ec==std::errc{} && result.ptr==text.data()+text.size() && choice>=96 && choice<=320)ThumbnailWidth=choice;
                return;
            }
            if (!text.starts_with("Scale=")) return;
            const auto result = std::from_chars(text.data() + 6, text.data() + text.size(), choice);
            if (result.ec == std::errc{} && result.ptr == text.data() + text.size() && choice >= 0 && choice <= 4)
                ScaleChoice = choice;
        };
        settings.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler*, ImGuiTextBuffer* output)
        { output->appendf("[RbsEditor][Theme]\nSceneIconPercent=%d\nSpotArtwork=%d\nScale=%d\nThumbnailWidth=%d\nLayoutVersion=%d\nCameraFrustums=%d\nLightGizmos=%d\nSceneIcons=%d\nSpotInner=%d\nSpotOuter=%d\nSpotRange=%d\n\n", SceneIconPercent, SpotArtwork, ScaleChoice, ThumbnailWidth, LayoutVersion,CameraFrustums,LightGizmos,SceneIcons,SpotInner,SpotOuter,SpotRange);
          output->appendf("SelectThrough=%d\nTransformPivot=%d\nTransformOrientation=%d\n\n",SelectThrough,TransformPivot,TransformOrientation);
          output->appendf("[RbsEditor][Snap]\nGridVisible=%d\nGridSnap=%d\nSurfaceSnap=%d\nAlignSurface=%d\nResolvePenetration=%d\nSnapIncrement=%.9g\nSurfaceOffset=%.9g\nSnapDistance=%.9g\n\n",Snap.visible,Snap.grid,Snap.surface,Snap.align,Snap.resolve,Snap.increment,Snap.offset,Snap.distance);
          if(SavedBackend)output->appendf("[RbsEditor][Backend]\nBackend=%s\n\n",
              *SavedBackend==GEngine::PhysicsBackend::Jolt?"jolt":"native"); };
        ImGui::AddSettingsHandler(&settings);
    }

    inline void BeginFrame()
    {
        Active = true;
        float dpi = ImGui::GetMainViewport()->DpiScale;
#ifdef GENGINE_PLATFORM_WINDOWS
        if (auto hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw))
            dpi = std::max(dpi, float(GetDpiForWindow(hwnd)) / 96.f);
#endif
        const float requested = ScaleChoice ? Scales[ScaleChoice - 1] : std::max(1.f, dpi);
        std::size_t index = 0;
        while (index + 1 < Scales.size() && Scales[index] < requested) ++index;
        Scale = Scales[index];
        if (AppliedScale != Scale)
        {
            // Recreate the unscaled baseline; never accumulate rounding/scaling error.
            ImGuiStyle style;
            ImGui::StyleColorsDark(&style);
            style.WindowPadding = {10, 10};
            style.FramePadding = {6, 5};
            style.ItemSpacing = {8, 6};
            style.ItemInnerSpacing = {4, 4};
            style.WindowRounding = 0;
            style.ChildRounding = 3;
            style.FrameRounding = 3;
            style.PopupRounding = 3;
            style.TabRounding = 3;
            style.ScrollbarSize = 12;
            style.IndentSpacing = 14;
            style.WindowMinSize = {180,100};
            style.DisabledAlpha = .5f;
            auto* c = style.Colors;
            c[ImGuiCol_Text] = Color(0xE3E6EB);
            c[ImGuiCol_TextDisabled] = Color(0x737C8A);
            c[ImGuiCol_WindowBg] = Color(0x202329);
            c[ImGuiCol_ChildBg] = Color(0x202329);
            c[ImGuiCol_PopupBg] = Color(0x202329);
            c[ImGuiCol_Border] = Color(0x353B46);
            c[ImGuiCol_FrameBg] = Color(0x14171C);
            c[ImGuiCol_FrameBgHovered] = Color(0x354253);
            c[ImGuiCol_FrameBgActive] = Color(0x345D87);
            c[ImGuiCol_Button] = Color(0x303641);
            c[ImGuiCol_ButtonHovered] = Color(0x43566F);
            c[ImGuiCol_ButtonActive] = Color(0x345D87);
            c[ImGuiCol_Header] = Color(0x2C323C);
            c[ImGuiCol_HeaderHovered] = Color(0x354253);
            c[ImGuiCol_HeaderActive] = Color(0x345D87);
            c[ImGuiCol_Tab] = Color(0x191C22);
            c[ImGuiCol_TabActive] = Color(0x354253);
            c[ImGuiCol_TabUnfocused] = Color(0x191C22);
            c[ImGuiCol_TabUnfocusedActive] = Color(0x2C3543);
            c[ImGuiCol_TabHovered] = Color(0x43566F);
            c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = Color(0x15171C);
            c[ImGuiCol_MenuBarBg] = Color(0x15171C);
            c[ImGuiCol_Separator] = Color(0x353B46);
            c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = Color(0x7EA9D8);
            c[ImGuiCol_DockingPreview] = Color(0x345D87, .7f);
            style.ScaleAllSizes(Scale);
            ImGui::GetStyle() = style;
            AppliedScale = Scale;
        }
        ImGui::PushFont(Regular[index]);
    }
    inline void EndFrame() { ImGui::PopFont(); Active = false; }
    inline void Tooltip(const char* text)
    {
        if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(Px(360));
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    inline bool Component(const char* label, bool open = true, const char* stateId = nullptr)
    {
        std::size_t index = 0;
        while (index + 1 < Scales.size() && Scales[index] < Scale) ++index;
        ImGui::PushFont(Bold[index]);
        const auto flags = open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
        // Presentation renames retain stored header state and do not scope child IDs.
        const bool visible = stateId ? ImGui::TreeNodeEx(stateId,
            flags | ImGuiTreeNodeFlags_CollapsingHeader, "%s", label) : ImGui::CollapsingHeader(label, flags);
        ImGui::PopFont();
        return visible;
    }

    class PropertyRow
    {
        bool table;
    public:
        explicit PropertyRow(const char* label, const char* tip = nullptr)
        {
            ImGui::PushID(label);
            const float width = ImGui::GetContentRegionAvail().x;
            table = width >= Px(360) && ImGui::BeginTable("row", 2, ImGuiTableFlags_SizingStretchProp);
            if (table)
            {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, std::min(Px(124), width * .29f));
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
            }
            ImGui::TextWrapped("%.*s",int(ImGui::FindRenderedTextEnd(label)-label),label);
            Tooltip(tip);
            if (table) ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
        }
        ~PropertyRow()
        {
            if (table) ImGui::EndTable();
            ImGui::PopID();
        }
    };

    template<class T,class Validator=Numeric::Accept>
    bool Number(const char* label, T& value, float speed = .01f,
                const char* tip = "Drag to edit; Ctrl+click to type. Enter validates; Esc cancels.",
                Numeric::Bounds<T> bounds={},Validator validate={})
    {
        PropertyRow row(label, tip);
        return Numeric::Scalar("##value",value,speed,bounds,validate);
    }

    template<class Validator=Numeric::Accept,class AxisObserver=std::nullptr_t>
    bool XYZ(const char* label, float* values, float reset, float speed = .05f,
             const char* tip = "Drag or Ctrl+click a value to edit. Click an axis to reset only that axis.",
             Numeric::Bounds<float> bounds={},Validator validate={},const char* const* axisLabels=nullptr,
             AxisObserver observeAxis=nullptr,const float* axisDefaults=nullptr)
    {
        PropertyRow row(label, tip);
        bool changed = false;
        const char* error=nullptr;
        const float width = ImGui::GetContentRegionAvail().x;
        const bool stacked = width < Px(225);
        const float button = ImGui::GetFrameHeight();
        const float cell = stacked ? width : (width - Px(8)) / 3.f;
        constexpr unsigned colors[]{0xB6454B, 0x438653, 0x3D70AD};
        constexpr const char* xyz[]{"X", "Y", "Z"};
        const char* const* axes=axisLabels?axisLabels:xyz;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {Px(4), Px(4)});
        for (int axis = 0; axis < 3; ++axis)
        {
            if (axis && !stacked) ImGui::SameLine();
            ImGui::PushID(axis);
            const auto id=ImGui::GetID("##value");
            const float axisReset=axisDefaults?axisDefaults[axis]:reset;
            const auto semantic=[&](float proposed)->const char*
            {
                std::array<float,3> candidate{values[0],values[1],values[2]};candidate[axis]=proposed;
                return validate(candidate.data());
            };
            ImGui::PushStyleColor(ImGuiCol_Button, Color(colors[axis]));
            auto hover = Color(colors[axis]);
            hover.x = std::min(1.f, hover.x + .12f);
            hover.y = std::min(1.f, hover.y + .12f);
            hover.z = std::min(1.f, hover.z + .12f);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(colors[axis], .7f));
            if (ImGui::Button(axes[axis], {button, button}))
            {
                const char* invalid=Numeric::Validate(axisReset,bounds);
                if(!invalid)invalid=semantic(axisReset);
                if(invalid)Numeric::Reject(id,invalid);
                else {changed|=values[axis]!=axisReset;values[axis]=axisReset;Numeric::ClearError();}
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Reset %s to %.3g (only this axis)", axes[axis], double(axisReset));
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, 0);
            ImGui::SetNextItemWidth(std::max(Px(38), cell - button));
            changed|=Numeric::Scalar("##value",values[axis],speed,bounds,semantic,false,nullptr,false);
            if constexpr (!std::is_same_v<AxisObserver,std::nullptr_t>) observeAxis(axis);
            if(Numeric::Error(id))error=Numeric::Error(id);
            ImGui::PopID();
        }
        ImGui::PopStyleVar();Numeric::Feedback(error);
        return changed;
    }
    template<class AxisObserver=std::nullptr_t>
    bool Color3(const char* label,float* values,AxisObserver observeAxis=nullptr)
    {
        // Keep the picker, but route every numeric channel through the same parser.
        constexpr const char* rgb[]{"R","G","B"};
        bool changed=XYZ(label,values,1.f,.01f,"Linear RGB channels: 0 to 1.",Numeric::Closed(0.f,1.f),Numeric::Accept{},rgb,observeAxis);
        ImGui::PushID(label);
        if(ImGui::ColorButton("Color picker",{values[0],values[1],values[2],1}))ImGui::OpenPopup("picker");
        if(ImGui::BeginPopup("picker"))
        {
            std::array<float,3> proposed{values[0],values[1],values[2]};
            if(ImGui::ColorPicker3("##picker",proposed.data(),ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoOptions))
            {
                bool valid=true;for(float v:proposed)valid&=!Numeric::Validate(v,Numeric::Closed(0.f,1.f));
                if(valid){std::copy(proposed.begin(),proposed.end(),values);changed=true;Numeric::ClearError();}
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();return changed;
    }
    inline void ReadOnly(const char* label, const char* value, const char* tip = nullptr)
    {
        PropertyRow row(label, tip);
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s", value);
        Tooltip(tip);
    }
}
