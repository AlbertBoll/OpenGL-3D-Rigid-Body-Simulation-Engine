#pragma once
#include "RbsNumericConstraints.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <array>
#include <cstring>
#include <utility>

namespace Rbs::Editor::Numeric
{
    // One keyboard editor, matching ImGui's single active text item. Fixed storage;
    // no per-frame allocation and no parsing through ImGui's permissive scanf path.
    struct State
    {
        ImGuiContext* context{};
        ImGuiID editing{},editingWindow{},errorId{},errorWindow{};
        int lastSeen{},errorSeen{};
        bool focus{},selectionHandoff{};
        std::array<char,128> text{};
        std::array<unsigned char,8> original{};
        const char* error{};
    };
    inline State state;
    inline void Reset()
    {
        if(state.editing && ImGui::GetCurrentContext()==state.context && state.context->ActiveId==state.editing)
            ImGui::ClearActiveID();
        state={};
    }
    inline bool TakeSelectionHandoff(){return std::exchange(state.selectionHandoff,false);}
    inline void ClearError(){state.error=nullptr;state.errorId=0;}
    inline void Reject(ImGuiID id,const char* reason)
    {state.selectionHandoff=true;state.errorId=id;state.error=reason;state.errorWindow=ImGui::GetCurrentWindow()->ID;state.errorSeen=ImGui::GetFrameCount();}
    inline bool HasError()
    {return (state.editing && state.editingWindow==ImGui::GetCurrentWindow()->ID) || (state.error && state.errorWindow==ImGui::GetCurrentWindow()->ID && state.errorSeen>=ImGui::GetFrameCount()-1);}
    inline const char* Error(ImGuiID id){return state.errorId==id?state.error:nullptr;}
    inline void Feedback(const char* error)
    {
        if(!error)return;
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.94f,.54f,.54f,1));
        ImGui::TextWrapped("%s",error);ImGui::PopStyleColor();
    }
    template<class T> constexpr ImGuiDataType Type()
    {
        if constexpr(std::is_same_v<T,float>)return ImGuiDataType_Float;
        else if constexpr(std::is_same_v<T,double>)return ImGuiDataType_Double;
        else if constexpr(std::is_same_v<T,int>)return ImGuiDataType_S32;
        else {static_assert(std::is_same_v<T,std::uint64_t>);return ImGuiDataType_U64;}
    }
    template<class T> const char* Format()
    {
        if constexpr(std::is_floating_point_v<T>)return "%.6g";
        else if constexpr(std::is_same_v<T,int>)return "%d";
        else return "%llu";
    }
    template<class T,class Validator=Accept>
    bool Scalar(const char* label,T& value,float speed,Bounds<T> bounds={},Validator validate={},
                bool slider=false,const char* format=nullptr,bool feedback=true)
    {
        auto& g=*ImGui::GetCurrentContext();
        if(state.context!=&g){Reset();state.context=&g;}
        const auto id=ImGui::GetID(label);
        const bool disabled=(g.CurrentItemFlags&ImGuiItemFlags_Disabled)!=0;
        if(state.editing && state.lastSeen<g.FrameCount-1)state.editing=0;
        if(state.errorId==id)state.errorSeen=g.FrameCount;
        const auto accept=[&](T proposed)->bool
        {
            const char* error=Validate(proposed,bounds);
            if(!error)error=validate(proposed);
            if(error){Reject(id,error);return false;}
            if(state.errorId==id)ClearError();
            const bool changed=value!=proposed;value=proposed;state.selectionHandoff=true;return changed;
        };
        bool changed=false;
        if(state.editing==id && disabled)state.editing=0;
        if(state.editing==id && std::memcmp(state.original.data(),&value,sizeof(T))!=0)
        {state.editing=0;Reject(id,"Value changed while typing; readback restored.");}
        if(state.editing==id)
        {
            state.lastSeen=g.FrameCount;
            if(state.focus)
            {
                // A drag and its text editor share an ID. Activate InputText itself
                // now so it initializes its buffer, rather than deferring a tab
                // focus request that can leave the old drag active without text state.
                ImGui::ClearActiveID();g.NavActivateInputId=id;state.focus=false;
            }
            const bool escape=ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape),false);
            const bool enter=ImGui::InputText(label,state.text.data(),state.text.size(),
                ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll);
            if(escape){state.selectionHandoff=true;state.editing=0;if(state.errorId==id)ClearError();}
            else if(enter || ImGui::IsItemDeactivated())
            {
                T proposed=value;
                const auto* error=Parse(std::string_view(state.text.data()),proposed,bounds);
                if(error)Reject(id,error);else changed=accept(proposed);
                state.editing=0; // Rejected text never becomes the displayed/previewed value.
                if(g.ActiveId==id)ImGui::ClearActiveID();
            }
        }
        else
        {
            T proposed=value;
            const T low=bounds.Lower(),high=bounds.Upper();
            if(low>high){ImGui::TextDisabled("No valid value in this interval");return false;}
            constexpr auto flags=ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoInput|ImGuiSliderFlags_NoRoundToFormat;
            const bool keyboardInput=g.NavActivateInputId==id;
            ImGui::PushItemFlag(ImGuiItemFlags_Inputable,true);
            const bool edited=slider?ImGui::SliderScalar(label,Type<T>(),&proposed,&low,&high,format?format:Format<T>(),flags):
                ImGui::DragScalar(label,Type<T>(),&proposed,speed,bounds.minimum?&low:nullptr,bounds.maximum?&high:nullptr,format?format:Format<T>(),flags);
            ImGui::PopItemFlag();
            const bool textRequested=!disabled && (keyboardInput ||
                (g.LastItemData.StatusFlags&ImGuiItemStatusFlags_FocusedByTabbing) ||
                (ImGui::IsItemHovered() && ((ImGui::IsMouseClicked(0)&&g.IO.KeyCtrl)||ImGui::IsMouseDoubleClicked(0))));
            if(textRequested)
            {
                state.editing=id;state.editingWindow=ImGui::GetCurrentWindow()->ID;state.lastSeen=g.FrameCount;state.focus=true;
                std::memcpy(state.original.data(),&value,sizeof(T));
                const auto result=std::to_chars(state.text.data(),state.text.data()+state.text.size()-1,value);
                *result.ptr=0;
            }
            else if(edited && !disabled)
            {
                // Also handles a single-valued/interdependent interval and protects
                // against accumulated overshoot. Backend semantic rejection is separate.
                if constexpr(std::is_floating_point_v<T>)
                {if(std::isfinite(proposed))proposed=std::clamp(proposed,low,high);}
                else proposed=std::clamp(proposed,low,high);
                changed=accept(proposed);
            }
        }
        if(const auto* error=Error(id))
        {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),IM_COL32(239,137,137,255),ImGui::GetStyle().FrameRounding);
            if(feedback)Feedback(error);
            else if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",error);
        }
        return changed;
    }
}
