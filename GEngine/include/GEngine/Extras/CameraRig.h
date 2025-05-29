#pragma once
#include "Core/Actor.h"
#include"Core/TimeStep.h"


namespace GEngine
{
    struct RigParams
    {
        float m_UnitsPerSecond{};
        float m_DegreesPerSecond{};
        float m_YawSpeed{};
        float m_PitchSpeed{};
        float m_CurrentPitch{};
        float m_CurrentYaw{};

     /*   float ConvertRadianToDegree(float radian)
        {
            return 180.f / Math::Pi * radian;
        }

        float ConvertRadianToDegree(float radian)
        {
            return 180.f / Math::Pi * radian;
            Math::ToRadians()
        }*/
        
    };


    class CameraRig: public Actor
    {

    public:
        CameraRig(bool bAttachToControlledActor = false, float units_per_seconds = 30.0f, float degree_per_seconds = 40.0f);
       // ~CameraRig()override { if(m_RigAttachment)delete m_RigAttachment; }
        void Attach(Actor* child);
        void SetYawSpeed(float YawSpeed);
        void SetPitchSpeed(float PitchSpeed);
        void Reset();
        void Update(Timestep ts) override;

        RigParams& GetRigParam() { return m_RigParams; }


    private:
        Actor* m_RigAttachment{};
 
        RigParams m_RigParams;
    };
}
