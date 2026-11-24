#pragma once
#include "Physics/PhysicsWorld.h"
#include "Scene/RenderEcs.h"
#include <array>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Rbs::Editor {
    struct ImpulseParameters {
        float magnitude=1;
        GEngine::Vec3f direction{1,0,0},pointOffset{},angular{};
        bool local{},customPoint{},angularEnabled{};
        GEngine::PhysicsImpulseMode mode=GEngine::PhysicsImpulseMode::Impulse;
        const char* Validate() const {
            using namespace GEngine;
            if(!Math::IsFinite(magnitude)||magnitude<0)return "Magnitude must be finite and nonnegative.";
            if(!Math::IsFinite(direction) || (magnitude>0 && direction==Vec3f(0)))return "A linear impulse needs a finite, nonzero direction.";
            if(!Math::IsFinite(pointOffset)||!Math::IsFinite(angular))return "Point and angular vector must be finite.";
            if(mode!=PhysicsImpulseMode::Impulse && mode!=PhysicsImpulseMode::VelocityChange)return "Unknown impulse mode.";
            if(magnitude==0 && (!angularEnabled || angular==Vec3f(0)))return "Choose a nonzero linear or angular action.";
            return nullptr;
        }
        GEngine::PhysicsImpulseCommand Command(GEngine::PhysicsBodyHandle body,const GEngine::PhysicsImpulseMotion& motion) const {
            using namespace GEngine;
            const auto rotate=[&](const Vec3f& v){return local?motion.orientation*v:v;};
            PhysicsImpulseCommand c;c.body=body;c.mode=mode;
            if(magnitude>0)c.linear=rotate(Vec3f(glm::normalize(glm::dvec3(direction))))*magnitude;
            if(angularEnabled)c.angular=rotate(angular);
            if(customPoint)c.point=motion.center+rotate(pointOffset);
            return c;
        }
    };
    struct ImpulseTarget {
        GEngine::EntityRenderId entity;
        GEngine::PhysicsBodyHandle body;
        std::uint64_t key{};
        std::string signature,name;
    };
    struct ImpulseRecord {
        std::uint64_t serial{},generation{},tick{};
        unsigned preset{};
        GEngine::PhysicsBackend backend{};
        ImpulseParameters parameters;
        std::vector<ImpulseTarget> targets=std::vector<ImpulseTarget>(std::size_t(0));
        std::vector<GEngine::PhysicsImpulseResult> results=std::vector<GEngine::PhysicsImpulseResult>(std::size_t(0));
        std::vector<GEngine::PhysicsBodyProperties> properties=std::vector<GEngine::PhysicsBodyProperties>(std::size_t(0));
        std::optional<GEngine::PhysicsJoltSettings> settings;
        std::size_t Bytes() const {
            std::size_t n=sizeof(*this)+targets.capacity()*sizeof(ImpulseTarget)+results.capacity()*sizeof(GEngine::PhysicsImpulseResult)+properties.capacity()*sizeof(GEngine::PhysicsBodyProperties);
            for(const auto& t:targets)n+=t.name.capacity()+t.signature.capacity();return n;
        }
    };
    struct ImpulseEditor {
        ImpulseParameters parameters,savedParameters;
        std::vector<ImpulseTarget> savedTargets;
        std::uint64_t savedGeneration{},nextSerial=1,pendingSelectionRevision{};
        unsigned savedPreset{};
        bool showArrows{},pendingUsesSaved{};
        std::unique_ptr<ImpulseRecord> pending;
        static constexpr std::size_t RecordBytes=8*1024*1024;
        std::array<std::unique_ptr<ImpulseRecord>,8> records{};
        std::size_t recordCount{};
        std::string diagnostic;
        std::size_t Bytes() const {std::size_t n=0;for(const auto& r:records)if(r)n+=r->Bytes();return n;}
        void Store(std::unique_ptr<ImpulseRecord> record) noexcept {
            while(recordCount && (recordCount==records.size() || Bytes()+record->Bytes()>RecordBytes)) {
                std::move(records.begin()+1,records.begin()+recordCount,records.begin());records[--recordCount].reset();
            }
            records[recordCount++]=std::move(record);
        }
    };
}
