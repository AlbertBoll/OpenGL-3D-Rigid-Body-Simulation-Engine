#pragma once
#include "Scene/_Scene.h"
#include <array>
#include <memory>
#include <optional>
#include <vector>
#include "Scene/EditorTransformBudget.h"

namespace Rbs::Editor
{
    struct TransformBatch
    {
        enum class Kind { Transform, Rename, Visibility } kind=Kind::Transform;
        GEngine::EntityRenderId target;
        std::string beforeName,afterName;
        bool beforeVisible=true,afterVisible=true;
        std::vector<GEngine::EditorTransformEdit> edits = std::vector<GEngine::EditorTransformEdit>(std::size_t(0));
        std::vector<GEngine::Mat4> parentWorld = std::vector<GEngine::Mat4>(std::size_t(0));
        std::size_t count{};
        std::uint64_t generation{};
        void Resize(std::size_t n) {edits.resize(n);parentWorld.resize(n);count=n;}
        void Clear() {edits.clear();parentWorld.clear();count=0;kind=Kind::Transform;target={};beforeName.clear();afterName.clear();}
        std::size_t Bytes() const {return sizeof(*this)+edits.capacity()*sizeof(GEngine::EditorTransformEdit)+parentWorld.capacity()*sizeof(GEngine::Mat4)+beforeName.capacity()+afterName.capacity();}
        std::span<const GEngine::EditorTransformEdit> Span() const {return {edits.data(),count};}
    };
    struct TransformSession
    {
        enum class Action {None,Apply,Cancel,Undo,Redo};
        struct GizmoNumber
        {
            int axis=-1; // Captured X/Y/Z (0..2), or 3 for uniform scale.
            bool editing{}, focus{};
            unsigned fieldId{};
            int lastDrawFrame{};
            std::array<char,128> text{};
            std::optional<float> staged;
            const char* error{};
        } number;
        TransformBatch preview;
        TransformBatch proposal;
        GEngine::PhysicsPlacementSettings placement;
        GEngine::Mat4 proposedPivot{1};
        bool queryPending{}, invalid{}, resumeAfterApply{}, released{}, live{};
        std::vector<GEngine::Mat4> world,parentWorld;
        std::vector<GEngine::PhysicsPlacementPose> poses;
        std::uint64_t previewTick{};
        GEngine::Mat4 pivot{1},origin{1},gizmo{1},dragOrigin{1};
        bool dirty{};
        Action pending{};
        static constexpr std::size_t HistoryCapacity=32;
        std::unique_ptr<std::array<std::unique_ptr<TransformBatch>,HistoryCapacity>> history;
        std::size_t size{},cursor{};
        std::uint64_t generation{};
        std::size_t HistoryBytes() const {std::size_t bytes=0;if(history)for(std::size_t i=0;i<size;++i)bytes+=(*history)[i]->Bytes();return bytes;}
        // Called only after preparation succeeds. Whole oldest commands are evicted.
        void Store(std::unique_ptr<TransformBatch>&& batch) noexcept {
            for(std::size_t i=cursor;i<size;++i)(*history)[i].reset();size=cursor;
            while(cursor && (cursor==HistoryCapacity || HistoryBytes()+batch->Bytes()>GEngine::EditorTransformBudget::HistoryBytes)) {
                std::move(history->begin()+1,history->begin()+size,history->begin());
                (*history)[--size].reset();--cursor;
            }
            (*history)[cursor++]=std::move(batch);size=cursor;
        }
        void Reset(std::uint64_t value)
        {number={};preview.Clear();proposal.Clear();if(history)for(auto& b:*history)b.reset();queryPending=invalid=resumeAfterApply=released=live=false;dirty=false;pending=Action::None;size=cursor=0;generation=value;}
    };
}
