#include "../tests/BackendSwitchChecks.h"

#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
namespace Rbs { unsigned MeshHighlightStage(); }
#endif
#include "RigidBodySimulation.h"
#include "../tests/OwnerReviewProbe.h"
#include "RbsEditorWidgets.h"
#include "RbsScenePresetPayload.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include "Physics/PhysicsSystem.h"
#include "Renderer/SceneRenderResources.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <new>

#include "UI/Dockspace.h"
#include "UI/FramebufferImage.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#ifdef GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS
#include "../tests/DistanceJointChecks.h"
#endif
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include "../tests/SandboxChecks.h"
#endif
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#include "../tests/ColliderReplacementCapture.h"
#include "../tests/ColliderReplacementObservation.h"
#endif

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

namespace Rbs
{
    void DistanceChainImpulseControl::Bind(PhysicsBodyHandle target, bool enabled)
    {
        *this = {};
        m_Target = target;
        m_Enabled = enabled;
        m_Status = enabled ? Status::Ready : Status::Disabled;
    }

    bool DistanceChainImpulseControl::Request(const PhysicsWorld& world, float magnitude, Direction direction)
    {
        if (!m_Enabled) { m_Status = Status::Disabled; return false; }
        if (m_Pending) return false; // Never replace a command waiting for a step.
        if (!std::isfinite(magnitude) || magnitude < MinMagnitude || magnitude > MaxMagnitude)
        { m_Status = Status::InvalidMagnitude; return false; }
        Vec3f impulse(0.f);
        switch (direction)
        {
        case Direction::PositiveX: impulse.x = magnitude; break;
        case Direction::NegativeX: impulse.x = -magnitude; break;
        case Direction::PositiveZ: impulse.z = magnitude; break;
        case Direction::NegativeZ: impulse.z = -magnitude; break;
        default: m_Status = Status::InvalidDirection; return false;
        }
        const auto body = world.ReadBody(m_Target);
        if (!body || body->type != BodyType::Dynamic || body->inverseMass <= 0.f)
        { m_Status = Status::InvalidTarget; return false; }
        if (magnitude == 0.f) { m_Status = Status::Zero; return true; }
        m_QueuedImpulse = impulse;
        m_Pending = true;
        m_Status = Status::Queued;
        return true;
    }

    void DistanceChainImpulseControl::Consume(PhysicsWorld& world, bool paused)
    {
        if (!m_Pending || paused) return;
        m_Pending = false; // A rejected/stale command is also consumed exactly once.
        const auto body = world.ReadBody(m_Target);
        if (!body || body->type != BodyType::Dynamic || body->inverseMass <= 0.f)
        { m_Status = Status::InvalidTarget; return; }
        if (!world.ApplyLinearImpulse(m_Target, m_QueuedImpulse))
        { m_Status = Status::Rejected; return; }
        m_LastApplied = m_QueuedImpulse;
        ++m_AppliedCount;
        m_Status = Status::Applied;
    }

#ifdef GENGINE_MESH_HIGHLIGHT_AUDIT
    void AuditHighlightUiCallback();
#endif
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
    namespace ColliderEditorProbe { void Observe(const char*); }
#endif
    static void NoteColliderControl(const char* label)
    {
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
        ColliderEditorProbe::Observe(label);
#endif
    }
#ifdef GENGINE_PHASE24_COLLIDER_VALIDATION
    static std::uint64_t meshColliderFitCalls{};
    std::uint64_t MeshColliderFitCalls() { return meshColliderFitCalls; }
#endif

    // Command-only CPU preparation. Current scale becomes the replacement's
    // explicit unit-scale base; ordinary Physics creation/scaling is unchanged.
    std::expected<CollisionShapeDesc, GeometryAuthoringCode>
    FitMeshCollider(const MeshAsset& mesh, const Vec3f& scale, ShapeType type)
    {
#ifdef GENGINE_PHASE24_COLLIDER_VALIDATION
        ++meshColliderFitCalls;
#endif
        using Error = GeometryAuthoringCode;
        if (!Math::IsFinite(scale)) return std::unexpected(Error::InvalidScale);
        const auto& bounds = mesh.Bounds();
        if (bounds.empty || !mesh.VertexCount()) return std::unexpected(Error::EmptyBounds);
        const auto outward = [](double value) {
            float result = static_cast<float>(value);
            if (double(result) < value)
                result = std::nextafter(result, std::numeric_limits<float>::infinity());
            return result;
        };
        if (type == ShapeType::Box)
        {
            BoxShapeDesc box;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const double a = bounds.minimum[axis] * double(scale[axis]);
                const double b = bounds.maximum[axis] * double(scale[axis]);
                const double lower = (std::min)(a, b), upper = (std::max)(a, b);
                box.center[axis] = static_cast<float>((lower + upper) * .5);
                box.halfExtents[axis] = outward((std::max)(upper - double(box.center[axis]),
                                                                        double(box.center[axis]) - lower));
                if (!std::isfinite(box.center[axis]) || !std::isfinite(box.halfExtents[axis]) ||
                    box.halfExtents[axis] <= 0)
                    return std::unexpected(Error::DegenerateExtent);
            }
            return CollisionShapeDesc{box};
        }
        if (type != ShapeType::Sphere && type != ShapeType::Capsule)
            return std::unexpected(Error::InvalidDimensions);

        const Vec3f magnitude = glm::abs(scale);
        if (bounds.sphereCenter == std::array<double, 3>{} &&
            magnitude.x == magnitude.y && magnitude.x == magnitude.z)
        {
            const float radius = outward(bounds.sphereRadius * double(magnitude.x));
            if (!std::isfinite(radius) || radius <= 0)
                return std::unexpected(Error::InvalidDimensions);
            if (type == ShapeType::Sphere) return CollisionShapeDesc{SphereShapeDesc{radius}};
            const double radialExtent = (std::max)({std::abs(bounds.minimum[0]), std::abs(bounds.maximum[0]),
                                                    std::abs(bounds.minimum[2]), std::abs(bounds.maximum[2])}) * magnitude.x;
            // A spherical mesh needs no cylinder segment. The cached sphere is
            // already conservative; allow only its Float32 outward rounding.
            if (radius <= std::nextafter(static_cast<float>(radialExtent), std::numeric_limits<float>::infinity()))
                return CollisionShapeDesc{CapsuleShapeDesc{radius, 0.f}};
        }

        const auto layout = mesh.Layout();
        const VertexAttribute* position = nullptr;
        for (const auto& attribute : layout.attributes)
            if (attribute.semantic == VertexSemantic::Position) position = &attribute;
        if (!position || position->scalar != VertexScalarFormat::Float32 || position->components != 3 ||
            position->offsetBytes + sizeof(float) * 3 > layout.strideBytes)
            return std::unexpected(Error::UnsupportedLayout);
        const auto vertex = [&](std::size_t index) {
            std::array<float, 3> values;
            std::memcpy(values.data(), mesh.Vertices().data() + index * layout.strideBytes + position->offsetBytes,
                        sizeof(values));
            return glm::dvec3(values[0] * double(scale.x), values[1] * double(scale.y), values[2] * double(scale.z));
        };
        double squaredRadius = 0;
        for (std::size_t index = 0; index < mesh.VertexCount(); ++index)
        {
            const auto p = vertex(index);
            squaredRadius = (std::max)(squaredRadius, type == ShapeType::Sphere ? glm::dot(p, p) : p.x*p.x + p.z*p.z);
        }
        const float radius = outward(std::sqrt(squaredRadius));
        if (!std::isfinite(radius) || radius <= 0) return std::unexpected(Error::InvalidDimensions);
        if (type == ShapeType::Sphere) return CollisionShapeDesc{SphereShapeDesc{radius}};

        // Fixed local-Y axis and existing pivot. For the chosen enclosing radial
        // radius, this is the shortest symmetric segment containing all vertices.
        double halfLength = 0;
        for (std::size_t index = 0; index < mesh.VertexCount(); ++index)
        {
            const auto p = vertex(index);
            const double cap = std::sqrt((std::max)(0.0, double(radius)*radius - p.x*p.x - p.z*p.z));
            halfLength = (std::max)(halfLength, std::abs(p.y) - cap);
        }
        const float length = outward(halfLength);
        if (!std::isfinite(length) || !std::isfinite(radius + length))
            return std::unexpected(Error::InvalidDimensions);
        return CollisionShapeDesc{CapsuleShapeDesc{radius, length}};
    }

    struct ColliderPreparationFailure { ColliderAdmissionError code; const char* reason; };
    // Fresh, command-only preparation. Failure values/status never own strings.
    static std::expected<CollisionShapeDesc, ColliderPreparationFailure> PrepareMeshColliderAdmission(
        _Scene& scene, SceneRenderResources& resources, const _Entity& entity, ShapeType type)
    {
        try
        {
            const auto& registry=scene.ReadRegistry();
            if (entity.GetSceneContext()!=&scene || !entity ||
                registry.storage(entt::type_hash<MeshRendererComponent>::value())==registry.storage().end() ||
                registry.storage(entt::type_hash<Transform3DComponent>::value())==registry.storage().end() ||
                !entity.HasAllComponents<MeshRendererComponent,Transform3DComponent>())
                return std::unexpected(ColliderPreparationFailure{ColliderAdmissionError::InvalidIdentity,"Mesh fitting requires a live rendered body"});
            const auto source=resources.GeometrySource(entity.GetComponent<MeshRendererComponent>().mesh);
            if(!source || !source->source)
                return std::unexpected(ColliderPreparationFailure{ColliderAdmissionError::InvalidShape,"Mesh CPU geometry is unavailable"});
            auto description=FitMeshCollider(*source->source,entity.GetComponent<Transform3DComponent>().Scale,type);
            if(!description)
                return std::unexpected(ColliderPreparationFailure{ColliderAdmissionError::InvalidShape,"Mesh collider dimensions or layout are invalid"});
            return std::move(*description);
        }
        catch(const std::bad_alloc&)
        {
            return std::unexpected(ColliderPreparationFailure{ColliderAdmissionError::AllocationFailure,"Mesh collider preparation allocation failed; descriptor unavailable"});
        }
    }

    // Shared by both UI actions and the opt-in native fault fixture. No status
    // formatting or allocation follows the admitted Scene/Physics transaction.
    ColliderAdmissionReport RunMeshColliderAdmission(_Scene& scene,SceneRenderResources& resources,
        const _Entity& entity,ShapeType type,bool apply)
    {
        auto description=PrepareMeshColliderAdmission(scene,resources,entity,type);
        if(!description)
        {
            ColliderAdmissionReport rejected;rejected.error=description.error().code;rejected.reason=description.error().reason;
            return rejected;
        }
        return apply?scene.ReplaceBodyColliderIfNoAddedOverlap(entity,*description):scene.AssessBodyColliderReplacement(entity,*description);
    }

    static const char* ColliderTypeName(ShapeType type)
    {
        switch (type)
        {
        case ShapeType::Sphere: return "Sphere";
        case ShapeType::Box: return "Box";
        case ShapeType::Capsule: return "Capsule";
        case ShapeType::Convex: return "Convex Hull";
        default: return "Unavailable";
        }
    }

    struct ColliderInspector
    {
        PhysicsBodyHandle target{};
        _Entity entity;
        bool refreshRequested{};
        std::optional<CollisionShapeDesc> queued;
        bool queuedForce{};
        std::array<FrameDebugLine, 260> cachedPreview{};
        std::size_t cachedPreviewCount{};
        int replacement{};
        bool fitMesh{};
        bool temporaryPause{};
        bool readbackFailed{};
        const _Scene* sceneIdentity{};
        float radius{1}, halfLength{.5f};
        Vec3f halfExtents{.5f}, center{};
        std::optional<ColliderAdmissionReport> result;
        std::optional<ColliderReplacementConfirmation> confirmation;
        std::optional<ColliderShapeState> before, after;
        std::unique_ptr<PhysicalShape> preview;
        std::uint64_t previewContext{};
        Vec3f previewScale{1};
        MeshHandle previewMesh{};
        float previewYaw{.55f}, previewPitch{-.35f};
        std::uintptr_t activeIdentity{};
        std::uint64_t activeRevision{};
        const char* status{"Ready"};

        void Revert(const ColliderShapeState& current)
        {
            queued.reset();
            replacement = current.type == ShapeType::Box ? 1 : current.type == ShapeType::Capsule ? 2 : 0;
            fitMesh = false;
            if (const auto* sphere = std::get_if<SphereShapeDesc>(&current.dimensions)) radius = sphere->radius;
            if (const auto* capsule = std::get_if<CapsuleShapeDesc>(&current.dimensions))
            {
                radius = capsule->radius;
                halfLength = capsule->halfLength;
            }
            if (const auto* box = std::get_if<BoxShapeDesc>(&current.dimensions))
            {
                halfExtents = box->halfExtents;
                center = box->center;
            }
            activeIdentity = current.identity;
            activeRevision = current.revision;
            confirmation.reset();
            preview.reset();
            cachedPreviewCount = 0;
            refreshRequested = false;
            temporaryPause = false;
        }

        ShapeType PendingType() const
        {
            return replacement == 1 ? ShapeType::Box : replacement == 2 ? ShapeType::Capsule : ShapeType::Sphere;
        }

        CollisionShapeDesc Manual() const
        {
            if (replacement == 1) return BoxShapeDesc{halfExtents, center};
            if (replacement == 2) return CapsuleShapeDesc{radius, halfLength};
            return SphereShapeDesc{radius};
        }
    };

    static ColliderInspector colliderEditor;
    static std::array<ColliderInspector, 64> otherColliderEditors;
    static bool hasColliderDrafts{};
    static _Scene* colliderDraftScene{};
    static std::uint64_t colliderSelectionRevision{};
    static bool colliderEscOwned{};

    static void EndColliderConfirmation(ColliderInspector& editor, _Scene& scene, bool userPaused)
    {
        editor.confirmation.reset();
        // UI actions run between Physics updates. User pause is authoritative;
        // releasing this lease never toggles or restores a saved user setting.
        if (editor.temporaryPause && editor.sceneIdentity == &scene) scene.SetPaused(userPaused);
        editor.temporaryPause = false;
        editor.readbackFailed = false;
    }

    void ResetColliderEditor(_Scene& scene, bool userPaused)
    {
        EndColliderConfirmation(colliderEditor, scene, userPaused);
        colliderEditor = ColliderInspector{};
        if (hasColliderDrafts)
            for (auto& draft : otherColliderEditors) draft = ColliderInspector{};
        hasColliderDrafts = false;
        colliderDraftScene = nullptr;
        colliderEscOwned = false;
    }

    bool CancelColliderConfirmation(_Scene& scene, bool userPaused, bool clearPreview)
    {
        if (!colliderEditor.confirmation && !colliderEditor.temporaryPause) return false;
        EndColliderConfirmation(colliderEditor, scene, userPaused);
        if (clearPreview)
        {
            colliderEditor.preview.reset();
            colliderEditor.cachedPreviewCount = 0;
            colliderEditor.refreshRequested = false;
        }
        colliderEditor.status = "Cancelled; collider unchanged.";
        return true;
    }

    static void HandleColliderUiEscape(_Scene& scene, bool userPaused)
    {
        const bool owner = ImGui::GetIO().WantTextInput ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!colliderEscOwned && !owner && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            (void)CancelColliderConfirmation(scene, userPaused, true);
        colliderEscOwned = owner;
    }

    void SetColliderUserPause(_Scene& scene, bool userPaused)
    {
        const bool pending = colliderEditor.confirmation.has_value();
        EndColliderConfirmation(colliderEditor, scene, userPaused);
        scene.SetPaused(userPaused);
        if (pending) colliderEditor.status = "User pause changed; apply again.";
    }

    bool ColliderEditorTemporarilyPaused(_Scene& scene, const _Entity& selected, bool userPaused)
    {
        const auto binding = scene.GetBodyBinding(selected);
        if (hasColliderDrafts && colliderDraftScene != &scene)
            ResetColliderEditor(scene, userPaused);
        if (!binding || *binding != colliderEditor.target)
        {
            EndColliderConfirmation(colliderEditor, scene, userPaused);
            return false;
        }
        return colliderEditor.temporaryPause;
    }

    static glm::vec4 PreviewColor(const ColliderInspector& editor, const PhysicsSystem& physics)
    {
        return {1, .65f, .15f, 1};
    }

    DebugGeometryResult AppendColliderEditorPreviews(_Scene& scene,
        const ColliderOutlineSelection& selected, DebugGeometry& out)
    {
        if (!selected.count || !hasColliderDrafts) return {};
        auto& physics = *scene.GetPhysicsSystem();
        const auto append = [&](const ColliderInspector& draft) -> DebugGeometryResult
        {
            if (draft.sceneIdentity != &scene || !draft.preview || !selected.Contains(draft.target)) return {};
            const auto pose = physics.ReadBodyPose(draft.target);
            const auto current = physics.ReadBodyCollider(draft.target);
            if (!pose || !current || current->identity != draft.activeIdentity || current->revision != draft.activeRevision) return {};
            const auto color = PreviewColor(draft, physics);
            for (std::size_t i = 0; i < draft.cachedPreviewCount; ++i)
            {
                const auto& line = draft.cachedPreview[i];
                // Dash each edge instead of dropping whole Box edges or Capsule arcs.
                const int parts=draft.preview->GetShapeType()==ShapeType::Box?8:1;
                for(int d=0;d<parts;++d)
                    if (auto r = out.Line(pose->position + pose->orientation * glm::mix(line.start,line.end,float(d)/parts),
                        pose->position + pose->orientation * glm::mix(line.start,line.end,(d+.6f)/parts),
                        color, DebugDepth::Overlay); !r) return r;
            }
            return {};
        };
        if (auto r = append(colliderEditor); !r) return r;
        for (const auto& draft : otherColliderEditors)
            if (draft.target) if (auto r = append(draft); !r) return r;
        return {};
    }

    static void DrawColliderVisual(ColliderInspector& editor, PhysicsSystem& physics, PhysicsBodyHandle handle)
    {
        std::array<FrameDebugLine, 1024> storage;
        DebugGeometry geometry(storage);
        const auto pose = physics.ReadBodyPose(handle);
        if (!pose) return;
        if (!physics.AppendPresentationGeometry(geometry, handle, {}, pose->orientation)) return;
        const auto currentCount = geometry.Lines().size();
        const auto proposedColor = PreviewColor(editor, physics);
        if (editor.preview && !PhysicsSystem::AppendColliderPreview(geometry, *editor.preview,
                {}, pose->orientation, proposedColor)) return;
        const auto start = ImGui::GetCursorScreenPos();
        const ImVec2 size{(std::max)(1.f, ImGui::GetContentRegionAvail().x), ImGui::GetFontSize() * 10};
        ImGui::InvisibleButton("Collider preview (drag to rotate)", size);
        NoteColliderControl("Visual preview");
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
        {
            editor.previewYaw += ImGui::GetIO().MouseDelta.x * .01f;
            editor.previewPitch += ImGui::GetIO().MouseDelta.y * .01f;
        }
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, {start.x + size.x, start.y + size.y}, IM_COL32(23, 26, 31, 255), 4);
        float extent = .001f;
        for (const auto& line : geometry.Lines())
            extent = (std::max)({extent, glm::length(line.start), glm::length(line.end)});
        const Quat rotation = glm::angleAxis(editor.previewPitch, Vec3f{1, 0, 0}) *
            glm::angleAxis(editor.previewYaw, Vec3f{0, 1, 0});
        const float zoom = (std::min)(size.x, size.y) * .43f / extent;
        const auto project = [&](const Vec3f& point)
        {
            const auto rotated = rotation * point;
            return ImVec2{start.x + size.x * .5f + rotated.x * zoom,
                start.y + size.y * .5f - rotated.y * zoom};
        };
        for (std::size_t i = 0; i < geometry.Lines().size(); ++i)
        {
            const auto& line = geometry.Lines()[i];
            const auto color = i < currentCount ? IM_COL32(0, 220, 230, 180)
                : ImGui::ColorConvertFloat4ToU32({proposedColor.r, proposedColor.g, proposedColor.b, 1});
            if(i<currentCount)draw->AddLine(project(line.start),project(line.end),color,ImGui::GetFontSize()*.08f);
            else
            {
                const int parts=editor.preview && editor.preview->GetShapeType()==ShapeType::Box?8:1;
                for(int d=0;d<parts;++d)draw->AddLine(project(glm::mix(line.start,line.end,float(d)/parts)),
                    project(glm::mix(line.start,line.end,(d+.6f)/parts)),color,ImGui::GetFontSize()*.15f);
            }
        }
        ImGui::TextWrapped("%s", editor.preview ? (proposedColor.g < .3f ? "Cyan: current / Red: overlap risk" :
            "Cyan: current / Orange: proposed") : "Cyan: current collider");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag the preview above to rotate.");
    }

    static const char* ColliderFailureStatus(ColliderAdmissionError error)
    {
        switch (error)
        {
        case ColliderAdmissionError::InvalidShape: return "Invalid dimensions or inertia; unchanged.";
        case ColliderAdmissionError::InvalidIdentity: return "Body unavailable; unchanged.";
        case ColliderAdmissionError::AllocationFailure: return "Allocation failed; unchanged.";
        case ColliderAdmissionError::StaleConfirmation: return "Edit changed; apply again.";
        case ColliderAdmissionError::Busy: return "Physics is busy; unchanged.";
        default: return "Assessment could not finish; unchanged.";
        }
    }

    static void RefreshColliderDraft(ColliderInspector& editor, _Scene& scene,
        SceneRenderResources& resources, const _Entity& entity)
    {
        RBS_REVIEW_SCOPE(2);
        editor.refreshRequested = false;
        editor.before.reset();
        editor.after.reset();
        editor.preview.reset();
        editor.cachedPreviewCount = 0;
        const auto description = editor.fitMesh ? PrepareMeshColliderAdmission(scene, resources, entity, editor.PendingType())
            : std::expected<CollisionShapeDesc, ColliderPreparationFailure>{editor.Manual()};
        if (!description)
        {
            editor.result.reset();
            editor.status = description.error().reason;
            return;
        }
        auto& physics = *scene.GetPhysicsSystem();
        editor.result = scene.AssessBodyColliderReplacement(entity, *description, &editor.preview);
        editor.previewContext = physics.ColliderContextRevision();
        editor.previewScale = entity.GetComponent<Transform3DComponent>().Scale;
        if (entity.HasAllComponents<MeshRendererComponent>())
            editor.previewMesh = entity.GetComponent<MeshRendererComponent>().mesh;
        editor.status = editor.result->outcome == ColliderAdmissionOutcome::NoAddedOverlap
            ? "Preview ready; not applied." : PhysicsSystem::CanConfirmColliderRisk(*editor.result)
            ? "Preview has overlap risk; not applied." : ColliderFailureStatus(editor.result->error);
        if (editor.preview)
        {
            DebugGeometry geometry(editor.cachedPreview);
            if (PhysicsSystem::AppendColliderPreview(geometry, *editor.preview, {}, Quat{1, 0, 0, 0}, {1, 1, 1, 1}))
                editor.cachedPreviewCount = geometry.Lines().size();
        }
    }

    static void BroadcastColliderType(ColliderInspector& editor, int type)
    {
        if (&editor != &colliderEditor || !hasColliderDrafts) return;
        for (auto& other : otherColliderEditors)
            if (other.target && other.sceneIdentity == editor.sceneIdentity)
            {
                other.replacement = type;
                other.refreshRequested = true;
            }
    }

    // Widths scale with the font; labels never share the widget's clipped area.
    static void ColliderField(const char* label)
    {
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::TextWrapped("%s", label);
        if (width >= ImGui::GetFontSize() * 30)
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 15);
        ImGui::SetNextItemWidth(-1);
    }

    template<class Validator>
    static bool ColliderNumber(const char* label,float& value,Editor::Numeric::Bounds<float> bounds,Validator validate)
    {
        const char* units=std::string_view(label)=="Radius"?"Radius (m)":"Half length (m)";
        const bool changed=Editor::Number(units,value,.01f,"Metres. Ctrl+click to type; invalid dimensions leave the proposal unchanged.",bounds,validate);
        NoteColliderControl(label);return changed;
    }
    template<class Validator>
    static bool ColliderVector(const char* label,Vec3f& value,Validator validate)
    {
        const bool extents=std::string_view(label)=="Half extents";
        return Editor::XYZ(extents?"Half extents (m)":"Center (m)",&value.x,extents?.5f:0.f,.01f,"Metres. Invalid geometry leaves the proposal unchanged.",
            extents?Editor::Numeric::Positive<float>():Editor::Numeric::Bounds<float>{},validate);
    }

    static void ColliderDimensions(const ColliderShapeState& shape)
    {
        if (!shape.editable) return;
        if (const auto* sphere = std::get_if<SphereShapeDesc>(&shape.dimensions))
            ImGui::TextWrapped("Radius %.6g", double(sphere->radius));
        else if (const auto* capsule = std::get_if<CapsuleShapeDesc>(&shape.dimensions))
            ImGui::TextWrapped("Radius %.6g / Half Length %.6g", double(capsule->radius), double(capsule->halfLength));
        else if (const auto* box = std::get_if<BoxShapeDesc>(&shape.dimensions))
            ImGui::TextWrapped("Half extents %.6g, %.6g, %.6g", double(box->halfExtents.x),
                double(box->halfExtents.y), double(box->halfExtents.z));
    }

    static void FinishColliderCommand(ColliderInspector& editor, _Scene& scene, bool& userPaused,
        ColliderAdmissionReport result, bool force)
    {
        auto& physics=*scene.GetPhysicsSystem();const auto handle=editor.target;

            editor.confirmation.reset();
            editor.after.reset();
            editor.result = std::move(result);
            if (editor.result->committed)
            {
                editor.preview.reset();
                const auto readback = physics.ReadBodyCollider(handle);
                if (readback && readback->type == editor.PendingType())
                {
                    editor.after = *readback;
                    editor.activeIdentity = readback->identity;
                    editor.activeRevision = readback->revision;
                    editor.status = force ? "Applied (risk accepted)." : "Applied.";
                }
                else
                {
                    editor.status = "Commit readback failed; inspect Diagnostics.";
                    editor.readbackFailed = editor.temporaryPause = true;
                    scene.SetPaused(true);
                    return;
                }
                // Commit and native readback finish before releasing the pause.
                EndColliderConfirmation(editor, scene, userPaused);
            }
            else
            {
                EndColliderConfirmation(editor, scene, userPaused);
                if (PhysicsSystem::CanConfirmColliderRisk(*editor.result))
                {
                    auto confirmation = physics.PrepareColliderConfirmation(*editor.result);
                    if (confirmation)
                    {
                        editor.confirmation = std::move(*confirmation);
                        editor.temporaryPause = !userPaused;
                        scene.SetPaused(true);
                        editor.status = "Unchanged; confirmation required.";
                    }
                    else editor.status = ColliderFailureStatus(confirmation.error());
                }
                else editor.status = ColliderFailureStatus(editor.result->error);
            }
    }

    void ProcessColliderEditorCommands(_Scene& scene, const _Entity& selected, bool& userPaused)
    {
        auto& editor=colliderEditor;
        if(!editor.queued)return;
        auto desc=std::move(*editor.queued);editor.queued.reset();
        const auto binding=scene.GetBodyBinding(editor.entity);
        if(editor.sceneIdentity!=&scene || editor.entity!=selected || !binding || *binding!=editor.target)
        {EndColliderConfirmation(editor,scene,userPaused);editor.status="Selection changed; queued edit cancelled.";return;}
        auto result=editor.queuedForce && editor.confirmation?
            scene.ReplaceBodyColliderConfirmed(editor.entity,desc,*editor.confirmation):
            scene.ReplaceBodyColliderIfNoAddedOverlap(editor.entity,desc);
        FinishColliderCommand(editor,scene,userPaused,std::move(result),editor.queuedForce);
    }

    static void DrawColliderInspector(ColliderInspector& editor, _Scene& scene,
        SceneRenderResources& resources, const _Entity& entity, PhysicsBodyHandle handle,
        bool& userPaused)
    {
        auto& physics = *scene.GetPhysicsSystem();
        const auto current = physics.ReadBodyCollider(handle);
        if (!current)
        {
            EndColliderConfirmation(editor, scene, userPaused);
            editor.preview.reset();
            ImGui::TextWrapped("Current Collider: unavailable");
            return;
        }
        bool edited = editor.refreshRequested;
        if (editor.target != handle || editor.sceneIdentity != &scene)
        {
            EndColliderConfirmation(editor, scene, userPaused);
            editor = ColliderInspector{};
            editor.target = handle;
            editor.sceneIdentity = &scene;
            editor.entity = entity;
            editor.Revert(*current);
            Editor::Numeric::Reset();
            editor.fitMesh = bool(PrepareMeshColliderAdmission(scene, resources, entity, editor.PendingType()));
            edited = editor.fitMesh;
            if (!editor.fitMesh) editor.status = "No valid mesh fit; use manual dimensions.";
        }
        else if (editor.activeIdentity != current->identity)
        {
            EndColliderConfirmation(editor, scene, userPaused);
            editor.Revert(*current);
            Editor::Numeric::Reset();
            editor.fitMesh = bool(PrepareMeshColliderAdmission(scene, resources, entity, editor.PendingType()));
            edited = editor.fitMesh;
            editor.status = "Collider changed; edits refreshed.";
        }

        ImGui::Separator();
        ImGui::TextWrapped("Current Collider: %s", ColliderTypeName(current->type));
        NoteColliderControl("Current Collider");
        constexpr const char* types[]{"Sphere", "Box", "Capsule"};
        edited |= editor.preview && editor.activeRevision != current->revision;
        editor.activeRevision = current->revision;
        if (editor.preview && editor.fitMesh && entity.HasAllComponents<MeshRendererComponent>())
            edited |= editor.previewScale != entity.GetComponent<Transform3DComponent>().Scale ||
                editor.previewMesh != entity.GetComponent<MeshRendererComponent>().mesh;
        {
        std::optional<Editor::PropertyRow> row;
        if (Editor::Active) row.emplace("Shape", "Proposed collider type; Apply uses the existing overlap admission checks.");
        else ColliderField("Proposed Collider Type");
        const bool typeOpen = ImGui::BeginCombo("##ProposedCollider", types[editor.replacement]);
        NoteColliderControl("New Collider Type");
        if (typeOpen)
        {
            for (int index = 0; index < 3; ++index)
            {
                if (ImGui::Selectable(types[index], index == editor.replacement))
                {
                    editor.replacement = index;
                    BroadcastColliderType(editor, index);
                    edited = true;
                }
                NoteColliderControl(types[index]);
            }
            ImGui::EndCombo();
        }
        }
        if (Editor::Active)
        {
            Editor::PropertyRow row("Fit to mesh", "Fit the collider to the current render mesh. Disable for manual dimensions.");
            edited |= ImGui::Checkbox("##FitMesh", &editor.fitMesh);
        }
        else edited |= ImGui::Checkbox("Fit to Mesh", &editor.fitMesh);
        NoteColliderControl("Fit to Mesh");
        if (!editor.fitMesh)
        {
            const auto validShape=[](const CollisionShapeDesc& desc)->const char*
            {return CreateCollisionShape(desc)?nullptr:"Invalid shape: dimensions, bounds or inertia are not representable. Proposal unchanged.";};
            if (editor.replacement == 1)
            {
                edited |= ColliderVector("Half extents",editor.halfExtents,[&](const float* v){return validShape(BoxShapeDesc{{v[0],v[1],v[2]},editor.center});});
                edited |= ColliderVector("Center",editor.center,[&](const float* v){return validShape(BoxShapeDesc{editor.halfExtents,{v[0],v[1],v[2]}});});
            }
            else
            {
                edited |= ColliderNumber("Radius",editor.radius,Editor::Numeric::Positive<float>(),[&](float v){return validShape(editor.replacement==2?CollisionShapeDesc{CapsuleShapeDesc{v,editor.halfLength}}:CollisionShapeDesc{SphereShapeDesc{v}});});
                if (editor.replacement == 2)
                    edited |= ColliderNumber("Half Length",editor.halfLength,Editor::Numeric::Nonnegative<float>(),[&](float v){return validShape(CapsuleShapeDesc{editor.radius,v});});
            }
        }
        if (edited)
        {
            EndColliderConfirmation(editor, scene, userPaused);
            editor.status = "Pending edit";
        }
        const auto prepare = [&]() -> std::expected<CollisionShapeDesc, ColliderPreparationFailure>
        {
            if (editor.fitMesh) return PrepareMeshColliderAdmission(scene, resources, entity, editor.PendingType());
            return editor.Manual();
        };

        if (edited) RefreshColliderDraft(editor, scene, resources, entity);
        if (editor.fitMesh && editor.result)
        {
            ColliderShapeState fitted;
            fitted.editable = true;
            fitted.dimensions = editor.result->descriptor;
            ColliderDimensions(fitted);
        }
        DrawColliderVisual(editor, physics, handle);

        if (editor.confirmation)
        {
            const auto description = prepare();
            if (!description || !physics.IsColliderConfirmationCurrent(*editor.confirmation, handle, *description))
            {
                EndColliderConfirmation(editor, scene, userPaused);
                editor.status = "Context changed; apply again.";
            }
        }

        const auto finish = [&](ColliderAdmissionReport result,bool force)
        { FinishColliderCommand(editor,scene,userPaused,std::move(result),force); };

        const bool buttonsTogether = ImGui::GetContentRegionAvail().x >=
            ImGui::CalcTextSize("Apply to Active Object").x + ImGui::CalcTextSize("Revert").x +
            ImGui::GetStyle().FramePadding.x * 4 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginDisabled(Editor::Numeric::HasError());
        const bool apply = ImGui::Button("Apply to Active Object");
        NoteColliderControl("Apply");
        ImGui::EndDisabled();
        if (apply)
        {
            editor.before = *current;
            const auto description = prepare();
            if (description)
            {
                if(Editor::Active){editor.queued=*description;editor.queuedForce=false;editor.status="Apply queued for simulation boundary.";}
                else finish(scene.ReplaceBodyColliderIfNoAddedOverlap(entity,*description),false);
            }
            else
            {
                ColliderAdmissionReport failure;
                failure.error = description.error().code;
                failure.reason = description.error().reason;
                finish(std::move(failure), false);
            }
        }
        if (buttonsTogether) ImGui::SameLine();
        const bool revert = ImGui::Button("Revert");
        NoteColliderControl("Revert");
        if (revert)
        {
            EndColliderConfirmation(editor, scene, userPaused);
            editor.Revert(*current);
            Editor::Numeric::Reset();
            const auto fitted = PrepareMeshColliderAdmission(scene, resources, entity, editor.PendingType());
            editor.fitMesh = bool(fitted);
            editor.result.reset();
            if (fitted)
            {
                editor.result.emplace();
                (void)editor.result->SetDescriptor(*fitted);
            }
            editor.status = "Pending edits reverted.";
        }
        if (editor.confirmation && editor.result)
        {
            const bool known = editor.result->outcome == ColliderAdmissionOutcome::RejectedRisk;
            std::size_t affected = 0;
            for (const auto& pair : editor.result->pairs)
                if (pair.newOutcome == ColliderPairOutcome::Contact &&
                    (pair.oldOutcome == ColliderPairOutcome::SeparatedByBounds || pair.comparableMeasure)) ++affected;
            if (known) ImGui::TextWrapped("Added overlap with %zu bodies. Replace anyway?", affected);
            else ImGui::TextWrapped("Overlap risk is unverified. Force replacement?");
            const bool cancel = ImGui::Button("Cancel");
            NoteColliderControl("Cancel");
            if (cancel)
            {
                EndColliderConfirmation(editor, scene, userPaused);
                editor.preview.reset();
                editor.status = "Cancelled; collider unchanged.";
            }
            const bool advanced = known || ImGui::TreeNode("Advanced");
            if (!known) NoteColliderControl("Advanced");
            if (advanced)
            {
                if (!known) ImGui::TextWrapped("Safety is not certified.");
                const char* confirmLabel = known ? "Replace Anyway" : "Force Replace";
                const bool confirm = ImGui::Button(confirmLabel);
                NoteColliderControl(confirmLabel);
                if (confirm)
                {
                    const auto description = prepare();
                    if (description && editor.confirmation)
                    {
                        if(Editor::Active){editor.queued=*description;editor.queuedForce=true;editor.status="Confirmed Apply queued.";}
                        else finish(scene.ReplaceBodyColliderConfirmed(entity,*description,*editor.confirmation),true);
                    }
                }
                if (!known) ImGui::TreePop();
            }
        }
        ImGui::TextWrapped("%s", editor.status);
        if (editor.temporaryPause)
            ImGui::TextWrapped("%s", editor.readbackFailed ? "Readback failure pause" : "Confirmation pause (temporary)");
        if (ImGui::TreeNode("Diagnostics (Developer)"))
        {
            ImGui::TextWrapped("Body %u / generation %llu / world %llu", handle.index,
                static_cast<unsigned long long>(handle.generation), static_cast<unsigned long long>(handle.registry));
            const auto active = physics.ReadBodyCollider(handle);
            if (active)
            {
                ImGui::TextWrapped("Physics Shape readback: %s / identity %llu / revision %llu", ColliderTypeName(active->type),
                    static_cast<unsigned long long>(active->identity), static_cast<unsigned long long>(active->revision));
                ColliderDimensions(*active);
            }
            if (editor.result)
            {
                const auto& result = *editor.result;
                ImGui::TextWrapped("Outcome %d / error %d / committed %s", int(result.outcome), int(result.error),
                    result.committed ? "yes" : "no");
                ImGui::TextWrapped("%s", result.reason);
                ImGui::TextWrapped("Assessment %.3f ms / %zu pairs", result.elapsedMs, result.pairs.size());
                for (const auto& pair : result.pairs)
                    ImGui::TextWrapped("Body %u: %s / %d -> %d / %.9g -> %.9g", pair.other.index, pair.reason,
                        int(pair.oldOutcome), int(pair.newOutcome), pair.oldMeasure, pair.newMeasure);
            }
            ImGui::TreePop();
        }
    }

    static void SyncColliderEditors(_Scene& scene, SceneRenderResources& resources,
        const ColliderOutlineSelection& selection, const _Entity& active, bool userPaused)
    {
        RBS_REVIEW_SCOPE(1);
        if (!selection.count)
        {
            if (hasColliderDrafts) ResetColliderEditor(scene, userPaused);
            return;
        }
        if (hasColliderDrafts && colliderDraftScene != &scene) ResetColliderEditor(scene, userPaused);
        if (colliderSelectionRevision != selection.revision)
        {
            EndColliderConfirmation(colliderEditor, scene, userPaused);
            colliderSelectionRevision = selection.revision;
        }
        for (auto& draft : otherColliderEditors)
            if (draft.target && (!selection.Contains(draft.target) || !scene.GetPhysicsSystem()->ReadBodyPose(draft.target)))
                draft = ColliderInspector{};
        const auto binding = scene.GetBodyBinding(active);
        if (!binding || colliderEditor.target != *binding)
        {
            EndColliderConfirmation(colliderEditor, scene, userPaused);
            if (colliderEditor.target && selection.Contains(colliderEditor.target))
                for (auto& draft : otherColliderEditors)
                    if (!draft.target) { draft = std::move(colliderEditor); break; }
            colliderEditor = ColliderInspector{};
            if (binding)
                for (auto& draft : otherColliderEditors)
                    if (draft.target == *binding)
                    {
                        colliderEditor = std::move(draft);
                        draft = ColliderInspector{};
                        break;
                    }
        }
        const auto initialize = [&](ColliderInspector& draft, const _Entity& entity, PhysicsBodyHandle handle)
        {
            const auto current = scene.GetPhysicsSystem()->ReadBodyCollider(handle);
            if (!current) return;
            if (!draft.target)
            {
                draft.target = handle;
                draft.entity = entity;
                draft.sceneIdentity = &scene;
                draft.Revert(*current);
                draft.fitMesh = bool(PrepareMeshColliderAdmission(scene, resources, entity, draft.PendingType()));
                draft.refreshRequested = draft.fitMesh;
            }
            else if (draft.activeIdentity != current->identity || draft.activeRevision != current->revision)
            {
                draft.activeIdentity = current->identity;
                draft.activeRevision = current->revision;
                draft.refreshRequested = bool(draft.preview);
            }
            if (draft.preview && draft.fitMesh && entity.HasAllComponents<MeshRendererComponent>())
                draft.refreshRequested |= draft.previewScale != entity.GetComponent<Transform3DComponent>().Scale ||
                    draft.previewMesh != entity.GetComponent<MeshRendererComponent>().mesh;
            if (draft.refreshRequested) RefreshColliderDraft(draft, scene, resources, entity);
        };
        for (std::size_t i = 0; i < selection.count; ++i)
        {
            const auto resolved = scene.RenderData().Resolve(selection.entities[i]);
            if (!resolved) continue;
            const _Entity entity{*resolved, &scene};
            if (binding && selection.bodies[i] == *binding) initialize(colliderEditor, entity, *binding);
            else
            {
                ColliderInspector* slot = nullptr;
                for (auto& draft : otherColliderEditors)
                    if (draft.target == selection.bodies[i]) { slot = &draft; break; }
                if (!slot) for (auto& draft : otherColliderEditors) if (!draft.target) { slot = &draft; break; }
                if (slot) initialize(*slot, entity, selection.bodies[i]);
            }
        }
        hasColliderDrafts = true;
        colliderDraftScene = &scene;
    }

    // Legacy programmatic/capture entry point intentionally retains its contract.
    std::expected<void, std::string> ReplaceMeshCollider(_Scene& scene, SceneRenderResources& resources,
                                                       _Entity entity, ShapeType type)
    {
        if (!entity || entity.GetSceneContext() != &scene ||
            !entity.HasAllComponents<MeshRendererComponent, Transform3DComponent>() || !scene.GetBodyBinding(entity))
            return std::unexpected(std::string("Mesh fitting requires a live rendered body."));
        const auto source = resources.GeometrySource(entity.GetComponent<MeshRendererComponent>().mesh);
        if (!source) return std::unexpected(DescribeSceneResourceError(source.error()));
        if (!source->source) return std::unexpected(std::string("Mesh CPU geometry is unavailable."));
        auto description = FitMeshCollider(*source->source, entity.GetComponent<Transform3DComponent>().Scale, type);
        if (!description) return std::unexpected(std::format("Mesh collider fit rejected ({})", int(description.error())));
        if (auto replaced = scene.ReplaceBodyCollider(entity, *description); !replaced)
            return std::unexpected(std::format("Runtime collider replacement rejected ({})", int(replaced.error().code)));
        return {};
    }
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
    std::expected<void,std::string> ApplyCapturedMeshFit(_Scene& scene,SceneRenderResources& resources,_Entity entity,const CameraSnapshot* camera)
    {
        if(!entity||entity.GetSceneContext()!=&scene||!entity.HasAllComponents<MeshRendererComponent,Transform3DComponent>())
            return std::unexpected(std::string("Captured mesh fitting requires the live selected rendered body."));
        bool captureStarted=false;
        if(ColliderCapture::Enabled()){
            const auto source=resources.GeometrySource(entity.GetComponent<MeshRendererComponent>().mesh);
            if(source&&source->source){const auto fitted=FitMeshCollider(*source->source,entity.GetComponent<Transform3DComponent>().Scale,ShapeType::Capsule);
                if(fitted)captureStarted=ColliderCapture::Before(scene,resources,entity,*fitted,camera);}
            if(ColliderCapture::autoConsumed&&!captureStarted)
                return std::unexpected(std::string("Automatic fitted replacement stopped: required before-capture unavailable."));
            if(captureStarted){
                ColliderReplacementObservation::BeginReplacementCapture(scene,entity);
                if(!ColliderReplacementObservation::session||ColliderReplacementObservation::session->failed)
                    return std::unexpected(std::string("Captured replacement stopped: pre-command state admission failed."));
            }
        }
        const auto commandStartedUtc=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto changed=ReplaceMeshCollider(scene,resources,entity,ShapeType::Capsule);
        const auto commandEndedUtc=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(captureStarted){ColliderCapture::After(scene,bool(changed),commandStartedUtc,commandEndedUtc);ColliderReplacementObservation::EndReplacementCapture(scene,entity,bool(changed));}
        return changed;
    }
#endif
}
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
#include "../tests/ColliderEditorChecks.h"
#endif
#ifdef GENGINE_LIFECYCLE_VALIDATION
namespace PreEditorLifecycle
{
    bool UIActive();
    bool ObserveViewportFocus(bool, bool, std::uint64_t, double, float, float,
                              ImDrawList*, int, int, const ImVec4&, const ImVec4&);
}
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
#include <array>
namespace PreEditorLifecycle
{
    bool PickTraceActive() noexcept;
    void PickTraceValues(const char*, std::int64_t = -1, const std::array<std::uint64_t, 12>& = {},
                         const std::array<double, 12>& = {}) noexcept;
    void PickTraceUI(const char*, const InputRouting&, int, unsigned, unsigned) noexcept;
    void PickTraceUIBegin() noexcept;
}
#endif
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
namespace PreEditorInput
{
    void NoteUIItem(unsigned);
    void NoteUIMenu(unsigned, bool, float, float, float, float);
    void DrawInputFixture();
}
#endif

void RigidBodySimulationApp::PublishPanelInputRegion()
{
    const auto position = ImGui::GetWindowPos();
    const auto size = ImGui::GetWindowSize();
    const auto origin = ImGui::GetMainViewport()->Pos;
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    const InputRect region{position.x - origin.x, position.y - origin.y,
                           position.x + size.x - origin.x, position.y + size.y - origin.y};
    auto result = BaseApp::GetInputManager()->AddUIRegion(region);
    PreEditorLifecycle::PickTraceValues("ui_region", bool(result), {},
                                        {region.left, region.top, region.right, region.bottom});
    if (!result)
#else
    if (auto result = BaseApp::GetInputManager()->AddUIRegion(
            {position.x - origin.x, position.y - origin.y, position.x + size.x - origin.x,
             position.y + size.y - origin.y});
        !result)
#endif
        Log::GetCoreLogger()->error("UI input region rejected: {}", int(result.error()));
}

bool RigidBodySimulationApp::RequestDistanceChainImpulse()
{
    if (!m_ActiveScene || !m_ActiveScene->GetPhysicsSystem() ||
        (SandboxEnabled() && (m_SceneRequest || m_SimulationState == Rbs::SimulationState::Switching ||
                             m_SimulationState == Rbs::SimulationState::Failed))) return false;
    auto* world = m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    if (!world) return false;
    return m_ChainImpulse.Request(*world, m_ChainImpulseMagnitude,
        static_cast<Rbs::DistanceChainImpulseControl::Direction>(m_ChainImpulseDirection));
}

void RigidBodySimulationApp::DrawDistanceChainImpulseControls()
{
    if (!m_ActiveSelection.IsDistanceScene()) return;
    using Control = Rbs::DistanceChainImpulseControl;
    ImGui::Separator();
    ImGui::TextWrapped("Distance chain impulse - terminal link %u", m_ActiveSelection.scene==Rbs::RbsScenePreset::DistancePendulum?1:m_ActiveSelection.chainLinks);
    if (SandboxEnabled()) ImGui::TextWrapped("Automatic 0.5 Ns impulse after tick 300; manual controls below.");
    if (SandboxEnabled())
    {
        {
            Rbs::Editor::PropertyRow row("Impulse (Ns)", "Finite magnitude from 0 to 5 Ns. Applied once at the next Physics step.");
            Rbs::Editor::Numeric::Scalar("##Impulse",m_ChainImpulseMagnitude,.01f,Rbs::Editor::Numeric::Closed(Control::MinMagnitude,Control::MaxMagnitude),Rbs::Editor::Numeric::Accept{},true);
        }
        {
            Rbs::Editor::PropertyRow row("Direction");
            ImGui::Combo("##Direction", &m_ChainImpulseDirection, "+X\0-X\0+Z\0-Z\0");
        }
    }
    else
    {
        Rbs::Editor::Numeric::Scalar("Impulse (Ns)",m_ChainImpulseMagnitude,.01f,Rbs::Editor::Numeric::Closed(Control::MinMagnitude,Control::MaxMagnitude),Rbs::Editor::Numeric::Accept{},true);
        ImGui::Combo("Horizontal direction", &m_ChainImpulseDirection, "+X\0-X\0+Z\0-Z\0");
    }
    ImGui::BeginDisabled(Rbs::Editor::Numeric::HasError() || !m_ChainImpulse.Enabled() || m_ChainImpulse.Pending() || m_SceneRequest.has_value());
    if (ImGui::Button("Apply Impulse")) RequestDistanceChainImpulse();
    ImGui::EndDisabled();
    const char* status = "Ready";
    switch (m_ChainImpulse.GetStatus())
    {
    case Control::Status::Ready: break;
    case Control::Status::Disabled: status = "Manual impulses disabled during qualification"; break;
    case Control::Status::Queued: status = m_ActiveScene->IsPaused()
        ? "Queued; resume simulation to apply" : "Queued for next Physics step"; break;
    case Control::Status::Applied: status = "Impulse applied"; break;
    case Control::Status::Zero: status = "Zero impulse; no motion requested"; break;
    case Control::Status::InvalidMagnitude: status = "Impulse must be finite and between 0 and 5 Ns"; break;
    case Control::Status::InvalidDirection: status = "Invalid horizontal direction"; break;
    case Control::Status::InvalidTarget: status = "Terminal body is no longer a live dynamic body"; break;
    case Control::Status::Rejected: status = "Physics rejected impulse; request cleared"; break;
    }
    ImGui::TextWrapped("%s", status);
    if (m_ChainImpulse.AppliedCount())
    {
        const auto last = m_ChainImpulse.LastApplied();
        ImGui::Text("Last applied: (%.2f, %.2f, %.2f) Ns", last.x, last.y, last.z);
    }
    else ImGui::TextUnformatted("Last applied: none");
    ImGui::Separator();
#ifdef GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS
    Rbs::DistanceChainImpulseChecks::PanelDrawn();
#endif
}

void RigidBodySimulationApp::DrawPhysicsSettings()
{
    using State=Rbs::SimulationState;
        const auto backendLabel=[](PhysicsBackend backend){return backend==PhysicsBackend::Jolt?"Jolt 5.6.0":"Native";};
        ImGui::TextWrapped("Active backend: %s",backendLabel(m_Backend));
        if(auto* world=m_ActiveScene?m_ActiveScene->GetPhysicsSystem():nullptr)
            ImGui::Text("Tick: %llu",static_cast<unsigned long long>(world->GetCompletedSteps()));
        if(m_Backend==PhysicsBackend::Jolt)
            ImGui::TextWrapped("Jolt native friction; authored spin/rolling resistance lengths are unsupported.");
        ImGui::BeginDisabled(m_SceneRequest.has_value() || m_SimulationState==State::Switching || !m_ActiveScene);
        {
        Rbs::Editor::PropertyRow row("Backend", "Apply rebuilds this authored scene at Tick 0; failure retains the previous world.");
        const float backendWidth=ImGui::GetContentRegionAvail().x;
        const bool inlineApply=backendWidth>=Rbs::Editor::Px(240);
        ImGui::SetNextItemWidth(inlineApply?backendWidth-Rbs::Editor::Px(64):backendWidth);
        if(ImGui::BeginCombo("##PhysicsBackend",backendLabel(m_PendingBackend)))
        {
            for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt})
            {
                ImGui::BeginDisabled(!PhysicsBackendAvailable(backend));
                if(ImGui::Selectable(backendLabel(backend),backend==m_PendingBackend))m_PendingBackend=backend;
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        if(inlineApply)ImGui::SameLine();
        ImGui::BeginDisabled(m_PendingBackend==m_Backend || !PhysicsBackendAvailable(m_PendingBackend));
        if(ImGui::Button("Apply##PhysicsBackend"))RequestBackend(m_PendingBackend);
        ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
        if(!PhysicsBackendAvailable(PhysicsBackend::Jolt))ImGui::TextWrapped("Jolt 5.6.0 is not enabled in this build.");
        ImGui::TextWrapped("Apply stops and rebuilds this authored scene at Tick 0. Failure retains the previous world.");
    DrawJoltSettings();
    DrawDistanceChainImpulseControls();
    ImGui::TextWrapped("%s",m_SceneDiagnostic.c_str());
}


void RigidBodySimulationApp::PrepareColliderUi()
{
    m_ColliderOutlines.Prune(*m_ActiveScene->GetPhysicsSystem());
    m_EditorSelection.Prune(m_ActiveScene->RenderData());
    if(m_HoveredEntity.GetSceneContext() &&
       (m_HoveredEntity.GetSceneContext()!=m_ActiveScene.get() || !m_HoveredEntity))
        SelectColliderEntity({},true);
    Rbs::SyncColliderEditors(*m_ActiveScene, *m_SceneResources, m_ColliderOutlines, m_HoveredEntity, m_IsPause);
    Rbs::HandleColliderUiEscape(*m_ActiveScene, m_IsPause);
}
void RigidBodySimulationApp::DrawLegacyDiagnosticPanels()
{
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if(ImGui::GetFrameCount()==4)ImGui::LogToFile(-1,"scene-ui-text.log");
#endif
    DrawShaderPanel();
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if(ImGui::GetFrameCount()==4)ImGui::LogToFile(-1,"scene-ui-text.log");
#endif
    DrawMaterialPanel();
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
    if (std::getenv("GENGINE_COLLIDER_SELECTION_SMOKE_OUTPUT"))
    {
        Rbs::ColliderEditorProbe::controls.clear();
        Rbs::ColliderEditorProbe::observing = true;
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos({870, 35}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({390, 650}, ImGuiCond_Always);
    }
#endif
#ifdef GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
#endif
    if(ImGui::Begin("Body Transform"))
    {
        PublishPanelInputRegion();
        if (!SandboxEnabled())
        {
            bool paused = m_IsPause;
            ImGui::BeginDisabled(m_SceneRequest.has_value() ||
                (SandboxEnabled() && (m_SimulationState == Rbs::SimulationState::Switching ||
                                      m_SimulationState == Rbs::SimulationState::Failed)));
            if (ImGui::Checkbox("Pause simulation", &paused))
            {
                if (SandboxEnabled()) SetSimulationState(paused ? Rbs::SimulationState::Paused : Rbs::SimulationState::Running);
                else { m_IsPause = paused; Rbs::SetColliderUserPause(*m_ActiveScene, paused); }
            }
            ImGui::EndDisabled();
        }
        DrawDistanceChainImpulseControls();
        DrawPhysicsDebugSection();
        DrawObjectProperties();
        DrawPresentationSettings();
    }
    ImGui::End();
}
void RigidBodySimulationApp::DrawPhysicsDebugSection()
{
        int outlines = m_IsShowDebugBoundingBox ? 2 : m_ShowSelectedColliderOutlines ? 1 : 0;
        Rbs::ColliderField("Physics visualization scope");
        if (ImGui::Combo("##ColliderOutlines", &outlines, "Off\0Selected\0All\0"))
        {
            m_IsShowDebugBoundingBox = outlines == 2;
            m_ShowSelectedColliderOutlines = outlines == 1;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+Click toggles selected objects (up to 4096). Esc clears selection.");
        if (ImGui::TreeNode("Layers"))
        {
            ImGui::Checkbox("Collider Outlines", &m_ColliderLayers.colliders);
            ImGui::Checkbox("World AABBs", &m_ColliderLayers.worldAabbs);
            ImGui::BeginDisabled(m_Backend==PhysicsBackend::Jolt);
            ImGui::Checkbox("Contact Points", &m_ColliderLayers.contactPoints);
            ImGui::Checkbox("Contact Normals", &m_ColliderLayers.contactNormals);
            ImGui::EndDisabled();
            if(m_Backend==PhysicsBackend::Jolt)ImGui::TextWrapped("Native contact diagnostic overlays are unavailable for Jolt.");
            ImGui::TreePop();
        }
}
void RigidBodySimulationApp::DrawObjectProperties()
{
    auto& collider=Rbs::colliderEditor;
        ImGui::TextWrapped("Selected: %zu / Apply edits the active object only", m_ColliderOutlines.count);
        if (m_ColliderOutlines.capacityReached)
            ImGui::TextWrapped("Selection limit reached (4096); selection unchanged.");
        if (auto pose = m_ActiveScene->ReadBodyTransform(m_HoveredEntity); pose)
        {
            ImGui::TextWrapped("Active: %s", m_HoveredEntity.GetName().c_str());
            Rbs::DrawColliderInspector(collider, *m_ActiveScene, *m_SceneResources,
                m_HoveredEntity, pose->body, m_IsPause);
            if (ImGui::TreeNode("Transform and motion"))
            {
            static std::string diagnostic;
            const auto report = [&](const auto& result) {
                diagnostic = result ? "Edit accepted" : std::format("Edit rejected (code {}). Previous state preserved.", int(result.error().code));
            };
            auto position = m_HoveredEntity.Transform().Translation;
            auto rotation = m_HoveredEntity.Transform().EulerRotation;
            auto scale = m_HoveredEntity.Transform().Scale;
            if (Rbs::Editor::XYZ("Teleport position", &position.x, 0.f, .05f))
                QueueTransformEdit(TransformTool::Translate, position);
            if (Rbs::Editor::XYZ("Teleport rotation (radians)", &rotation.x, 0.f, .01f))
                report(m_ActiveScene->SetLocalRotation(m_HoveredEntity, rotation));
            if (Rbs::Editor::XYZ("Transform scale", &scale.x, 1.f, .02f))
                QueueTransformEdit(TransformTool::Scale, scale);
            auto* world = m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            if (auto body = world->ReadBody(pose->body); body && pose->type != BodyType::Static)
            {
                auto linear = body->linearVelocity, angular = body->angularVelocity;
                bool changed = Rbs::Editor::XYZ("Linear velocity", &linear.x, 0.f, .05f);
                changed |= Rbs::Editor::XYZ("Angular velocity", &angular.x, 0.f, .02f);
                if (changed)
        {m_VelocityEdit=VelocityEdit{m_HoveredEntity,pose->body,linear,angular,m_SceneGeneration};m_PropertyDiagnostic="Velocity edit queued for Update.";}
            }
            if (!diagnostic.empty()) ImGui::TextWrapped("%s", diagnostic.c_str());
            ImGui::TreePop();
            }
        }
        else
        {
            Rbs::ResetColliderEditor(*m_ActiveScene, m_IsPause);
            ImGui::TextWrapped("Select a body to edit its collider.");
        }
}
void RigidBodySimulationApp::DrawEditorProperties()
{
    namespace E = Rbs::Editor;
    if (!m_HoveredEntity || !m_HoveredEntity.HasAllComponents<Transform3DComponent>())
    {
        ImGui::TextUnformatted("No entity selected");
        ImGui::TextWrapped("Select an entity in Hierarchy or click an object in the Viewport.");
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Ctrl+click adds to selection. Esc clears.");
        ImGui::PopStyleColor();
        return;
    }
    ImGui::TextWrapped("%s", m_HoveredEntity.GetName().c_str());
    DrawEntityPresentationProperties();
    const bool multiple=m_EditorSelection.count>1;
    const bool hasMesh=m_HoveredEntity.HasAllComponents<MeshRendererComponent>();
    const char* type=m_HoveredEntity.HasAllComponents<RenderCameraComponent>()?"Camera":
        m_HoveredEntity.HasAllComponents<RenderLightComponent>()?"Light":hasMesh?"Mesh":"Entity";
    ImGui::TextDisabled("%s | %zu selected",type,m_EditorSelection.count);
    if (m_ColliderOutlines.capacityReached) ImGui::TextWrapped("Selection limit reached (4096).");
    if(!m_PropertyDiagnostic.empty())ImGui::TextWrapped("%s",m_PropertyDiagnostic.c_str());
    if(m_EditorSelection.count>1)ImGui::TextWrapped("Group Transform: one atomic gesture. History: up to 32 commands / 8 MiB. Unsupported batches retain the full selection.");
    ImGui::Separator();
    ImGui::BeginDisabled(m_SceneRequest.has_value() || m_SimulationState == Rbs::SimulationState::Switching);
    if (E::Component("Transform"))
    {
        ImGui::BeginDisabled(m_EditorSelection.count>1);
        auto position = m_HoveredEntity.Transform().Translation;
        const bool componentPose=m_HoveredEntity.HasAnyComponents<RenderCameraComponent,RenderLightComponent>();
        // Authored lights may set the quaternion directly. Display the actual
        // local orientation used by RenderState, not a stale Euler convenience field.
        auto rotation = glm::degrees(componentPose?glm::eulerAngles(m_HoveredEntity.Transform().QuatRotation):m_HoveredEntity.Transform().EulerRotation);
        auto scale = m_HoveredEntity.Transform().Scale;
        if (E::XYZ("Translation (m)", &position.x, 0.f))
            QueueTransformEdit(TransformTool::Translate, position);
        if (E::XYZ("Rotation (deg)", &rotation.x, 0.f, .25f))
            QueueTransformEdit(TransformTool::Rotate, glm::radians(rotation));
        const bool componentScale=m_HoveredEntity.HasAnyComponents<RenderCameraComponent,RenderLightComponent>();
        const bool unsupportedScale=componentScale;
        ImGui::BeginDisabled(unsupportedScale);
        if (E::XYZ("Scale", &scale.x, 1.f, .02f, unsupportedScale ?
            (componentScale?"Camera/Light scale is not supported; edit projection size or light range/cones.":"Sphere/Capsule require uniform scale.") :
            "Reset an axis to 1. Scale edits use Scene validation; collider fitting remains explicit."))
            QueueTransformEdit(TransformTool::Scale, scale);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if(m_EditorSelection.count>1)ImGui::TextWrapped("Numeric Transform fields describe the active object. Use the toolbar gizmo for an atomic group gesture (up to 4096 roots; collider/query budgets apply).");
    }
    // Context editing remains single-object; no fabricated common material values.
    if(!multiple)DrawContextProperties();
    if(!multiple && hasMesh && E::Component("Geometry / Mesh",false))
    {
        const auto& mesh=m_HoveredEntity.GetComponent<MeshRendererComponent>();
        E::ReadOnly("Mesh / submesh",std::format("{} / {}",mesh.mesh.index,mesh.submesh).c_str());
        E::ReadOnly("Cast shadows",mesh.castShadows?"Enabled":"Disabled");
        E::ReadOnly("Receive shadows",mesh.receiveShadows?"Enabled":"Disabled");
        E::ReadOnly("Pickable",mesh.pickable?"Enabled":"Disabled");
        ImGui::TextWrapped("Scene Visible in the entity header affects editor visibility only. Mesh assignment is read-only here.");
    }
    const auto pose = m_ActiveScene->ReadBodyTransform(m_HoveredEntity);
    auto* world = m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    const auto body = pose ? world->ReadBody(pose->body) : PhysicsBodyResult<PhysicsBodyState>{
        std::unexpected(PhysicsBodyError{PhysicsBodyErrorCode::InvalidHandle})};
    if(multiple)DrawBulkBodyProperties();
    if (body && !multiple && E::Component("Rigidbody",false))
    {
        auto& edit=m_BodyPropertyEditor;
        const auto actual=world->ReadBodyProperties(pose->body);
        if(edit.target!=pose->body || edit.generation!=m_SceneGeneration)
        {
            edit={};edit.target=pose->body;edit.entity=m_HoveredEntity;edit.generation=m_SceneGeneration;
            if(actual)edit.draft=*actual;
        }
        else if(!edit.dirty && !edit.queued && actual)edit.draft=*actual;
        auto& p=edit.draft;
        ImGui::BeginDisabled(edit.queued);
        {E::PropertyRow row("Motion type","Changes preserve body identity; a joint must retain a Dynamic endpoint.");
            int type=int(p.type);if(ImGui::Combo("##Motion",&type,"Static\0Dynamic\0Kinematic\0")){p.type=BodyType(type);edit.dirty=true;}}
        edit.dirty |= E::Number("Mass (kg)",p.mass,.05f,"Positive authored mass. Recomputes inertia; retained while fixed.",E::Numeric::Positive<float>(),[](float v)->const char*{return std::isfinite(1.f/v)&&1.f/v>0?nullptr:"Mass must have a finite, positive inverse.";});
        ImGui::EndDisabled();
        auto linear = body->linearVelocity, angular = body->angularVelocity;
        ImGui::BeginDisabled(body->type == BodyType::Static);
        bool changed = E::XYZ("Velocity (m/s)", &linear.x, 0.f);
        changed |= E::XYZ("Angular (rad/s)", &angular.x, 0.f, .02f);
        if (changed)
        {m_VelocityEdit=VelocityEdit{m_HoveredEntity,pose->body,linear,angular,m_SceneGeneration};m_PropertyDiagnostic="Velocity edit queued for Update.";}
        ImGui::EndDisabled();
        ImGui::BeginDisabled(edit.queued);
        edit.dirty |= E::Number("Friction",p.material.friction,.01f,"Finite and nonnegative.",E::Numeric::Nonnegative<float>());
        edit.dirty |= E::Number("Restitution",p.material.restitution,.01f,"Range 0 to 1.",E::Numeric::Closed(0.f,1.f));
        const bool jolt=m_Backend==PhysicsBackend::Jolt;
        ImGui::BeginDisabled(!jolt);
        edit.dirty |= E::Number("Linear damping",p.linearDamping,.01f,"Jolt only; range 0 to 1.",E::Numeric::Closed(0.f,1.f));
        edit.dirty |= E::Number("Angular damping",p.angularDamping,.01f,"Jolt only; range 0 to 1.",E::Numeric::Closed(0.f,1.f));
        edit.dirty |= E::Number("Gravity factor",p.gravityFactor,.05f,"Jolt only; range 0 to 10.",E::Numeric::Closed(0.f,10.f));
        {E::PropertyRow row("CCD");edit.dirty |= ImGui::Checkbox("##CCD",&p.continuousCollision);}
        {E::PropertyRow row("Allow sleeping");edit.dirty |= ImGui::Checkbox("##Sleep",&p.allowSleeping);}
        if(ImGui::TreeNode("Solver overrides")) {
            ImGui::BeginDisabled(p.type!=BodyType::Dynamic);
            edit.dirty|=E::Number("Velocity override",p.velocityStepsOverride,1.f,"0 inherits world; 2..255. Maximum override can affect the entire contact/constraint island.",E::Numeric::Closed(0,255),[](int v)->const char*{return v==1?"Use 0 (world) or at least 2 so friction can operate.":nullptr;});
            edit.dirty|=E::Number("Position override",p.positionStepsOverride,1.f,"0 inherits world. Jolt uses the maximum across the island, not an isolated per-body solver.",E::Numeric::Closed(0,255));
            ImGui::EndDisabled();
            ImGui::TextWrapped("Experimental. Public Jolt MotionProperties setters; safe boundary, no body rebuild. Dynamic colliding bodies only. May change the whole island.");
            if(actual)ImGui::TextWrapped("Effective overrides: %d / %d. Jolt native: 0 / 0 (inherit).",actual->velocityStepsOverride,actual->positionStepsOverride);
            ImGui::TreePop();
        }
        ImGui::EndDisabled();
        if(!jolt)ImGui::TextWrapped("Native: damping 0, gravity factor 1, CCD and sleep policy are fixed. Sleep thresholds are editable below.");
        if(ImGui::TreeNode("Sleep thresholds"))
        {
            ImGui::BeginDisabled(jolt);
            edit.dirty |= E::Number("Linear (m/s)",p.sleep.linearSpeedThreshold,.01f,"Finite, nonnegative Native threshold.",E::Numeric::Nonnegative<float>());
            edit.dirty |= E::Number("Angular (rad/s)",p.sleep.angularSpeedThreshold,.01f,"Finite, nonnegative Native threshold.",E::Numeric::Nonnegative<float>());
            edit.dirty |= E::Number("Dwell (s)",p.sleep.inactivitySeconds,.01f,"Finite, positive Native dwell time.",E::Numeric::Positive<double>());
            ImGui::EndDisabled();
            if(jolt)ImGui::TextWrapped("Jolt world thresholds are under World Settings > Physics > Sleeping. These Native per-body thresholds cannot be mapped.");
            ImGui::TreePop();
        }
        ImGui::BeginDisabled(!edit.dirty || E::Numeric::HasError());
        if(ImGui::Button("Apply Rigidbody"))edit.queued=true;
        ImGui::EndDisabled();
        if(ImGui::GetContentRegionAvail().x>E::Px(280))ImGui::SameLine();
        if(ImGui::Button("Revert Rigidbody") && actual){p=*actual;edit.dirty=false;E::Numeric::Reset();}
        E::Tooltip("Discard this draft and read the active body. Reset restores preset defaults. Backend Apply preserves supported applied edits.");
        const auto baseline=std::find_if(m_JoltSettingsEditor.bodyBaseline.begin(),m_JoltSettingsEditor.bodyBaseline.end(),[&](const auto& b){return b.first==pose->body;});
        ImGui::BeginDisabled(baseline==m_JoltSettingsEditor.bodyBaseline.end());
        if(ImGui::Button("Reset to Baseline##Body")){p=baseline->second;edit.dirty=true;E::Numeric::Reset();}
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        E::Tooltip("Stage the preserved world-entry body settings; Apply is still required. Main Reset restores authored preset defaults.");
        ImGui::TextWrapped("%s",edit.queued?"Apply queued for simulation boundary.":edit.dirty?"Unapplied draft":"Active body settings");
        if (ImGui::TreeNodeEx("Material details",0,"Contact resistance"))
        {
            E::ReadOnly("Spin length (m)", std::format("{:.3f}", body->material.spinResistanceLength).c_str());
            E::ReadOnly("Rolling length (m)", std::format("{:.3f}", body->material.rollingResistanceLength).c_str());
            if (m_Backend == PhysicsBackend::Jolt)
                ImGui::TextWrapped("Spin and rolling resistance lengths are unsupported by Jolt. Authored values are retained.");
            ImGui::TreePop();
        }
    }
    if (pose && !multiple && E::Component("Collider", false))
        Rbs::DrawColliderInspector(Rbs::colliderEditor, *m_ActiveScene, *m_SceneResources,
            m_HoveredEntity, pose->body, m_IsPause);
    if (pose && !multiple)
    {
        bool foundJoint=false,showJoints=false;
        for (std::size_t i = 0; i < world->GetJointCount(); ++i)
        {
            const auto handle = world->JointAt(i);
            if (!handle) continue;
            const auto joint = world->ReadDistanceJoint(*handle);
            if (!joint || (joint->descriptor.bodyA != pose->body && joint->descriptor.bodyB != pose->body)) continue;
            if(!foundJoint){showJoints=E::Component("Constraints / Joints",false);foundJoint=true;}
            if(!showJoints)break;
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Distance Joint %zu",i+1);
            {
                const auto& desc = joint->descriptor;
                E::ReadOnly("Endpoints", std::format("Body {} / Body {}", desc.bodyA.index, desc.bodyB.index).c_str());
                E::ReadOnly("Length (m)", std::format("{:.3f}", desc.length).c_str());
                auto a = desc.localAnchorA, b = desc.localAnchorB;
                ImGui::BeginDisabled();
                E::XYZ("Anchor A (m)", &a.x, 0.f, .01f, "Read-only: the current joint API has no live descriptor update operation.");
                E::XYZ("Anchor B (m)", &b.x, 0.f, .01f, "Read-only: the current joint API has no live descriptor update operation.");
                ImGui::EndDisabled();
                E::ReadOnly("Connected collision", desc.collideConnected ? "Enabled" : "Disabled");
                E::ReadOnly("Length error (m)", std::format("{:.5f}", joint->error).c_str());
                ImGui::TextWrapped("Read-only joint descriptor. Change the scene preset to load another configuration.");
            }
            ImGui::PopID();
        }
    }
    if(body || multiple)DrawImpulseProperties();
    if(E::Component("Advanced / Diagnostics",false))
    {
        if(multiple)ImGui::TextWrapped("Identifiers below describe the active object, not a common value.");
        if(const auto id=m_ActiveScene->RenderData().Identify(m_HoveredEntity))
            E::ReadOnly("Entity identity",std::format("{} / {} / {}",id->registry,id->index,id->generation).c_str());
        E::ReadOnly("Scene generation",std::format("{}",m_SceneGeneration).c_str());
        if(pose)E::ReadOnly("Body identity",std::format("{} / {}",pose->body.index,pose->body.generation).c_str());
        if(body && !multiple)
        {
            const bool jolt=m_Backend==PhysicsBackend::Jolt;
            const auto actual=world->ReadBodyProperties(pose->body);
            if(jolt && ImGui::TreeNode("Effective / SDK body defaults")) {
                if(actual)ImGui::TextWrapped("Friction %.6g / .2 | Restitution %.6g / 0 | Damping %.6g, %.6g / .05, .05 | Gravity %.6g / 1 | CCD %s / Discrete | Sleep %s / ON",actual->material.friction,actual->material.restitution,actual->linearDamping,actual->angularDamping,actual->gravityFactor,actual->continuousCollision?"LinearCast":"Discrete",actual->allowSleeping?"ON":"OFF");
                ImGui::TextWrapped("SDK defaults are reference values, not GEngine's preserved authored baseline. GEngine uses product material combination.");ImGui::TreePop();
            }
            if(jolt && ImGui::TreeNode("Contact coefficients")) {
                ImGui::TextWrapped("GEngine product rule: friction A*B, restitution A*B. Minimum restitution velocity is a World setting; it can suppress restitution.");
                const auto diagnostics=world->ReadJoltDiagnostics();bool found=false;
                if(diagnostics && diagnostics->enabled)for(const auto& contact:diagnostics->contacts)
                    if(contact.bodyA==pose->body || contact.bodyB==pose->body){
                        ImGui::TextWrapped("Last step pair: friction %.6g, restitution %.6g. Normal velocity %.4g -> %.4g m/s (callback -> end of full update).",contact.friction,contact.restitution,contact.normalVelocityBefore,contact.normalVelocityAfter);found=true;break;}
                if(!found)ImGui::TextWrapped("No current observed contact. Enable read-only contact sampling in World Settings > Jolt Physics > Advanced Diagnostics.");
                ImGui::TreePop();
            }
        }
    }
    ImGui::EndDisabled();
    if (!m_PropertyDiagnostic.empty()) ImGui::TextWrapped("%s", m_PropertyDiagnostic.c_str());
}

void RigidBodySimulationApp::DrawPresentationSettings()
{
        if (ImGui::TreeNode("Presentation (Developer)"))
        {
            bool interpolate = m_ActiveScene->IsRenderInterpolationEnabled();
            if (ImGui::Checkbox("Interpolate presentation", &interpolate))
                m_ActiveScene->SetRenderInterpolationEnabled(interpolate);
            ImGui::TextUnformatted("Gold: mesh / Cyan: Physics / Magenta: presentation");
            ImGui::TreePop();
        }

}

void RigidBodySimulationApp::ImGuiRender()
{
    RBS_REVIEW_SCOPE(0);
    GetInputManager()->BeginUIRouting();
#ifdef GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS
    Rbs::DistanceChainImpulseChecks::OnUI(*this);
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::PickTraceUIBegin();
#endif
    if (SandboxEnabled())
    {
        Rbs::Editor::BeginFrame();
        PrepareEditorLayout();
    }
    if (auto host = UI::BeginDockspaceHost({"DockSpace Demo", "MyDockSpace"}); !host)
    {
        if (SandboxEnabled()) Rbs::Editor::EndFrame();
        ReportPlatformError(host.error());
        m_Running = false;
        return;
    }
    PrepareColliderUi();
    if(SandboxEnabled())
    {
        DrawHierarchyRegion();
        DrawInspectorRegion();
        DrawDiagnosticsRegion();
    }
    else DrawLegacyDiagnosticPanels();
    Rbs::SyncColliderEditors(*m_ActiveScene,*m_SceneResources,m_ColliderOutlines,m_HoveredEntity,m_IsPause);
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if(ImGui::GetFrameCount()==4)ImGui::LogToFile(-1,"scene-ui-text.log");
#endif
    DrawMenus();
#if defined(GENGINE_INPUT_VALIDATION) && !defined(GENGINE_INPUT_CONTROL)
    PreEditorInput::DrawInputFixture();
#endif
#if defined(GENGINE_LIFECYCLE_VALIDATION) && !defined(GENGINE_INPUT_VALIDATION)
    if(PreEditorLifecycle::UIActive())PreEditorInput::DrawInputFixture();
#endif
    DrawViewport();
    if(SandboxEnabled())DrawCameraPanels();
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if(ImGui::GetFrameCount()==4)ImGui::LogFinish();
#endif
    if(m_Launch.m_BackendSwitchChecks)Rbs::BackendSwitchChecks::InspectLayout(*this);
    UI::EndDockspaceHost();
    PublishEditorInputRouting();
    if (SandboxEnabled()) Rbs::Editor::EndFrame();
}

void RigidBodySimulationApp::PublishEditorInputRouting()
{
    // Publish one completed UI descriptor for the next input poll. The viewport
    // background is a View surface; every active widget/menu/popup has UI priority.
    const auto& io = ImGui::GetIO();
    const auto origin = ImGui::GetMainViewport()->Pos;
    const bool popup =
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool viewGesture = GetInputManager()->PointerCapture() == InputLayer::View;
    auto& uiContext = *ImGui::GetCurrentContext();
    // Consumption follows the owned pointer gesture, even if Alt belonged to a
    // different layer before activation. Let ImGui retire its own queued release.
    if (m_ViewportAltGestureConsumed)
    {
        uiContext.NavWindowingToggleLayer = false;
        bool queuedAlt = false;
        for (const auto& queued : uiContext.InputEventsQueue)
            queuedAlt |= queued.Type == ImGuiInputEventType_Key &&
                (queued.Key.Key == ImGuiKey_ModAlt || queued.Key.Key == ImGuiKey_LeftAlt ||
                 queued.Key.Key == ImGuiKey_RightAlt);
        if (!io.KeyAlt && !(GetInputManager()->GetPhysicalState().modifiers & LeftAlt) && !queuedAlt)
            m_ViewportAltGestureConsumed = false;
    }
    const auto* viewportWindow = ImGui::FindWindowByName("Viewport");
    // ImGui retains a MoveId for a background click even when title-bar-only
    // movement prevents a drag. It is not an active widget taking this gesture.
    const bool viewBackground =
        viewportWindow && uiContext.ActiveIdWindow == viewportWindow &&
        uiContext.ActiveId == viewportWindow->MoveId && !uiContext.MovingWindow;
    const bool active = ImGui::IsAnyItemActive() && !viewBackground;
    const bool text = io.WantTextInput;
    const bool visible = HasVisibleViewport() && m_ViewportBounds[1].x > m_ViewportBounds[0].x &&
                         m_ViewportBounds[1].y > m_ViewportBounds[0].y;
    const unsigned navWindow = uiContext.NavWindow ? uiContext.NavWindow->ID : 0;
    const bool navTransferred = navWindow != m_ViewportNavWindow && navWindow &&
        viewportWindow && uiContext.NavWindow != viewportWindow;
    // Viewport-owned controls temporarily consume pointer/keyboard input without
    // retiring activation. Closing their popup must not cost the next scene click.
    const bool navInViewport=viewportWindow && uiContext.NavWindow &&
        ImGui::IsWindowChildOf(uiContext.NavWindow,const_cast<ImGuiWindow*>(viewportWindow),true,false);
    const bool overlay = navInViewport && (m_ViewportOverlayInteracting || m_ViewportPopupOpen || m_GizmoUsing);
    if(visible && overlay && (m_ViewportOverlayInteracting || m_GizmoUsing))
    {SetViewportActive(true,false);m_ViewportRequestFocus=false;}
    if (!visible || (!overlay && (active || popup || text || navTransferred)))
        SetViewportActive(false);
    m_ViewportNavWindow = navWindow;
    // Reuse Hierarchy's one-shot activation+selection handoff after finishing a
    // numeric edit. Active text, popup, drag/drop and Alt/camera routing still gate
    // delivery; this does not forward the originating UI gesture into the View.
    if(SandboxEnabled() && Rbs::Editor::Numeric::TakeSelectionHandoff())
        m_HierarchySelectionHandoff=true;
    const InputRouting routing{
        GetWindow()->GetWindowID(),
        m_ViewportBounds[0].x - origin.x,
        m_ViewportBounds[0].y - origin.y,
        m_ViewportBounds[1].x - origin.x,
        m_ViewportBounds[1].y - origin.y,
        visible,
        m_ViewportActive,
        active || popup || text,
        active || popup || text || (io.WantCaptureKeyboard && !m_ViewportActive)};
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    const bool tracing = PreEditorLifecycle::PickTraceActive();
    unsigned traceFlags{}, traceMoveId{};
    if (tracing)
    {
        traceFlags = unsigned(active) | (unsigned(popup) << 1) | (unsigned(text) << 2) |
            (unsigned(viewBackground) << 3) | (unsigned(viewGesture) << 4) | (unsigned(visible) << 5) |
            (unsigned(m_ViewportForcused) << 6) | (unsigned(m_ViewportHovered) << 7);
        traceMoveId = viewportWindow ? viewportWindow->MoveId : 0;
        PreEditorLifecycle::PickTraceUI("ui_before", routing, -1, traceFlags, traceMoveId);
    }
    auto published = GetInputManager()->PublishRouting(routing);
    if (tracing) PreEditorLifecycle::PickTraceUI("ui_after", routing, bool(published), traceFlags, traceMoveId);
    if (!published)
#else
    if (auto published = GetInputManager()->PublishRouting(routing); !published)
#endif
        Log::GetCoreLogger()->error("RBS input routing rejected: {}", int(published.error()));
    // Paint only after this frame's UI transfers/cancellation have resolved.
    // Activation is authoritative; hover, capture and animation never own focus.
    const ImVec2 imageMin{m_ViewportBounds[0].x, m_ViewportBounds[0].y};
    const ImVec2 imageMax{m_ViewportBounds[1].x, m_ViewportBounds[1].y};
    const ImVec2 clipMin = viewportWindow ? ImMax(imageMin, viewportWindow->InnerClipRect.Min) : imageMin;
    const ImVec2 clipMax = viewportWindow ? ImMin(imageMax, viewportWindow->InnerClipRect.Max) : imageMin;
    // ImGui's renderer applies DisplayFramebufferScale to every draw list. Use
    // that exact transform, then apply monitor DPI once to the physical widths.
    const ImVec2 pixelScale = io.DisplayFramebufferScale;
    const float dpi = viewportWindow ? viewportWindow->Viewport->DpiScale : 1.f;
    const bool drawable = visible && m_CameraAvailable && viewportWindow &&
        viewportWindow->Active && !viewportWindow->Hidden &&
        std::isfinite(dpi) && dpi > 0.f &&
        std::isfinite(pixelScale.x) && pixelScale.x > 0.f &&
        std::isfinite(pixelScale.y) && pixelScale.y > 0.f &&
        clipMax.x > clipMin.x && clipMax.y > clipMin.y &&
        imageMax.x - imageMin.x > 3.f && imageMax.y - imageMin.y > 3.f;
    auto* borderDraw = viewportWindow ? viewportWindow->DrawList : nullptr;
#ifdef GENGINE_LIFECYCLE_VALIDATION
    const int firstBorderVertex = borderDraw ? borderDraw->VtxBuffer.Size : 0;
    const int firstBorderIndex = borderDraw ? borderDraw->IdxBuffer.Size : 0;
#endif
    float flashOpacity = 0.f, borderOpacity = 0.f;
    if (drawable)
    {
        // Put the complete ring inside the visible image, including the docked
        // window's inner clip. Filled strips avoid a centered stroke's clipped
        // half-width/AA fringe. Snap inward in the final renderer's pixel space.
        const auto displayOrigin = viewportWindow->Viewport->Pos;
        const ImVec2 minimum{
            displayOrigin.x + std::ceil((clipMin.x - displayOrigin.x) * pixelScale.x) / pixelScale.x,
            displayOrigin.y + std::ceil((clipMin.y - displayOrigin.y) * pixelScale.y) / pixelScale.y};
        const ImVec2 maximum{
            displayOrigin.x + std::floor((clipMax.x - displayOrigin.x) * pixelScale.x) / pixelScale.x,
            displayOrigin.y + std::floor((clipMax.y - displayOrigin.y) * pixelScale.y) / pixelScale.y};
        const auto ring = [&](float pixelsAt1x, ImU32 color)
        {
            if (maximum.x <= minimum.x || maximum.y <= minimum.y) return;
            const float pixels = (std::max)(1.f, std::round(pixelsAt1x * dpi));
            const float x = (std::min)(pixels / pixelScale.x, (maximum.x - minimum.x) * .5f);
            const float y = (std::min)(pixels / pixelScale.y, (maximum.y - minimum.y) * .5f);
            borderDraw->AddRectFilled(minimum, {maximum.x, minimum.y + y}, color);
            borderDraw->AddRectFilled({minimum.x, maximum.y - y}, maximum, color);
            borderDraw->AddRectFilled({minimum.x, minimum.y + y}, {minimum.x + x, maximum.y - y}, color);
            borderDraw->AddRectFilled({maximum.x - x, minimum.y + y}, {maximum.x, maximum.y - y}, color);
        };
        borderDraw->PushClipRect(clipMin, clipMax, false);
        // Opaque state indication is independent of the clock and Style.Alpha.
        // These are the owner's explicit palette, not the dark HeaderActive.
        borderOpacity = 1.f;
        ring(m_ViewportActive ? 2.f : 1.f,
             m_ViewportActive ? IM_COL32(56, 189, 248, 255) : IM_COL32(71, 85, 105, 255));
        if (m_ViewportActive && m_ViewportHighlightStart >= 0)
        {
            const float remaining = std::clamp(float(1.0 -
                (ImGui::GetTime() - m_ViewportHighlightStart) / .25), 0.f, 1.f);
            flashOpacity = remaining * remaining * (3.f - 2.f * remaining);
            // Only the extra emphasis fades; the 2px focused ring stays intact.
            ring(4.f, IM_COL32(125, 211, 252, int(std::round(255.f * flashOpacity))));
        }
        borderDraw->PopClipRect();
    }
#ifdef GENGINE_LIFECYCLE_VALIDATION
    m_ViewportHighlightOpacity = flashOpacity;
    if (!PreEditorLifecycle::ObserveViewportFocus(m_ViewportActive, drawable, m_ViewportActivations,
            m_ViewportHighlightStart, flashOpacity, borderOpacity, borderDraw,
            firstBorderVertex, firstBorderIndex,
            {imageMin.x, imageMin.y, imageMax.x, imageMax.y},
            {clipMin.x, clipMin.y, clipMax.x, clipMax.y}))
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Viewport focus border", {},
            "DrawList observation", {}, "Persistent border/activation/clip oracle failed"});
#endif
#ifdef GENGINE_RBS_MODULARITY_VALIDATION
    ValidatePacketAUi();
#endif
#ifdef GENGINE_MESH_HIGHLIGHT_AUDIT
    if (std::getenv("GENGINE_ORDINARY_HIGHLIGHT_OUTPUT")) Rbs::AuditHighlightUiCallback();
#endif
}


void RigidBodySimulationApp::DrawShaderPanel()
{
    ImGui::Begin("Custom shader reload");
    PublishPanelInputRegion();
    DrawShaderSection();
    ImGui::End();
}

void RigidBodySimulationApp::DrawShaderSection()
{
    if (m_ShaderReloadGallery.material)
    {
        ImGui::TextWrapped("Both cubes share a shader; the clone keeps its lower brightness.");
        if (ImGui::Button("Focus custom cubes") && m_CameraView == CameraView::Editor)
            FocusMaterialCamera(m_EditorCamera_, {1.5f, 5, -22}, .2f, 0, 12.f);
        if (ImGui::Button("Reload color edit"))
            m_ShaderReloadGallery.request = 1;
        if (ImGui::Button("Restore original"))
            m_ShaderReloadGallery.request = 2;
        if (ImGui::Button("Try invalid shader"))
            m_ShaderReloadGallery.request = 3;
        if (ImGui::Button("Try incompatible schema"))
            m_ShaderReloadGallery.request = 4;
        if (ImGui::Button("Prepare edit"))
            m_ShaderReloadGallery.request = 5;
        if (ImGui::Button("Commit prepared"))
            m_ShaderReloadGallery.request = 6;
        if (ImGui::Button("Cancel prepared"))
            m_ShaderReloadGallery.request = 7;
        ImGui::TextWrapped("%s", m_ShaderReloadGallery.message.c_str());
    }
    else
        ImGui::TextWrapped("No shader target. Load GeometryGallery from the toolbar.");
}


void RigidBodySimulationApp::DrawMaterialPanel()
{
    ImGui::Begin("Material authoring");
    PublishPanelInputRegion();
    DrawMaterialSection();
    ImGui::End();
}

void RigidBodySimulationApp::DrawMaterialSection()
{
    if (m_MaterialGallery.uvMaterial)
    {
        ImGui::TextWrapped("Plane / Cube / Sphere / Capsule / Torus / Diamond");
        ImGui::TextWrapped("Same floor: UV front row, Triplanar second row");
        if (ImGui::Button("Focus material comparison") && m_CameraView == CameraView::Editor)
        {
            FocusMaterialCamera(m_EditorCamera_, {0, 2, -22}, .35f, 0, 38.f);
        }
        ImGui::Checkbox("Animate object transforms", &m_MaterialGallery.animate);
        constexpr const char* geometryNames[]{"Plane",   "Cube",  "Sphere",
                                              "Capsule", "Torus", "Diamond"};
        ImGui::Combo("Inspect geometry", &m_MaterialGallery.inspectedGeometry, geometryNames, 6);
        if (ImGui::Button("Focus selected geometry") && m_CameraView == CameraView::Editor)
        {
            FocusMaterialCamera(m_EditorCamera_,
                                {-10.f + 4.f * m_MaterialGallery.inspectedGeometry, 2.f, -20.f},
                                .35f, .65f, 7.f);
        }
        const char* comparisons[]{"Paired rows", "UV at reference positions",
                                  "Triplanar at reference positions"};
        if (ImGui::Combo("Comparison", &m_MaterialGallery.comparison, comparisons, 3))
            SetMaterialComparison(m_MaterialGallery.comparison);
        if (auto desc = m_SceneResources->DescribeMaterial(m_MaterialGallery.triplanarMaterial);
            desc)
        {
            bool changed = Rbs::Editor::Numeric::Scalar("Projection repeats / local unit",desc->projectionScale.value,.01f,Rbs::Editor::Numeric::Closed(.001f,1024.f));
            changed |= Rbs::Editor::Numeric::Scalar("Blend sharpness",desc->blendSharpness.value,.01f,Rbs::Editor::Numeric::Closed(1.f,8.f),Rbs::Editor::Numeric::Accept{},true);
            changed |= Rbs::Editor::Numeric::Scalar("Normal strength",desc->normalStrength,.01f,Rbs::Editor::Numeric::Closed(0.f,2.f),Rbs::Editor::Numeric::Accept{},true);
            if (changed)
                if (auto edited =
                        m_SceneResources->EditMaterial(m_MaterialGallery.triplanarMaterial, *desc);
                    !edited)
                    Log::GetCoreLogger()->error("{}", DescribeSceneResourceError(edited.error()));
        }
    }
    else
        ImGui::TextWrapped("No material target. Load GeometryGallery from the toolbar.");
}


void RigidBodySimulationApp::DrawAsyncMenu()
{
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (ImGui::GetFrameCount() == 3 || ImGui::GetFrameCount() == 4)
        ImGui::OpenPopup("Async box texture");
#endif
    if (ImGui::BeginMenu("Async box texture", !m_AsyncTextureGallery.clone || m_WoodSettled))
    {
        if (!m_AsyncTextureGallery.clone)
            ImGui::TextDisabled(
                "No async demo target in this scene. Select GeometryGallery at launch.");
        else
        {
            for (const auto* path :
                 {"Sphere/wood_diffuse", "Plane/wood_diffuse", "missing-phase12-image"})
                if (ImGui::MenuItem(path))
                {
                    m_AsyncTextureGallery.source = path;
                    m_AsyncTextureGallery.request = true;
                }
            if (ImGui::MenuItem("Cancel pending request", nullptr, false,
                                m_AsyncTextureGallery.pending.has_value()))
                m_AsyncTextureGallery.cancel = true;
            ImGui::TextWrapped("%s", m_AsyncTextureGallery.message.c_str());
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Load barrel mesh", nullptr, false,
                        m_ActiveSelection.scene == Rbs::RbsScenePreset::GeometryGallery && m_WoodSettled &&
                            !m_LoadBarrel))
        m_LoadBarrel = true;
}

void RigidBodySimulationApp::DrawMenus()
{
    if (ImGui::BeginMenuBar())
    {
#ifdef GENGINE_RBS_SCENE_VALIDATION
        if (ImGui::GetFrameCount() == 3 || ImGui::GetFrameCount() == 4)
            ImGui::OpenPopup("File");
#endif
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
        const auto filePoint = ImGui::GetCursorScreenPos();
#endif
        const bool fileOpen = ImGui::BeginMenu("File");
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
        PreEditorInput::NoteUIMenu(3, fileOpen, filePoint.x, filePoint.y,
                                   ImGui::CalcTextSize("File").x +
                                       2 * ImGui::GetStyle().ItemSpacing.x,
                                   ImGui::GetFrameHeight());
#endif
        if (fileOpen)
        {
            DrawAsyncMenu();
            // Disabling fullscreen would allow the window to be moved to the front of other windows,
            // which we can't undo at the moment without finer window depth/z control.
            if (ImGui::MenuItem("Exit"))
            {
                ShutDown();
            }

            ImGui::EndMenu();
        }

#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
        const auto shadowPoint = ImGui::GetCursorScreenPos();
#endif
        const bool shadowsOpen = ImGui::BeginMenu("Shadows");
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
        PreEditorInput::NoteUIMenu(0, shadowsOpen, shadowPoint.x, shadowPoint.y,
                                   ImGui::CalcTextSize("Shadows").x +
                                       2 * ImGui::GetStyle().ItemSpacing.x,
                                   ImGui::GetFrameHeight());
#endif
        if (shadowsOpen)
        {
            auto request = GetShadowQuality().requested;
            int tier = static_cast<int>(request.quality);
            bool changed =
                ImGui::Combo("Quality", &tier, "Low (1024)\0Medium (2048)\0High (4096)\0Custom\0");
            request.quality = static_cast<ShadowQuality>(tier);
            if (request.quality == ShadowQuality::Custom)
            {
                int resolution = static_cast<int>(request.customResolution);
                if (Rbs::Editor::Numeric::Scalar("Resolution",resolution,64.f,Rbs::Editor::Numeric::Closed(1,8192)))
                {
                    request.customResolution =
                        static_cast<unsigned>(resolution);
                    changed = true;
                }
            }
            std::uint64_t budget = request.byteBudget / (1024 * 1024);
            if (Rbs::Editor::Numeric::Scalar("Depth budget (MiB)",budget,48.f,Rbs::Editor::Numeric::Closed(std::uint64_t{1},std::numeric_limits<std::uint64_t>::max()/(1024*1024))))
            {
                request.byteBudget = budget * 1024 * 1024;
                changed = true;
            }
            bool fallback = request.fallback == ShadowFallback::LowerTiers;
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
            PreEditorInput::NoteUIItem(2);
#endif
            if (ImGui::Checkbox("Allow lower quality on allocation failure", &fallback))
            {
                request.fallback = fallback ? ShadowFallback::LowerTiers : ShadowFallback::None;
                changed = true;
            }
            if (changed)
                if (auto queued = RequestShadowQuality(request); !queued)
                    ReportFramebufferError("shadow quality request", queued.error());
            const auto& current = GetShadowQuality();
            ImGui::Text("Active: %s (%u x %u)", ShadowQualityLabel(current.effective).data(),
                        current.resolution, current.resolution);
            ImGui::Text("Estimated / allocated depth: %.1f / %.1f MiB",
                        double(current.memory.estimatedBytes) / (1024 * 1024),
                        double(current.memory.allocatedDepthBytes) / (1024 * 1024));
            ImGui::TextDisabled("Driver overhead is not included.");
            if (ShadowQualityPending())
                ImGui::TextUnformatted("Quality change pending");
            if (current.fallbackReason)
                ImGui::TextWrapped("Lower tier selected: %s", current.fallbackReason->message);
            if (GetShadowQualityError())
                ImGui::TextWrapped("Previous quality retained: %s",
                                   GetShadowQualityError()->message);
            ImGui::EndMenu();
        }
        DrawCameraMenu();
        if(SandboxEnabled())DrawEditorToolbar();
        ImGui::EndMenuBar();
    }
}

void RigidBodySimulationApp::DrawCameraMenu()
{
    if (!ImGui::BeginMenu("Cameras")) return;
    if (ImGui::MenuItem("Editor view", nullptr, m_CameraView == CameraView::Editor))
        (void)SetCameraView(CameraView::Editor);
    if (ImGui::MenuItem("Active Scene view", nullptr, m_CameraView == CameraView::ActiveScene))
        (void)SetCameraView(CameraView::ActiveScene);
    if (ImGui::MenuItem("End preview", nullptr, false, m_CameraView == CameraView::Preview))
        (void)EndCameraPreview();
    ImGui::Separator();
    auto active = m_ActiveScene->ActiveSceneCamera();
    for (std::size_t slot = 0; slot != m_SceneCameras.size(); ++slot)
    {
        const auto id = m_SceneCameras[slot];
        const auto entity = m_ActiveScene->RenderData().Resolve(id);
        const bool live = bool(entity);
        const auto label = std::format("{}###SceneCamera{}",live ? _Entity{*entity,m_ActiveScene.get()}.Name() : "Missing Scene Camera",slot);
        if (!ImGui::BeginMenu(label.c_str())) continue;
        if (ImGui::MenuItem("Select active", nullptr, active && *active && **active == id, live))
            (void)SelectSceneCamera(id);
        if (ImGui::MenuItem("Preview", nullptr,
                            m_CameraView == CameraView::Preview && m_PreviewCamera == id, live))
            (void)SetCameraView(CameraView::Preview, id);
        if (ImGui::MenuItem("Replace with fresh identity", nullptr, false, live))
            (void)RetireSceneCamera(slot, true);
        if (ImGui::MenuItem("Delete", nullptr, false, live))
            (void)RetireSceneCamera(slot, false);
        if (ImGui::MenuItem("Create from Editor pose", nullptr, false, !live))
            if (auto created = CreateSceneCamera(slot); !created)
                m_CameraDiagnostic = "Camera creation failed";
        ImGui::EndMenu();
    }
    if (!m_CameraDiagnostic.empty()) ImGui::TextWrapped("%s", m_CameraDiagnostic.c_str());
    if (!m_CameraUnavailableDiagnostic.empty())
        ImGui::TextWrapped("%s", m_CameraUnavailableDiagnostic.c_str());
    ImGui::EndMenu();
}

void RigidBodySimulationApp::DrawViewport()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    //ImGui::ShowDemoWindow(&p_open);
    // A deliberate activation click requests focus once. Hover/animation never do.
    if (m_ViewportRequestFocus)
    {
        ImGui::SetNextWindowFocus();
        m_ViewportRequestFocus = false;
    }
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
    if (std::getenv("GENGINE_COLLIDER_SELECTION_SMOKE_OUTPUT"))
    {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos({10, 35}, ImGuiCond_Always);
        const bool meshResize=std::getenv("GENGINE_MESH_HIGHLIGHT_OUTPUT")&&Rbs::MeshHighlightStage()==10;
        ImGui::SetNextWindowSize(meshResize?ImVec2{720,504}:ImVec2{840,650}, ImGuiCond_Always);
    }
#endif
    const bool viewportVisible = ImGui::Begin("Viewport",nullptr,SandboxEnabled() ? ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse : 0);
    auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
    auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
    auto viewportOffset = ImGui::GetWindowPos();
    m_ViewportBounds[0] = {viewportMinRegion.x + viewportOffset.x,
                           viewportMinRegion.y + viewportOffset.y};
    m_ViewportBounds[1] = {viewportMaxRegion.x + viewportOffset.x,
                           viewportMaxRegion.y + viewportOffset.y};
    //GENGINE_CORE_INFO("{0}, {1}", viewportOffset.x, viewportOffset.y);

    m_ViewportForcused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();
    if(SandboxEnabled() && ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_F2,false))
        if(auto id=m_ActiveScene->RenderData().Identify(m_HoveredEntity))BeginEntityRename(*id);

    //GENGINE_INFO("Focused: {}", ImGui::IsWindowFocused());
    //GENGINE_INFO("Hovered: {}", ImGui::IsWindowHovered());

    auto viewportPanelSize = ImGui::GetContentRegionAvail();
    const bool hasArea = viewportVisible && viewportPanelSize.x > 0 && viewportPanelSize.y > 0;
    auto scale = hasArea ? UI::CurrentViewportFramebufferScale()
                         : std::expected<FramebufferScale, PlatformError>(FramebufferScale{});
    if (scale)
    {
        if (auto sized = SetEditorViewport(
                hasArea ? EditorViewportLogicalSize{viewportPanelSize.x, viewportPanelSize.y}
                        : EditorViewportLogicalSize{},
                *scale);
            !sized)
            ReportPlatformError(sized.error());
    }
    else
        ReportPlatformError(scale.error());

    ObserveViewport(scale);

    //m_ViewportSize = {1280, 720};
    //m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    //if (m_RenderTarget && m_RenderTarget->IsMultiSampled())
    //m_RenderTarget->BindAndBlitToScreen(0);

    //ImGui::Image(reinterpret_cast<void*>(m_FinalFrameBuffer->GetColorMap()), { m_ViewportSize.x, m_ViewportSize.y }, { 0,1 }, { 1, 0 });
    if (HasVisibleViewport() && m_CameraAvailable)
    {
        if (auto image = UI::FramebufferImage(*m_RenderTarget, m_ViewportSize.x, m_ViewportSize.y);
            !image)
            ReportFramebufferError("viewport image", image.error());
        else
        {
            if(SandboxEnabled() && ImGui::BeginDragDropTarget())
            {
                if(const auto* payload=ImGui::AcceptDragDropPayload(Rbs::ScenePresetPayloadType))
                {
                    if(const auto selection=Rbs::DecodeScenePreset(payload->Data,std::size_t(payload->DataSize)))
                        (void)RequestScene(*selection); // Only queues; Update owns transactional rebuild.
                    else m_SceneDiagnostic="Invalid preset drop rejected; scene retained.";
                }
                ImGui::EndDragDropTarget();
            }
            const auto minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
            m_ViewportBounds[0] = {minimum.x, minimum.y};
            m_ViewportBounds[1] = {maximum.x, maximum.y};
        }
    }
    if(SandboxEnabled() && HasVisibleViewport()) {
        // Reserve toolbar input first, then composite its existing draw commands
        // above world gizmos even when responsive layout stacks the groups.
        auto* draw=ImGui::GetWindowDrawList();draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);DrawViewportOverlay();
        draw->ChannelsSetCurrent(0);DrawViewportGizmo();
        DrawImpulseOverlay();DrawComponentOverlays();DrawViewportMarquee();
        draw->ChannelsSetCurrent(1);DrawGroupTransformActions();draw->ChannelsMerge();
    }
    if (!m_CameraAvailable && !m_CameraUnavailableDiagnostic.empty())
        ImGui::TextWrapped("%s", m_CameraUnavailableDiagnostic.c_str());
    if (m_ViewportHighlightPending)
    {
        m_ViewportHighlightStart = ImGui::GetTime();
        m_ViewportHighlightPending = false;
    }
    if (m_CameraAvailable && !m_CameraDiagnostic.empty())
    {
        // Keep fallback/command feedback visible over the image without changing
        // the viewport extent or introducing an input-consuming widget.
        const ImVec2 position{m_ViewportBounds[0].x + 8.f, m_ViewportBounds[0].y + Rbs::Editor::Px(56)};
        const float wrap = (std::max)(1.f, m_ViewportBounds[1].x - position.x - 8.f);
        const auto size = ImGui::CalcTextSize(m_CameraDiagnostic.c_str(), nullptr, false, wrap);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled({position.x - 4.f, position.y - 4.f},
                            {position.x + size.x + 4.f, position.y + size.y + 4.f},
                            ImGui::GetColorU32(ImVec4(0, 0, 0, .8f)), 3.f);
        draw->AddText(nullptr, 0.f, position, ImGui::GetColorU32(ImGuiCol_Text),
                      m_CameraDiagnostic.c_str(), nullptr, wrap);
    }

    /*m_MousePickFrameBuffer->Bind();
	RenderSystem::OnMouseClicked(m_ActiveScene.get(), *m_MousePickFrameBuffer, m_ViewportBounds[0], m_ViewportBounds[1]);
	m_MousePickFrameBuffer->UnBind();*/

    /*auto windowSize = ImGui::GetWindowSize();
	ImVec2 minBound = ImGui::GetWindowPos();
	minBound.x += viewportOffset.x;
	minBound.y += viewportOffset.y;

	ImVec2 maxBound = { minBound.x + windowSize.x, minBound.y + windowSize.y };

	m_ViewportBounds[0] = { minBound.x, minBound.y };
	m_ViewportBounds[1] = { maxBound.x, maxBound.y };*/

    ImGui::End();
    ImGui::PopStyleVar();
}

namespace
{
    // Note: Switch this to true to enable dockspace
    //static bool show = true;
    //ImGui::ShowDemoWindow(&show);

    //static bool opt_fullscreen = true;
    //static bool opt_padding = false;
    //static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    //////// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
    //////// because it would be confusing to have two docking targets within each others.
    //ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    //if (opt_fullscreen)
    //{
    //	ImGuiViewport* viewport = ImGui::GetMainViewport();
    //	ImGui::SetNextWindowPos(viewport->Pos);
    //	ImGui::SetNextWindowSize(viewport->Size);
    //	ImGui::SetNextWindowViewport(viewport->ID);
    //	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    //	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    //	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    //	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    //}
    //else
    //{
    //	dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
    //}

    ////// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
    ////// and handle the pass-thru hole, so we ask Begin() to not render a background.
    //if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
    //	window_flags |= ImGuiWindowFlags_NoBackground;

    ////// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
    ////// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
    ////// all active windows docked into it will lose their parent and become undocked.
    ////// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
    ////// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
    //if (!opt_padding)
    //	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    //ImGui::Begin("DockSpace Demo", &show, window_flags);
    //if (!opt_padding)
    //	ImGui::PopStyleVar();

    //if (opt_fullscreen)
    //	ImGui::PopStyleVar(2);

    ////// Submit the DockSpace
    //ImGuiIO& io = ImGui::GetIO();

    //auto& style = ImGui::GetStyle();
    //float winMinSize = style.WindowMinSize.x;
    //style.WindowMinSize.x = 370.f;
    //if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    //{
    //	ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    //	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    //}

    //style.WindowMinSize.x = winMinSize;

    //if (ImGui::BeginMenuBar())
    //{
    //	if (ImGui::BeginMenu("File"))
    //	{
    //		// Disabling fullscreen would allow the window to be moved to the front of other windows,
    //		// which we can't undo at the moment without finer window depth/z control.
    //		//ImGui::MenuItem("Fullscreen", nullptr, &opt_fullscreen);
    //		//ImGui::MenuItem("Padding", nullptr, &opt_padding);
    //		//ImGui::Separator();

    //		if (ImGui::MenuItem("Exit"))
    //		{
    //			ShutDown();
    //		}

    //		ImGui::EndMenu();
    //	}

    //	ImGui::EndMenuBar();
    //}

    //ImGui::End();
}

void RigidBodySimulationApp::UI_Toolbar()
{
    /*ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	auto& colors = ImGui::GetStyle().Colors;
	const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
	const auto& buttonActive = colors[ImGuiCol_ButtonActive];
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));*/
}
