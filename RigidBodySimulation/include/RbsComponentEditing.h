#pragma once
#include "Component/RenderComponents.h"
#include "Camera/CameraSnapshot.h"
#include <cmath>
#include <array>
#include <span>
#include <optional>

namespace Rbs::Editor
{
    // Retain authored cascade distribution while binding it to the actual view.
    // The default view returns the exact original values, including fixture overrides.
    inline std::optional<std::array<float,5>> CameraCascadeSplits(std::span<const float> authored,
        double authoredNear,double authoredFar,double viewNear,double viewFar)
    {
        if(authored.size()!=5 || !std::isfinite(viewNear) || !std::isfinite(viewFar) ||
            viewNear<=0 || viewFar<=viewNear || authoredFar<=authoredNear)return {};
        std::array<float,5> result{};double previous=viewNear;
        for(std::size_t i=0;i<result.size();++i) {
            result[i]=(viewNear==authoredNear && viewFar==authoredFar)?authored[i]:
                float(viewNear+(double(authored[i])-authoredNear)/(authoredFar-authoredNear)*(viewFar-viewNear));
            if(i+1==result.size())result[i]=float(viewFar);
            if(!std::isfinite(result[i]) || result[i]<=previous || result[i]>float(viewFar))return {};
            previous=result[i];
        }
        return result;
    }
    inline const char* ValidateCamera(const GEngine::Component::RenderCameraComponent& camera)
    {
        using Projection=GEngine::Component::CameraProjection;
        if(camera.projection!=Projection::Perspective && camera.projection!=Projection::Orthographic)return "Invalid projection type.";
        if(!std::isfinite(camera.verticalFovRadians)||camera.verticalFovRadians<=.0174533f||camera.verticalFovRadians>=3.124139f)
            return "Vertical FOV must be between 1 and 179 degrees.";
        if(!std::isfinite(camera.orthographicHeight)||camera.orthographicHeight<=0)return "Orthographic height must be positive.";
        if(!std::isfinite(camera.nearPlane)||!std::isfinite(camera.farPlane)||camera.nearPlane<=0||camera.farPlane<=camera.nearPlane)
            return "Clipping planes require 0 < near < far.";
        GEngine::CameraSnapshotDesc desc;
        desc.projection=camera.projection;desc.verticalFovRadians=camera.verticalFovRadians;
        desc.orthographicHeight=camera.orthographicHeight;desc.nearPlane=camera.nearPlane;desc.farPlane=camera.farPlane;
        if(!GEngine::CameraSnapshot::Create(desc))return "Projection is outside the renderer's finite, invertible range.";
        constexpr std::array<float,5> cascades{20,40,100,500,1000};
        if(!CameraCascadeSplits(cascades,double(.1f),1000,camera.nearPlane,camera.farPlane))
            return "Clip interval is too narrow for five distinct renderer shadow slices.";
        return nullptr;
    }
    inline const char* ValidateLight(const GEngine::Component::RenderLightComponent& light,float pointShadowNear=.1f)
    {
        using Kind=GEngine::Component::RenderLightKind;
        if(light.kind!=Kind::Directional && light.kind!=Kind::Point && light.kind!=Kind::Spot)return "Invalid light type.";
        for(float c:light.color)if(!std::isfinite(c)||c<0 || c>1)return "Linear color channels must be between 0 and 1.";
        if(!std::isfinite(light.intensity)||light.intensity<0)return "Intensity must be finite and nonnegative.";
        if(!std::isfinite(light.range)||light.range<=0)return "Light range must be positive.";
        if(light.kind==Kind::Point) {
            if(light.range<=pointShadowNear)return "Point shadow range must exceed the renderer's near clip.";
            if(light.castShadows) {
                GEngine::CameraSnapshotDesc projection;projection.verticalFovRadians=glm::radians(90.);
                projection.nearPlane=pointShadowNear;projection.farPlane=light.range;
                if(!GEngine::CameraSnapshot::Create(projection))return "Point shadow projection must be finite and invertible.";
            }
        }
        if(!std::isfinite(light.innerConeRadians)||!std::isfinite(light.outerConeRadians)||light.innerConeRadians<0 ||
            light.outerConeRadians<=0||light.outerConeRadians>=1.5707963f||light.innerConeRadians>light.outerConeRadians)
            return "Spot angles require 0 <= inner <= outer < 90 degrees.";
        if(light.kind==Kind::Spot)
        {
            if(const auto* error=GEngine::ValidateSpotShadow(light.spotShadow))return error;
            if(light.castShadows && !GEngine::SpotShadowMatrix({0,0,0},{0,0,-1},light.range,light.outerConeRadians))
                return "Spot shadow projection must be finite and invertible.";
        }
        return nullptr;
    }
}
