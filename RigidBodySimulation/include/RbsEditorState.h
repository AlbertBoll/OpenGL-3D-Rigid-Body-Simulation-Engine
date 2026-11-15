#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <map>
#include <string>
#include <charconv>
#include <cstdint>

namespace Rbs::Editor
{
    // Assigned at explicit RBS preset construction sites; never a runtime handle
    // or mutable display name. Not in AllComponents: runtime duplicates are ephemeral.
    inline constexpr unsigned SphereLatticeSphereRole=60; // Authored cohort after prototype duplication.
    struct PresetIdentity {std::uint64_t key{};std::string signature;};
    struct SavedEntity { std::string name,signature; bool hidden{},cameraOpen{}; };
    using PreferenceKey=std::pair<unsigned,std::uint64_t>;
    inline std::map<PreferenceKey,SavedEntity> EntityPreferences;
    inline std::string HexName(std::string_view text)
    {
        constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(text.size()*2);
        for(unsigned char c:text){out+=digits[c>>4];out+=digits[c&15];}return out;
    }
    inline bool DecodeName(std::string_view text,std::string& result)
    {
        if(text.size()%2)return false;std::string decoded;decoded.reserve(text.size()/2);
        for(std::size_t i=0;i<text.size();i+=2){unsigned value{};auto r=std::from_chars(text.data()+i,text.data()+i+2,value,16);
            if(r.ec!=std::errc{} || r.ptr!=text.data()+i+2 || value<32 || value==127)return false;decoded+=char(value);}
        result=std::move(decoded);return true;
    }
    inline void InstallEntityPreferences()
    {
        EntityPreferences.clear();ImGuiSettingsHandler settings;
        settings.TypeName="RbsEntity";settings.TypeHash=ImHashStr(settings.TypeName);
        settings.ReadOpenFn=[](ImGuiContext*,ImGuiSettingsHandler*,const char* section)->void* {
            std::string_view s(section);const auto split=s.find(':');if(split==s.npos)return nullptr;
            unsigned scene{};std::uint64_t key{};
            auto a=std::from_chars(s.data(),s.data()+split,scene),b=std::from_chars(s.data()+split+1,s.data()+s.size(),key,16);
            if(a.ec!=std::errc{}||a.ptr!=s.data()+split||b.ec!=std::errc{}||b.ptr!=s.data()+s.size())return nullptr;
            return &EntityPreferences[{scene,key}];
        };
        settings.ReadLineFn=[](ImGuiContext*,ImGuiSettingsHandler*,void* entry,const char* line) {
            auto& value=*static_cast<SavedEntity*>(entry);const std::string_view s(line);
            if(s.starts_with("Name="))DecodeName(s.substr(5),value.name);
            else if(s.starts_with("Signature="))DecodeName(s.substr(10),value.signature);
            else if(s=="Hidden=1")value.hidden=true;else if(s=="Hidden=0")value.hidden=false;
            else if(s=="CameraOpen=1")value.cameraOpen=true;else if(s=="CameraOpen=0")value.cameraOpen=false;
        };
        settings.WriteAllFn=[](ImGuiContext*,ImGuiSettingsHandler*,ImGuiTextBuffer* out) {
            for(const auto& [key,value]:EntityPreferences) {
                if(value.name.empty()&&!value.hidden&&!value.cameraOpen)continue;
                const auto encoded=HexName(value.name);
                const auto signature=HexName(value.signature);
                out->appendf("[RbsEntity][%u:%016llx]\nName=%s\nSignature=%s\nHidden=%d\nCameraOpen=%d\n\n",key.first,
                    static_cast<unsigned long long>(key.second),encoded.c_str(),signature.c_str(),value.hidden,value.cameraOpen);
            }
        };
        ImGui::AddSettingsHandler(&settings);
    }
}
