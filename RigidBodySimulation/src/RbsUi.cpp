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
#include "Physics/GJK.h"
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
#ifdef GENGINE_PHASE28_QUALIFICATION
namespace Rbs { void Phase28AngularWidget(RigidBodySimulationApp&,const char*); }
namespace Rbs { void Phase28ComponentWidget(const char*); }
namespace Rbs { float Phase28OutlineViewportScale(); }
#define RBS_PHASE28_WIDGET(key) Rbs::Phase28AngularWidget(*this,key)
#else
#define RBS_PHASE28_WIDGET(key) ((void)0)
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
#ifdef GENGINE_PHASE28_QUALIFICATION
        constexpr std::pair<std::string_view,const char*> controls[]{
            {"Current Collider","collider-current"},{"New Collider Type","collider-kind"},
            {"Sphere","collider-kind.sphere"},{"Box","collider-kind.box"},{"Capsule","collider-kind.capsule"},
            {"Fit to Mesh","collider-fit"},{"Radius","collider-radius"},{"Half Length","collider-half-length"},
            {"Apply","collider-apply"},{"Revert","collider-revert"},{"Cancel","collider-cancel"},
            {"Replace Anyway","collider-confirm"},{"Force Replace","collider-force"}};
        for(const auto& [name,key]:controls)if(name==label){Phase28ComponentWidget(key);break;}
#endif
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
        // Retain the same member defaults without expanding 64 nested preview
        // aggregates into a single compiler constant-initialization expression.
        ColliderInspector() noexcept {}

        PhysicsBodyHandle target{};
        _Entity entity;
        bool refreshRequested{};
        bool userEdited{};
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
            userEdited=false;
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
            extents?Editor::Numeric::Positive<float>():Editor::Numeric::Bounds<float>{},validate,nullptr,[&]([[maybe_unused]] int axis){
#ifdef GENGINE_PHASE28_QUALIFICATION
                constexpr const char* halfKeys[]{"collider-half-x","collider-half-y","collider-half-z"};
                constexpr const char* centerKeys[]{"collider-center-x","collider-center-y","collider-center-z"};
                Phase28ComponentWidget(extents?halfKeys[axis]:centerKeys[axis]);
#endif
            });
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
                    editor.userEdited=false;
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
        bool userEdit=false;
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
                if (ImGui::Selectable(types[index], index == editor.replacement) && index!=editor.replacement)
                {
                    editor.replacement = index;
                    BroadcastColliderType(editor, index);
                    userEdit=true;
                }
                NoteColliderControl(types[index]);
            }
            ImGui::EndCombo();
        }
        }
        if (Editor::Active)
        {
            Editor::PropertyRow row("Fit to mesh", "Fit the collider to the current render mesh. Disable for manual dimensions.");
            userEdit|=ImGui::Checkbox("##FitMesh", &editor.fitMesh);
        }
        else userEdit|=ImGui::Checkbox("Fit to Mesh", &editor.fitMesh);
        NoteColliderControl("Fit to Mesh");
        if (!editor.fitMesh)
        {
            const auto validShape=[](const CollisionShapeDesc& desc)->const char*
            {return CreateCollisionShape(desc)?nullptr:"Invalid shape: dimensions, bounds or inertia are not representable. Proposal unchanged.";};
            if (editor.replacement == 1)
            {
                userEdit|=ColliderVector("Half extents",editor.halfExtents,[&](const float* v){return validShape(BoxShapeDesc{{v[0],v[1],v[2]},editor.center});});
                userEdit|=ColliderVector("Center",editor.center,[&](const float* v){return validShape(BoxShapeDesc{editor.halfExtents,{v[0],v[1],v[2]}});});
            }
            else
            {
                userEdit|=ColliderNumber("Radius",editor.radius,Editor::Numeric::Positive<float>(),[&](float v){return validShape(editor.replacement==2?CollisionShapeDesc{CapsuleShapeDesc{v,editor.halfLength}}:CollisionShapeDesc{SphereShapeDesc{v}});});
                if (editor.replacement == 2)
                    userEdit|=ColliderNumber("Half Length",editor.halfLength,Editor::Numeric::Nonnegative<float>(),[&](float v){return validShape(CapsuleShapeDesc{editor.radius,v});});
            }
        }
        editor.userEdited|=userEdit;
        edited|=userEdit;
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
                              ImDrawList*, int, int, const ImVec4&, const ImVec4&, bool);
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
    if(m_ActiveSelection.IsAngularScene() && m_Angular.committed &&
       m_ChainImpulseMagnitude>3.f*m_Angular.committed->bodies[1].authoredMass)
    {m_SceneDiagnostic="Angular inspection impulse exceeds the admitted 3m N*s bound.";return false;}
    return m_ChainImpulse.Request(*world, m_ChainImpulseMagnitude,
        static_cast<Rbs::DistanceChainImpulseControl::Direction>(m_ChainImpulseDirection));
}

void RigidBodySimulationApp::DrawDistanceChainImpulseControls()
{
    if (!m_ActiveSelection.IsDistanceScene() && !m_ActiveSelection.IsBallSocketScene() &&
        !m_ActiveSelection.IsAngularScene()) return;
    using Control = Rbs::DistanceChainImpulseControl;
    ImGui::Separator();
    if(m_ActiveSelection.IsAngularScene()) ImGui::TextWrapped("Angular inspection impulse - dynamic bar, applied once at the next Update boundary even while paused");
    else if(m_ActiveSelection.IsBallSocketScene()) ImGui::TextWrapped("Ball-and-Socket impulse - dynamic body A (S: moving body)");
    else ImGui::TextWrapped("Distance chain impulse - terminal link %u", m_ActiveSelection.scene==Rbs::RbsScenePreset::DistancePendulum?1:m_ActiveSelection.chainLinks);
    if (SandboxEnabled() && m_ActiveSelection.IsAngularScene())
        ImGui::TextWrapped("%s", Rbs::IsMotorScene(m_ActiveSelection.scene)
            ? "Use Apply Impulse to push the Motor Bar. Reset restores the applied settings at Tick 0."
            : "The scene applies impulses after ticks 60 and 300. Reset restores the applied settings at Tick 0.");
    else if (SandboxEnabled()) ImGui::TextWrapped("Automatic 0.5 Ns impulse after tick 300; manual controls below.");
    const float maximum=m_ActiveSelection.IsAngularScene() && m_Angular.committed?
        (std::min)(Control::MaxMagnitude,3.f*m_Angular.committed->bodies[1].authoredMass):Control::MaxMagnitude;
    if (SandboxEnabled())
    {
        {
            Rbs::Editor::PropertyRow row("Impulse (Ns)", "Finite bounded magnitude; angular cases apply once at the next Update boundary.");
            Rbs::Editor::Numeric::Scalar("##Impulse",m_ChainImpulseMagnitude,.01f,Rbs::Editor::Numeric::Closed(Control::MinMagnitude,maximum),Rbs::Editor::Numeric::Accept{},true);
        }
        {
            Rbs::Editor::PropertyRow row("Direction");
            ImGui::Combo("##Direction", &m_ChainImpulseDirection, "+X\0-X\0+Z\0-Z\0");
        }
    }
    else
    {
        Rbs::Editor::Numeric::Scalar("Impulse (Ns)",m_ChainImpulseMagnitude,.01f,Rbs::Editor::Numeric::Closed(Control::MinMagnitude,maximum),Rbs::Editor::Numeric::Accept{},true);
        ImGui::Combo("Horizontal direction", &m_ChainImpulseDirection, "+X\0-X\0+Z\0-Z\0");
    }
    ImGui::BeginDisabled(Rbs::Editor::Numeric::HasError() || !m_ChainImpulse.Enabled() || m_ChainImpulse.Pending() || m_SceneRequest.has_value());
    const bool impulse=ImGui::Button("Apply Impulse");
    RBS_PHASE28_WIDGET("impulse");
    if(impulse) RequestDistanceChainImpulse();
    ImGui::EndDisabled();
    const char* status = "Ready";
    switch (m_ChainImpulse.GetStatus())
    {
    case Control::Status::Ready: break;
    case Control::Status::Disabled: status = "Manual impulses disabled during qualification"; break;
    case Control::Status::Queued: status = m_ActiveScene->IsPaused() && !m_ActiveSelection.IsAngularScene()
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
        auto* physics=m_ActiveScene?m_ActiveScene->GetPhysicsSystem():nullptr;
        auto* world=physics?physics->GetPhysicsWorld():nullptr;
        ImGui::TextWrapped("Active backend: %s",world?backendLabel(world->GetBackend()):"Unavailable");
        if(physics)ImGui::Text("Tick: %llu",static_cast<unsigned long long>(physics->GetCompletedSteps()));
        ImGui::BeginDisabled(m_SceneRequest.has_value() || m_SimulationState==State::Switching || !m_ActiveScene);
        {
        Rbs::Editor::PropertyRow row("Backend", "Apply rebuilds this authored scene at Tick 0; failure retains the previous world.");
        const float backendWidth=ImGui::GetContentRegionAvail().x;
        const bool inlineApply=backendWidth>=Rbs::Editor::Px(240);
        ImGui::SetNextItemWidth(inlineApply?backendWidth-Rbs::Editor::Px(64):backendWidth);
        const bool backendOpen=ImGui::BeginCombo("##PhysicsBackend",backendLabel(m_PendingBackend));
        RBS_PHASE28_WIDGET("backend");
        if(backendOpen)
        {
            for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt})
            {
                ImGui::BeginDisabled(!PhysicsBackendAvailable(backend));
                const bool selected=ImGui::Selectable(backendLabel(backend),backend==m_PendingBackend);
                RBS_PHASE28_WIDGET(backend==PhysicsBackend::Native?"backend.native":"backend.jolt");
                if(selected)m_PendingBackend=backend;
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        if(inlineApply)ImGui::SameLine();
        ImGui::BeginDisabled(m_PendingBackend==m_Backend || !PhysicsBackendAvailable(m_PendingBackend) ||
            (m_Ragdoll.draft && (m_Ragdoll.dirty || m_SimulationState==State::Running)) ||
            (m_ActiveSelection.IsAngularScene() && (m_Angular.dirty || m_SimulationState==State::Running)));
        const bool backendApply=ImGui::Button("Apply##PhysicsBackend");
        RBS_PHASE28_WIDGET("backend-apply");
        if(backendApply)RequestBackend(m_PendingBackend);
        ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
        if(!PhysicsBackendAvailable(PhysicsBackend::Jolt))ImGui::TextWrapped("Jolt 5.6.0 is not enabled in this build.");
        ImGui::TextWrapped("Apply stops and rebuilds this authored scene at Tick 0. Failure retains the previous world.");
    if(m_ActiveSelection.IsAngularScene() || m_Ragdoll.draft)
    {
        ImGui::TextWrapped("These scenes use a fixed solver profile. Reset restores the applied scene settings.");
        if(world && world->GetBackend()==PhysicsBackend::Jolt)
        {
            if(const auto actual=world->ReadJoltSettings())
            {
                ImGui::TextWrapped("Effective Jolt settings (read-only): velocity %d, position %d, collision substeps %d; restitution threshold %.6g m/s, Baumgarte %.6g.",
                    actual->velocitySteps,actual->positionSteps,actual->collisionSteps,
                    actual->minRestitutionVelocity,actual->baumgarte);
                ImGui::TextWrapped("Penetration slop %.6g m, speculative distance %.6g m; sleep %s, dwell %.6g s, point speed %.6g m/s.",
                    actual->penetrationSlop,actual->speculativeDistance,
                    actual->allowSleeping?"on":"off",actual->timeBeforeSleep,actual->pointVelocitySleepThreshold);
                ImGui::TextWrapped("Warm start %s, pair cache %s, manifold reduction %s.",
                    actual->warmStarting?"on":"off",actual->bodyPairCache?"on":"off",
                    actual->manifoldReduction?"on":"off");
            }
            else ImGui::TextWrapped("Effective Jolt settings readback unavailable.");
        }
        else if(world)ImGui::Text("Native solver iterations: %d",physics->GetSolverIterations());
    }
    else DrawJoltSettings();
    DrawDistanceChainImpulseControls();
    ImGui::TextWrapped("%s",m_SceneDiagnostic.c_str());
}


bool RigidBodySimulationApp::HasPendingColliderDraft() const
{
    const auto& edit=Rbs::colliderEditor;
    if(!m_ActiveScene || edit.sceneIdentity!=m_ActiveScene.get() ||
       !(edit.userEdited || edit.queued || edit.confirmation) ||
       edit.entity!=m_HoveredEntity || edit.entity.GetSceneContext()!=m_ActiveScene.get())return false;
    const auto binding=m_ActiveScene->GetBodyBinding(edit.entity);
    return binding && *binding==edit.target;
}

void RigidBodySimulationApp::PrepareColliderUi()
{
    m_ColliderOutlines.Prune(*m_ActiveScene->GetPhysicsSystem());
    m_EditorSelection.Prune(m_ActiveScene->RenderData());
    if(m_HoveredEntity.GetSceneContext() &&
       (m_HoveredEntity.GetSceneContext()!=m_ActiveScene.get() || !m_HoveredEntity))
    {
        if (!m_ActiveScene->RenderData().Resolve(m_EditorSelection.anchor))
            m_EditorSelection.anchor = {};
        if (!m_EditorSelection.count) m_ViewportOwnsSelection = false;
        SynchronizeEditorSelection();
    }
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
        if(m_ActiveSelection.IsAngularScene() || RagdollBoneFor(m_HoveredEntity))
            ImGui::TextDisabled("Select a joint endpoint in Properties to edit its body and collider.");
        else DrawObjectProperties();
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
        if(m_Ragdoll.draft) {
            ImGui::Checkbox("Ragdoll diagnostics",&m_Ragdoll.debugEnabled);
            if(m_Ragdoll.debugEnabled) {
                ImGui::Checkbox("Joint anchors",&m_Ragdoll.showAnchors);
                ImGui::Checkbox("Joint axes",&m_Ragdoll.showAxes);
                ImGui::Checkbox("Joint limits",&m_Ragdoll.showLimits);
                ImGui::Checkbox("Joint errors",&m_Ragdoll.showErrors);
                ImGui::Checkbox("Contact measurement",&m_Ragdoll.showContacts);
                if(m_Ragdoll.showContacts)ImGui::TextWrapped("Measure contact geometry in a bone's Advanced / Backend Readback to display a snapshot with its tick.");
            }
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
void RigidBodySimulationApp::DrawAngularWorldSettings()
{
    namespace E=Rbs::Editor;
    if(!m_Angular.committed || !m_Angular.draft)return;
    const bool worldOpen=E::Component("Scene Physics",true);
    RBS_PHASE28_WIDGET("book-world-section");
    if(!worldOpen)return;
    const auto* world=m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    if(world) {
        const auto actual=world->GetGravity();
        E::ReadOnly("Applied gravity (m/s^2)",std::format("({:.3f}, {:.3f}, {:.3f})",actual.x,actual.y,actual.z).c_str());
    }
    else E::ReadOnly("Applied gravity","World unavailable");
    E::ReadOnly("Fixed step (s)",std::format("{:.9f}",m_Angular.committed->fixedStepSeconds).c_str());
    E::ReadOnly("Solver profile","Existing Native one pass / Jolt current defaults; fixed for this case");
    ImGui::BeginDisabled(m_SceneRequest.has_value() || m_EditorMode!=EditorMode::Edit ||
        (m_SimulationState!=Rbs::SimulationState::Ready && m_SimulationState!=Rbs::SimulationState::Paused));
    {
        E::PropertyRow row("Draft gravity Y (m/s^2)",
            "Current-world override; X/Z remain zero. Applies by Tick-0 reconstruction.");
        const bool changed=E::Numeric::Scalar("##value",m_Angular.draft->gravity.y,.1f,
            E::Numeric::Closed(-10.f,0.f),E::Numeric::Accept{});
        RBS_PHASE28_WIDGET("gravity");
        if(changed)m_Angular.dirty=true;
    }
    ImGui::EndDisabled();
}

namespace
{
    std::string QuaternionText(const Quat& q)
    {
        return std::format("({:.4g}, {:.4g}, {:.4g}, {:.4g})",q.w,q.x,q.y,q.z);
    }
}

void RigidBodySimulationApp::DrawAngularProperties()
{
    namespace E=Rbs::Editor;
    if(!m_Angular.committed || !m_Angular.draft)
    {ImGui::TextWrapped("Object properties are unavailable while the scene initializes.");return;}
    auto& session=m_Angular;
    auto& draft=*session.draft;
    auto* physics=m_ActiveScene->GetPhysicsSystem();
    auto* world=physics?physics->GetPhysicsWorld():nullptr;
    const bool canAuthor=m_EditorMode==EditorMode::Edit && !m_SceneRequest &&
        (m_SimulationState==Rbs::SimulationState::Ready || m_SimulationState==Rbs::SimulationState::Paused);
    const bool selected=m_HoveredEntity.GetSceneContext()==m_ActiveScene.get() &&
        m_HoveredEntity && m_ActiveScene->RenderData().Identify(m_HoveredEntity).has_value() &&
        m_HoveredEntity.HasAllComponents<Transform3DComponent>();
    PhysicsBodyHandle selectedHandle{};
    bool selectedHasBody=false;
    if(selected)
        if(auto binding=m_ActiveScene->GetBodyBinding(m_HoveredEntity))
        {selectedHandle=*binding;selectedHasBody=true;}
    Rbs::AngularBodyPacket* endpoint=nullptr;
    if(selectedHasBody && m_EditorSelection.count<=1) {
        for(std::size_t i=0;i<2 && i<m_AngularBindings.bodyCount;++i) {
            if(static_cast<entt::entity>(m_HoveredEntity)==m_AngularBindings.entities[i] &&
               selectedHandle==m_AngularBindings.bodies[i])endpoint=&draft.bodies[i];
        }
    }
    const auto vectorText=[](const Vec3f& v){return std::format("({:.4f}, {:.4f}, {:.4f})",v.x,v.y,v.z);};
    const auto motionName=[](BodyType type){return type==BodyType::Static?"Static":type==BodyType::Dynamic?"Dynamic":"Kinematic";};
    const auto number=[&](const char* label,float& value,float speed,const char* tip,
                          E::Numeric::Bounds<float> bounds,const char* key) {
        E::PropertyRow row(label,tip);
        const bool changed=E::Numeric::Scalar("##value",value,speed,bounds);
        RBS_PHASE28_WIDGET(key);
        return changed;
    };
    const auto axes=[&](const char* label,Vec3f& value,float reset,float speed,const char* tip,
                        E::Numeric::Bounds<float> bounds,const char* key,int observedAxis,
                        const Vec3f* minimum=nullptr,const Vec3f* maximum=nullptr,const Vec3f* defaults=nullptr) {
        const auto valid=[&](const float* candidate)->const char* {
            for(int axis=0;minimum && maximum && axis<3;++axis)
                if(candidate[axis]<(*minimum)[axis] || candidate[axis]>(*maximum)[axis])
                    return "Dimension outside this endpoint's supported range.";
            return nullptr;
        };
        return E::XYZ(label,&value.x,reset,speed,tip,bounds,valid,nullptr,[&](int axis){
            if(axis==observedAxis)RBS_PHASE28_WIDGET(key);
#ifdef GENGINE_PHASE28_QUALIFICATION
            else if(std::string_view(key)=="size-x" && endpoint && endpoint->key==Rbs::AngularBodyKey::Bar)
                RBS_PHASE28_WIDGET(axis==1?"scale":"size-z");
#endif
        },defaults?&defaults->x:nullptr);
    };
    if(!endpoint) {
        ImGui::TextWrapped("The selected joint endpoint has no current body binding. Reselect it after the scene finishes loading.");
        return;
    }
    ImGui::TextWrapped("%s",m_HoveredEntity.GetName().c_str());
    if(session.dirty)ImGui::TextWrapped("Pending changes. Apply to rebuild the scene at Tick 0.");
    if(!canAuthor)ImGui::TextWrapped("Pause the simulation and enter Edit mode to change properties.");
    {
        const bool bar=endpoint->key==Rbs::AngularBodyKey::Bar;
        const Vec3f baseHalf=bar ? (draft.joint.motor ? Vec3f{3,.25f,.25f} : Vec3f{.15f,1.f,.15f}) : Vec3f{.25f};
        const auto minHalf=.75f*baseHalf,maxHalf=1.25f*baseHalf;
        const auto baseScale=2.f*baseHalf,minScale=2.f*minHalf,maxScale=2.f*maxHalf;
        ImGui::PushID(static_cast<int>(m_SceneGeneration));
        ImGui::PushID(static_cast<int>(endpoint->key));
        ImGui::BeginDisabled(!canAuthor);
        if(E::Component("Transform",true)) {
            auto position=endpoint->center;
            auto rotation=glm::degrees(glm::eulerAngles(endpoint->orientation));
            auto scale=endpoint->sphere?endpoint->halfExtents:2.f*endpoint->halfExtents;
            if(axes("Translation (m)",position,0.f,.05f,"Tick-0 world position of the selected endpoint.",
                    {},"position-x",0)) {
                Rbs::SetAngularBodyPosition(draft,endpoint->key,position);
                session.dirty=true;
            }
            if(axes("Rotation (deg)",rotation,0.f,.25f,"Tick-0 world orientation in degrees.",
                    E::Numeric::Closed(-180.f,180.f),"rotation-y",1)) {
                Rbs::SetAngularBodyRotation(draft,endpoint->key,glm::quat(glm::radians(rotation)));
                session.dirty=true;
            }
            if(endpoint->sphere) {
                if(number("Uniform scale",scale.x,.005f,"The sphere mesh uses a unit radius. Scale and collider radius stay equal.",
                   E::Numeric::Closed(minHalf.x,maxHalf.x),"sphere-scale")) {
                    Rbs::SetAngularBodyHalfExtents(draft,endpoint->key,Vec3f{scale.x});
                    session.dirty=true;
                }
            } else if(axes("Scale",scale,1.f,.01f,"Box size is 75% to 125% of its default on each axis. Rebuilds the collider and inertia.",
                    E::Numeric::Positive<float>(),"scale-x",0,&minScale,&maxScale,&baseScale)) {
                Rbs::SetAngularBodyHalfExtents(draft,endpoint->key,.5f*scale);
                session.dirty=true;
            }
            ImGui::TextWrapped("Position and rotation move the connected pair around this endpoint. Use these fields to keep both joint frames aligned.");
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::PopID();
        DrawContextProperties(ContextSection::Surface);
        ImGui::PushID(static_cast<int>(m_SceneGeneration));
        ImGui::PushID(static_cast<int>(endpoint->key));
        ImGui::BeginDisabled(!canAuthor);
        if(E::Component("Collider",true)) {
            if(!bar) {
                E::PropertyRow row("Shape","Select the anchor shape. The mesh and collider rebuild together.");
                const bool open=ImGui::BeginCombo("##shape",endpoint->sphere?"Sphere":"Box");
                RBS_PHASE28_WIDGET("shape");
                if(open) {
                    for(bool sphere:{false,true}) {
                        if(ImGui::Selectable(sphere?"Sphere":"Box",endpoint->sphere==sphere) && endpoint->sphere!=sphere) {
                            Rbs::SetAngularAnchorSphere(draft,sphere);session.dirty=true;
                        }
                        RBS_PHASE28_WIDGET(sphere?"shape.sphere":"shape.box");
                    }
                    ImGui::EndCombo();
                }
            } else {
                E::ReadOnly("Shape",endpoint->sphere?"Sphere":"Box",
                    bar?"The dynamic bar uses a box beam with a joint at its end.":nullptr);
                RBS_PHASE28_WIDGET("angular-shape-readonly");
            }
            auto half=endpoint->halfExtents;
            if(endpoint->sphere) {
                if(number("Radius (m)",half.x,.005f,"Sphere radius, 0.1875 to 0.3125 m.",
                    E::Numeric::Closed(minHalf.x,maxHalf.x),"radius")) {
                    Rbs::SetAngularBodyHalfExtents(draft,endpoint->key,Vec3f{half.x});session.dirty=true;
                }
            } else if(axes("Half extents (m)",half,.25f,.005f,draft.joint.motor?"75% to 125% of each reference half extent.":bar?
                "X/Z from 0.1125 to 0.1875 m, Y from 0.75 to 1.25 m.":
                "Each half extent ranges from 0.1875 to 0.3125 m.",
                E::Numeric::Positive<float>(),"size-x",0,&minHalf,&maxHalf,&baseHalf)) {
                Rbs::SetAngularBodyHalfExtents(draft,endpoint->key,half);session.dirty=true;
            }
        }
        if(E::Component("RigidBody",true)) {
            if(!bar) {
                E::PropertyRow row("Motion type","The anchor may be Static or Kinematic. The bar remains the dynamic endpoint.");
                const bool open=ImGui::BeginCombo("##motion",endpoint->kinematic?"Kinematic":"Static");
                RBS_PHASE28_WIDGET("motion");
                if(open) {
                    for(bool kinematic:{false,true}) {
                        if(ImGui::Selectable(kinematic?"Kinematic":"Static",kinematic==endpoint->kinematic) && kinematic!=endpoint->kinematic) {
                            endpoint->kinematic=kinematic;session.dirty=true;
                        }
                        RBS_PHASE28_WIDGET(kinematic?"motion.kinematic":"motion.static");
                    }
                    ImGui::EndCombo();
                }
            } else {
                E::ReadOnly("Motion type","Dynamic","The bar is the dynamic joint endpoint.");
                RBS_PHASE28_WIDGET("angular-motion-readonly");
            }
            float mass=endpoint->authoredMass;
            if(bar) {
                const float referenceMass = draft.joint.motor ? 100.f : 1.f;
                if(number("Mass (kg)",mass,.05f,"Apply rebuilds the bar inertia.",
                    E::Numeric::Closed(.5f*referenceMass,2.f*referenceMass),"mass")){Rbs::SetAngularBarMass(draft,mass);session.dirty=true;}
            } else {
                E::ReadOnly("Retained mass (kg)",std::format("{:.3f}",mass).c_str(),
                    "The anchor retains 1 kg with zero effective inverse mass.");
                RBS_PHASE28_WIDGET("angular-mass-readonly");
            }
            session.dirty|=number("Friction",endpoint->friction,.01f,"Contact friction from 0 to 1.",
                E::Numeric::Closed(0.f,1.f),"friction");
            session.dirty|=number("Restitution",endpoint->restitution,.01f,"Contact restitution from 0 to 1.",
                E::Numeric::Closed(0.f,1.f),"restitution");
            if(bar && m_Backend==PhysicsBackend::Jolt) {
                session.dirty|=number("Linear damping",endpoint->linearDamping,.01f,"Dynamic Jolt body, range 0 to 1.",
                    E::Numeric::Closed(0.f,1.f),"linear-damping");
                session.dirty|=number("Angular damping",endpoint->angularDamping,.01f,"Dynamic Jolt body, range 0 to 1.",
                    E::Numeric::Closed(0.f,1.f),"angular-damping");
            } else {
                E::ReadOnly("Damping",bar?"Fixed at zero on Native":"Fixed at zero for the anchor");
                RBS_PHASE28_WIDGET("angular-damping-readonly");
            }
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::PopID();
    }
    if(E::Component("Angular Joint",true)) {
        const bool hinge=draft.joint.motor || draft.scene==Rbs::RbsScenePreset::HingeBasic ||
            draft.scene==Rbs::RbsScenePreset::HingeQuaternion || draft.scene==Rbs::RbsScenePreset::HingeLimited;
        E::ReadOnly("Constraint",hinge?"Hinge":draft.scene==Rbs::RbsScenePreset::CvLimited?"Limited Constant Velocity":"Constant Velocity");
        RBS_PHASE28_WIDGET("angular-kind-readonly");
        E::ReadOnly("Endpoints",draft.joint.motor?"Motor Base and Motor Bar":"Angular Anchor and Angular Bar");
        ImGui::BeginDisabled(!canAuthor);
        E::ReadOnly("Local anchor A (m)",vectorText(draft.joint.localAnchorA).c_str());
        E::ReadOnly("Local anchor B (m)",vectorText(draft.joint.localAnchorB).c_str());
        if(const auto reference=Rbs::ReferenceAngularPacket(draft.scene,draft.variant)) {
            const auto base=reference->joint.localFrameA;
            auto offset=glm::degrees(glm::eulerAngles(draft.joint.localFrameA*glm::inverse(base)));
            if(axes("Joint frame offset (deg)",offset,0.f,.25f,
                "Rotation from the default local frame. Apply aligns both endpoint frames at Tick 0.",
                E::Numeric::Closed(-180.f,180.f),"joint-frame",2)) {
                Rbs::SetAngularJointFrame(draft,glm::normalize(glm::quat(glm::radians(offset))*base));
                session.dirty=true;
            }
        }
        if(draft.scene==Rbs::RbsScenePreset::HingeLimited || draft.scene==Rbs::RbsScenePreset::MotorLimit) {
            float lower=glm::degrees(draft.joint.lower),upper=glm::degrees(draft.joint.upper);
            if(number("Lower limit (deg)",lower,1.f,"Lower angular bound, from -45 to 0 degrees.",
               E::Numeric::Closed(-45.f,0.f),"limit-lower")){draft.joint.lower=glm::radians(lower);session.dirty=true;}
            if(number("Upper limit (deg)",upper,1.f,"Upper angular bound, from 0 to 45 degrees.",
               E::Numeric::Closed(0.f,45.f),"limit-upper")){draft.joint.upper=glm::radians(upper);session.dirty=true;}
        } else if(draft.scene==Rbs::RbsScenePreset::CvLimited) {
            float u=glm::degrees(draft.joint.halfAngleU),v=glm::degrees(draft.joint.halfAngleV);
            if(number("Pyramid U (deg)",u,1.f,"Symmetric U half-angle, 15 to 45 degrees.",
               E::Numeric::Closed(15.f,45.f),"limit-u")){draft.joint.halfAngleU=glm::radians(u);session.dirty=true;}
            if(number("Pyramid V (deg)",v,1.f,"Symmetric V half-angle, 15 to 45 degrees.",
               E::Numeric::Closed(15.f,45.f),"limit-v")){draft.joint.halfAngleV=glm::radians(v);session.dirty=true;}
        } else {
            E::ReadOnly("Angular limits","Free");
            RBS_PHASE28_WIDGET("angular-free-limits-readonly");
        }
        ImGui::EndDisabled();
    }
    const auto driven = world && draft.joint.motor ? world->ReadJoint(m_AngularBindings.joint) : ::GEngine::JointResult<::GEngine::JointState>{std::unexpected(::GEngine::JointError{})};
    if (draft.joint.motor && driven && driven->descriptor.angular.motor &&
        (selectedHandle==driven->descriptor.bodyA || selectedHandle==driven->descriptor.bodyB) && E::Component("Hinge Motor",true)) {
        auto& drive = *draft.joint.motor;
        const auto& applied = *driven->descriptor.angular.motor;
        ImGui::BeginDisabled(!canAuthor || m_PendingMotor.has_value());
        session.dirty |= ImGui::Checkbox("Enabled",&drive.enabled);
        session.dirty |= E::Number("Target (rad/s)",drive.targetVelocity,.02f,"Signed speed about the hinge axis.",E::Numeric::Closed(-2.f,2.f));
        session.dirty |= E::Number("Maximum torque (N m)",drive.maxTorque,.1f,"Symmetric drive and braking torque.",E::Numeric::Closed(0.f,100.f));
        ImGui::BeginDisabled(session.dirty);
        if (ImGui::Button("Reverse applied target")) {
            drive.targetVelocity = -applied.targetVelocity;
            session.dirty = true;
            RequestAngularApply();
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        const auto& observed = driven->angular;
        E::ReadOnly("Applied command",std::format("{}  {:.3f} rad/s  {:.3f} N m",
            applied.enabled?"Velocity":"Off",applied.targetVelocity,applied.maxTorque).c_str());
        const float stepSeconds = static_cast<float>(session.committed->fixedStepSeconds);
        const float impulseCap = applied.enabled ? applied.maxTorque * stepSeconds : 0.f;
        E::ReadOnly("Maximum impulse per step (N m s)",std::format("{:.5f}",impulseCap).c_str());
        E::ReadOnly("Speed / tracking error (rad/s)",std::format("{:.4f} / {:.4f}",observed.motorSpeed,observed.motorError).c_str());
        E::ReadOnly("Signed angle / limit side",std::format("{:.4f} rad / {}",observed.signedAngle,observed.motor.limitSide ? observed.motor.limitSide : observed.limitU).c_str());
        if (observed.motor.valid) {
            E::ReadOnly("Drive measurement",std::format("Valid at completed step {}",
                m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()).c_str());
            E::ReadOnly("Impulse / effort",std::format("{:.5f} N m s / {:.3f} N m",
                observed.motor.impulse,observed.motor.impulse/stepSeconds).c_str());
            E::ReadOnly("Saturated",observed.motorSaturated?"Yes":"No");
            E::ReadOnly("Signed motor work (J)",std::format("{:.6f}",observed.motor.work).c_str());
        } else E::ReadOnly("Drive measurement","Pending a completed fixed step");
    }
    DrawContextProperties(ContextSection::View);
    DrawSceneAuthoringActions();
    if(!session.diagnostic.empty())ImGui::TextWrapped("%s",session.diagnostic.c_str());
    const bool advancedOpen=E::Component("Advanced / Backend Readback",false);
    RBS_PHASE28_WIDGET("advanced");
    if(advancedOpen)
    {
        if(!world || !physics)E::ReadOnly("World","Provider readback unavailable");
        else
        {
            E::ReadOnly("World backend",world->GetBackend()==PhysicsBackend::Jolt?"Jolt 5.6.0":"Native");
            E::ReadOnly("Scene generation",std::format("{}",m_SceneGeneration).c_str());
            E::ReadOnly("Applied gravity (m/s^2)",std::format("({:.4f}, {:.4f}, {:.4f})",
                world->GetGravity().x,world->GetGravity().y,world->GetGravity().z).c_str());
            E::ReadOnly("Completed fixed steps",std::format("{}",physics->GetCompletedSteps()).c_str());
            E::ReadOnly("Actual bodies / joints",std::format("{} / {}",world->GetBodyCount(),world->GetJointCount()).c_str());
            if(selectedHasBody)
            {
                const char* role="Environment / other selected body";
                for(std::size_t i=0;i<m_AngularBindings.bodyCount;++i)
                    if(selectedHandle==m_AngularBindings.bodies[i])
                    {role=draft.joint.motor ? (i==0 ? "Motor Base" : i==1 ? "Motor Bar" : "Contact Sphere") :
                        i==0?"Angular Anchor":i==1?"Angular Bar":
                        SandboxEnabled()?"Shared Sandbox Floor (Contact ground)":"Contact Floor";break;}
                E::ReadOnly("Selected body role",role);
                E::ReadOnly("Selected body handle",std::format("{} / {}",
                    selectedHandle.index,selectedHandle.generation).c_str());
            }
            if(!selected)E::ReadOnly("Selected body","None; select a live scene object for body and joint readback");
            else if(!selectedHasBody)E::ReadOnly("Selected body","Selected entity has no Physics body");
            else if(auto body=world->ReadEffectiveBody(selectedHandle))
            {
                const auto& transform=m_HoveredEntity.Transform();
                E::ReadOnly("Render scale",vectorText(transform.Scale).c_str());
                E::ReadOnly("Collider shape",Rbs::ColliderTypeName(body->shapeType));
                E::ReadOnly("Local bounds half extents (m)",vectorText(.5f*(body->localBounds.maxs-body->localBounds.mins)).c_str());
                E::ReadOnly("Local bounds center (m)",vectorText(.5f*(body->localBounds.maxs+body->localBounds.mins)).c_str());
                E::ReadOnly("Motion",motionName(body->type));
                E::ReadOnly("Friction / restitution",std::format("{:.4f} / {:.4f}",body->material.friction,body->material.restitution).c_str());
                RBS_PHASE28_WIDGET("effective-material");
                if(auto p=world->ReadBodyProperties(selectedHandle)) {
                    E::ReadOnly("Retained mass (kg)",std::format("{:.4f}",p->mass).c_str());
                    RBS_PHASE28_WIDGET("effective-mass");
                    E::ReadOnly("Linear / angular damping",std::format("{:.4f} / {:.4f}",p->linearDamping,p->angularDamping).c_str());
                    RBS_PHASE28_WIDGET("effective-damping");
                } else E::ReadOnly("RigidBody properties","Provider readback unavailable");
                E::ReadOnly("Inverse mass (1/kg)",std::format("{:.4f}",body->inverseMass).c_str());
                RBS_PHASE28_WIDGET("effective-inverse-mass");
                E::ReadOnly("Linear velocity (m/s)",vectorText(body->linearVelocity).c_str());
                RBS_PHASE28_WIDGET("effective-linear-velocity");
                E::ReadOnly("Angular velocity (rad/s)",vectorText(body->angularVelocity).c_str());
                RBS_PHASE28_WIDGET("effective-angular-velocity");
                E::ReadOnly("Actual position (m)",std::format("({:.4f}, {:.4f}, {:.4f})",
                    body->position.x,body->position.y,body->position.z).c_str());
                RBS_PHASE28_WIDGET("effective-position");
                E::ReadOnly("Actual orientation (w,x,y,z)",std::format("({:.4f}, {:.4f}, {:.4f}, {:.4f})",
                    body->orientation.w,body->orientation.x,body->orientation.y,body->orientation.z).c_str());
                RBS_PHASE28_WIDGET("effective-orientation");
                E::ReadOnly("Actual center of mass (m)",std::format("({:.4f}, {:.4f}, {:.4f})",
                    body->centerOfMass.x,body->centerOfMass.y,body->centerOfMass.z).c_str());
                RBS_PHASE28_WIDGET("effective-com");
                E::ReadOnly("Body inverse inertia diagonal (1/kg m^2)",std::format("({:.6g}, {:.6g}, {:.6g})",
                    body->inverseInertiaBody[0][0],body->inverseInertiaBody[1][1],body->inverseInertiaBody[2][2]).c_str());
                RBS_PHASE28_WIDGET("effective-body-inertia");
                E::ReadOnly("World inverse inertia diagonal (1/kg m^2)",std::format("({:.6g}, {:.6g}, {:.6g})",
                    body->inverseInertiaWorld[0][0],body->inverseInertiaWorld[1][1],body->inverseInertiaWorld[2][2]).c_str());
                RBS_PHASE28_WIDGET("effective-world-inertia");
            }
            else E::ReadOnly("Selected body","Body handle is stale or backend readback failed");
            const auto joint=m_AngularBindings.joint?world->ReadJoint(m_AngularBindings.joint):
                JointResult<JointState>(std::unexpected(JointError{JointErrorCode::InvalidHandle}));
            if(!joint)E::ReadOnly("Angular joint","Provider readback unavailable; current state is unverified");
            else if(!selectedHasBody ||
                (selectedHandle!=joint->descriptor.bodyA && selectedHandle!=joint->descriptor.bodyB))
                E::ReadOnly("Angular joint","Selected entity is not an endpoint of the scene joint");
            else
            {
                const bool hinge=joint->descriptor.kind==JointKind::Hinge;
                E::ReadOnly("Provider",world->GetBackend()==PhysicsBackend::Native?"Native angular rows":
                    hinge?(joint->descriptor.angular.limitsEnabled?"Jolt limited hinge":"Jolt HingeConstraint"):
                    "Jolt constant velocity");
                E::ReadOnly("Joint kind",hinge?"Hinge":joint->descriptor.kind==JointKind::ConstantVelocity?
                    "Constant Velocity":"Limited Constant Velocity");
                E::ReadOnly("Authored joint key","Primary; Anchor -> Bar (stable authored keys)");
                if (joint->descriptor.angular.motor) {
                    const auto identity = [](const auto& handle) {
                        return std::format("{}/{}/{}", handle.registry, handle.index, handle.generation);
                    };
                    const auto& desc = joint->descriptor;
                    E::ReadOnly("Driven joint", identity(joint->handle).c_str(), "World domain / slot / generation.");
                    E::ReadOnly("Endpoint A / B", std::format("{}   {}", identity(desc.bodyA), identity(desc.bodyB)).c_str(),
                        "Provider body handles, each in world domain / slot / generation order.");
                    E::ReadOnly("Applied local anchors (m)", std::format("{} / {}", vectorText(desc.localAnchorA),
                        vectorText(desc.localAnchorB)).c_str(), "Committed COM-local anchors on A and B.");
                    E::ReadOnly("Applied frame A (w,x,y,z)", QuaternionText(desc.angular.localFrameA).c_str());
                    E::ReadOnly("Applied frame B (w,x,y,z)", QuaternionText(desc.angular.localFrameB).c_str());
                }
                const auto xA=joint->descriptor.angular.localFrameA*Vec3f{1.f,0.f,0.f};
                const auto xB=joint->descriptor.angular.localFrameB*Vec3f{1.f,0.f,0.f};
                E::ReadOnly("Local axis A",std::format("({:.3f}, {:.3f}, {:.3f})",xA.x,xA.y,xA.z).c_str());
                E::ReadOnly("Local axis B",std::format("({:.3f}, {:.3f}, {:.3f})",xB.x,xB.y,xB.z).c_str());
                E::ReadOnly("Anchor A world (m)",std::format("({:.4f}, {:.4f}, {:.4f})",joint->worldAnchorA.x,joint->worldAnchorA.y,joint->worldAnchorA.z).c_str());
                E::ReadOnly("Anchor B world (m)",std::format("({:.4f}, {:.4f}, {:.4f})",joint->worldAnchorB.x,joint->worldAnchorB.y,joint->worldAnchorB.z).c_str());
                E::ReadOnly("Anchor residual (m)",std::format("{:.6f}",joint->error).c_str());
                RBS_PHASE28_WIDGET("effective-anchor-residual");
                if(hinge)
                {
                    E::ReadOnly("Signed principal angle (deg)",std::format("{:.3f}",glm::degrees(joint->angular.signedAngle)).c_str());
                    E::ReadOnly("Axis error (deg)",std::format("{:.4f}",glm::degrees(joint->angular.axisError)).c_str());
                    E::ReadOnly("Limits (deg)",joint->descriptor.angular.limitsEnabled?
                        std::format("{:.2f} .. {:.2f}",glm::degrees(joint->descriptor.angular.lower),glm::degrees(joint->descriptor.angular.upper)).c_str():"Free");
                    E::ReadOnly("Limit state",joint->angular.limitU<0?"Lower":joint->angular.limitU>0?"Upper":"Free");
                }
                else
                {
                    E::ReadOnly("CV relation error (deg)",std::format("{:.4f}",glm::degrees(joint->angular.cvError)).c_str());
                    const bool limited=joint->descriptor.kind==JointKind::LimitedConstantVelocity;
                    E::ReadOnly("Effective U limits (deg)",limited?
                        std::format("{:.2f} .. {:.2f}",-glm::degrees(joint->descriptor.angular.halfAngleU),
                            glm::degrees(joint->descriptor.angular.halfAngleU)).c_str():"Free");
                    E::ReadOnly("Effective V limits (deg)",limited?
                        std::format("{:.2f} .. {:.2f}",-glm::degrees(joint->descriptor.angular.halfAngleV),
                            glm::degrees(joint->descriptor.angular.halfAngleV)).c_str():"Free");
                    E::ReadOnly("U / V angles (deg)",std::format("{:.3f} / {:.3f}",glm::degrees(joint->angular.angleU),glm::degrees(joint->angular.angleV)).c_str());
                    E::ReadOnly("U / V limit state",std::format("{} / {}",joint->angular.limitU,joint->angular.limitV).c_str());
                    E::ReadOnly("U / V violation (deg)",std::format("{:.4f} / {:.4f}",glm::degrees(joint->angular.violationU),glm::degrees(joint->angular.violationV)).c_str());
                }
                if(joint->angular.impulseAvailable && physics->GetCompletedSteps()>0)
                    E::ReadOnly("Provider row impulse (Nms)",std::format("({:.4f}, {:.4f}, {:.4f})",joint->angular.angularImpulse.x,joint->angular.angularImpulse.y,joint->angular.angularImpulse.z).c_str());
                else if(physics->GetCompletedSteps()==0)
                    E::ReadOnly("Provider row impulse","Unavailable until a completed fixed step");
                else if(world->GetBackend()==PhysicsBackend::Jolt && hinge)
                    E::ReadOnly("Provider row impulse","Jolt hinge provider reports complete row impulse unavailable");
                else E::ReadOnly("Provider row impulse","Provider did not expose complete row impulse data for this tick");
            }
        }
        E::ReadOnly("Applied revision",std::format("{}",session.revision).c_str());
        if(selected)
            if(const auto id=m_ActiveScene->RenderData().Identify(m_HoveredEntity))
                E::ReadOnly("Selected entity identity",std::format("{} / {} / {}",
                    id->registry,id->index,id->generation).c_str());
        if(session.interactive)ImGui::TextWrapped("Reset restores the applied scene after a manual action.");
        if(!m_PropertyDiagnostic.empty())ImGui::TextWrapped("%s",m_PropertyDiagnostic.c_str());
        if(!m_SceneDiagnostic.empty())ImGui::TextWrapped("%s",m_SceneDiagnostic.c_str());
        ImGui::TextWrapped("Optional axes/limits overlays use Physics Debug; visualization starts Off. Runtime diagnostics identify the last completed tick where available.");
    }
}

namespace
{
    std::string RagdollVectorText(const Vec3f& v)
    {
        return std::format("({:.4g}, {:.4g}, {:.4g})",v.x,v.y,v.z);
    }

    bool RagdollRotationField(const char* label,Quat& orientation)
    {
        Vec3f angles=glm::degrees(glm::eulerAngles(orientation));
        if(!Rbs::Editor::XYZ(label,&angles.x,0.f,.25f,"COM-local right-handed frame, in degrees."))return false;
        orientation=glm::normalize(glm::quat(glm::radians(angles)));
        return true;
    }

    bool DrawRagdollBodyFields(RagdollBodyDesc& body,PhysicsBackend backend)
    {
        namespace E=Rbs::Editor;
        auto& p=body.properties;
        bool changed=false;
        int motion=int(p.type);
        {E::PropertyRow row("Motion type");
            if(ImGui::Combo("##motion",&motion,"Static\0Dynamic\0Kinematic\0")) {
                p.type=static_cast<BodyType>(motion);changed=true;
            }}
        changed|=E::Number("Mass (kg)",p.mass,.05f,"Positive retained mass. Apply rebuilds inertia.",E::Numeric::Positive<float>());
        changed|=E::Number("Friction",p.material.friction,.01f,"Contact friction.",E::Numeric::Closed(0.f,1.f));
        changed|=E::Number("Restitution",p.material.restitution,.01f,"Contact restitution.",E::Numeric::Closed(0.f,1.f));
        ImGui::BeginDisabled(p.type==BodyType::Static);
        changed|=E::XYZ("Initial velocity (m/s)",&body.initialLinearVelocity.x,0.f,.05f);
        changed|=E::XYZ("Initial angular velocity (rad/s)",&body.initialAngularVelocity.x,0.f,.02f);
        ImGui::EndDisabled();
        if(backend==PhysicsBackend::Jolt) {
            ImGui::BeginDisabled(p.type!=BodyType::Dynamic);
            changed|=E::Number("Linear damping",p.linearDamping,.01f,"Dynamic Jolt body.",E::Numeric::Closed(0.f,1.f));
            changed|=E::Number("Angular damping",p.angularDamping,.01f,"Dynamic Jolt body.",E::Numeric::Closed(0.f,1.f));
            changed|=E::Number("Gravity factor",p.gravityFactor,.05f,"Dynamic Jolt body.",E::Numeric::Closed(0.f,10.f));
            changed|=E::Number("Velocity override",p.velocityStepsOverride,1.f,"0 inherits world, 2 to 255 overrides island iterations.",
                E::Numeric::Closed(0,255),[](int n)->const char*{return n==1?"Use 0 or at least 2 velocity steps.":nullptr;});
            changed|=E::Number("Position override",p.positionStepsOverride,1.f,"0 inherits world.",E::Numeric::Closed(0,255));
            ImGui::EndDisabled();
            ImGui::BeginDisabled(p.type==BodyType::Static);
            changed|=ImGui::Checkbox("Continuous collision",&p.continuousCollision);
            changed|=ImGui::Checkbox("Allow sleeping",&p.allowSleeping);
            ImGui::EndDisabled();
            E::ReadOnly("Per-body sleep thresholds","Unavailable on Jolt (world three-point test)");
        } else {
            E::ReadOnly("Damping / gravity factor",std::format("{} / {} / {}",p.linearDamping,p.angularDamping,p.gravityFactor).c_str());
            E::ReadOnly("CCD / allow sleeping",std::format("{} / {} (Native policy)",p.continuousCollision,p.allowSleeping).c_str());
            E::ReadOnly("Velocity / position override",std::format("{} / {} (Native policy)",p.velocityStepsOverride,p.positionStepsOverride).c_str());
            changed|=E::Number("Sleep linear speed (m/s)",p.sleep.linearSpeedThreshold,.01f,"Native body threshold.",E::Numeric::Nonnegative<float>());
            changed|=E::Number("Sleep angular speed (rad/s)",p.sleep.angularSpeedThreshold,.01f,"Native body threshold.",E::Numeric::Nonnegative<float>());
            changed|=E::Number("Sleep dwell (s)",p.sleep.inactivitySeconds,.01f,"Minimum continuous inactive time.",E::Numeric::Positive<double>());
        }
        E::ReadOnly("Spin / rolling length (m)",std::format("{} / {}",p.material.spinResistanceLength,p.material.rollingResistanceLength).c_str(),
            "The passive ragdoll keeps the reference resistance values.");
        return changed;
    }

    bool DrawRagdollJointFields(RagdollJointDesc& joint)
    {
        namespace E=Rbs::Editor;
        bool changed=false;
        const bool hinge=joint.Kind()==JointKind::Hinge;
        E::ReadOnly("Constraint",hinge?"Hinge":joint.angular.limitsEnabled?"Limited Constant Velocity":"Constant Velocity");
        E::ReadOnly("Endpoints",std::format("{} to {}",RagdollName(joint.bodyA),RagdollName(joint.bodyB)).c_str());
        changed|=E::XYZ("Local anchor A (m)",&joint.localAnchorA.x,0.f,.005f,"COM-local on the first endpoint.");
        changed|=E::XYZ("Local anchor B (m)",&joint.localAnchorB.x,0.f,.005f,"COM-local on the second endpoint.");
        changed|=RagdollRotationField("Local frame A (deg)",joint.angular.localFrameA);
        changed|=RagdollRotationField("Local frame B (deg)",joint.angular.localFrameB);
        changed|=ImGui::Checkbox("Collide connected",&joint.collideConnected);
        changed|=ImGui::Checkbox("Enable limits",&joint.angular.limitsEnabled);
        if(joint.angular.limitsEnabled) {
            const auto angle=[](const char* label,float& radians,float minimum,float maximum){
                float degrees=glm::degrees(radians);
                if(!E::Number(label,degrees,.5f,"Degrees in the authored local frame.",E::Numeric::Closed(minimum,maximum)))return false;
                radians=glm::radians(degrees);return true;
            };
            if(hinge) {
                changed|=angle("Lower (deg)",joint.angular.lower,-45.f,-15.f);
                changed|=angle("Upper (deg)",joint.angular.upper,15.f,45.f);
            } else {
                changed|=angle("Half-angle U (deg)",joint.angular.halfAngleU,15.f,45.f);
                changed|=angle("Half-angle V (deg)",joint.angular.halfAngleV,15.f,45.f);
            }
        }
        return changed;
    }

    void DrawRagdollJointReadback(const JointState& state,const PhysicsWorld& world,std::uint64_t tick)
    {
        namespace E=Rbs::Editor;
        const auto& desc=state.descriptor;
        const auto& angular=state.angular;
        E::ReadOnly("Descriptor",std::format("{}{}; {} to {}; connected collision {}",
            desc.kind==JointKind::Hinge?"Hinge":"Constant Velocity",desc.angular.limitsEnabled?" (limited)":"",
            desc.bodyA.index,desc.bodyB.index,desc.collideConnected).c_str());
        E::ReadOnly("COM-local anchors (m)",std::format("{} / {}",RagdollVectorText(desc.localAnchorA),RagdollVectorText(desc.localAnchorB)).c_str());
        E::ReadOnly("COM-local frames (w,x,y,z)",std::format("{} / {}",QuaternionText(desc.angular.localFrameA),QuaternionText(desc.angular.localFrameB)).c_str());
        if(desc.angular.limitsEnabled) {
            const bool hinge=desc.kind==JointKind::Hinge;
            E::ReadOnly(hinge?"Applied bounds (deg)":"Applied U/V half-angles (deg)",
                std::format("{:.4g} / {:.4g}",glm::degrees(hinge?desc.angular.lower:desc.angular.halfAngleU),
                    glm::degrees(hinge?desc.angular.upper:desc.angular.halfAngleV)).c_str());
        }
        E::ReadOnly("World anchors (m)",std::format("{} / {}",RagdollVectorText(state.worldAnchorA),RagdollVectorText(state.worldAnchorB)).c_str());
        E::ReadOnly("Anchor error (m)",std::format("{:.6g}",state.error).c_str());
        E::ReadOnly("Relative rotation (w,x,y,z)",QuaternionText(angular.relativeRotation).c_str());
        const auto a=world.ReadEffectiveBody(desc.bodyA),b=world.ReadEffectiveBody(desc.bodyB);
        if(a && b)E::ReadOnly("Relative angular velocity (rad/s)",RagdollVectorText(b->angularVelocity-a->angularVelocity).c_str());
        if(desc.kind==JointKind::Hinge) {
            E::ReadOnly("Signed angle / axis error (deg)",std::format("{:.4g} / {:.4g}",
                glm::degrees(angular.signedAngle),glm::degrees(angular.axisError)).c_str());
        } else {
            E::ReadOnly("U / V / CV error (deg)",std::format("{:.4g} / {:.4g} / {:.4g}",
                glm::degrees(angular.angleU),glm::degrees(angular.angleV),glm::degrees(angular.cvError)).c_str());
        }
        E::ReadOnly("Limit state U / V",std::format("{} / {}",angular.limitU,angular.limitV).c_str());
        E::ReadOnly("Limit violation U / V (deg)",std::format("{:.5g} / {:.5g}",
            glm::degrees(angular.violationU),glm::degrees(angular.violationV)).c_str());
        E::ReadOnly("Constraint active",state.active?"Yes":"No");
        E::ReadOnly("Point impulse (N s)",tick?RagdollVectorText(state.impulse).c_str():"Awaiting first tick");
        E::ReadOnly("Angular row impulse (N m s)",tick && angular.impulseAvailable?
            RagdollVectorText(angular.angularImpulse).c_str():"Unavailable for this provider or tick");
    }

    // Minimum separating-axis translation for two oriented boxes. A negative
    // result proves separation. The inputs are the current effective shapes.
    double RagdollBoxOverlap(const PhysicsBodyState& a,const PhysicsBodyState& b)
    {
        const auto axisA=glm::mat3_cast(glm::dquat(a.orientation));
        const auto axisB=glm::mat3_cast(glm::dquat(b.orientation));
        const auto halfA=.5*(glm::dvec3(a.localBounds.maxs)-glm::dvec3(a.localBounds.mins));
        const auto halfB=.5*(glm::dvec3(b.localBounds.maxs)-glm::dvec3(b.localBounds.mins));
        const auto center=[](const PhysicsBodyState& body){
            return glm::dvec3(body.position)+glm::dquat(body.orientation)*
                (.5*(glm::dvec3(body.localBounds.maxs)+glm::dvec3(body.localBounds.mins)));
        };
        const auto delta=center(b)-center(a);
        double depth=std::numeric_limits<double>::max();
        const auto project=[&](glm::dvec3 axis){
            const double length=glm::length(axis);
            if(length<1.e-12)return;
            axis/=length;
            const auto radiusA=glm::dot(halfA,glm::abs(glm::transpose(axisA)*axis));
            const auto radiusB=glm::dot(halfB,glm::abs(glm::transpose(axisB)*axis));
            depth=(std::min)(depth,radiusA+radiusB-std::abs(glm::dot(axis,delta)));
        };
        for(int i=0;i<3;++i) {
            project(axisA[i]);project(axisB[i]);
            for(int j=0;j<3;++j)project(glm::cross(axisA[i],axisB[j]));
        }
        return depth;
    }

    std::string RagdollContactSnapshot(PhysicsSystem& physics,const PassiveRagdoll& mapping,
        const std::array<PhysicsBodyHandle,11>& handles,std::uint64_t generation)
    {
        const auto& world=*physics.GetPhysicsWorld();
        std::array<std::optional<PhysicsBodyState>,11> states;
        std::array<std::unique_ptr<PhysicalShape>,11> shapes;
        bool filtered[6][6]{};
        for(auto handle:mapping.joints) {
            const auto joint=world.ReadJoint(handle);
            if(!joint)return "Contact snapshot unavailable: a joint has retired.";
            const auto& d=joint->descriptor;
            auto a=std::find(mapping.bodies.begin(),mapping.bodies.end(),d.bodyA);
            auto b=std::find(mapping.bodies.begin(),mapping.bodies.end(),d.bodyB);
            if(a==mapping.bodies.end() || b==mapping.bodies.end())return "Unexpected endpoint in current ragdoll mapping.";
            if(!d.collideConnected)filtered[a-mapping.bodies.begin()][b-mapping.bodies.begin()]=
                filtered[b-mapping.bodies.begin()][a-mapping.bodies.begin()]=true;
        }
        for(std::size_t i=0;i<handles.size();++i) {
            const auto state=world.ReadEffectiveBody(handles[i]);
            if(!state)return "Contact geometry unavailable: a mapped body has retired or has no effective collider.";
            states[i]=*state;
        }
        const auto proxy=[&](std::size_t i,RigidBody3D& body){
            if(!shapes[i]) {
                const auto desc=physics.CopyBodyShapeDescriptor(handles[i]);
                if(!desc)return false;
                const auto expected=states[i]->shapeType;
                if(expected!=ShapeType::Box && expected!=ShapeType::Sphere && expected!=ShapeType::Capsule)return false;
                auto copy=CreateCollisionShape(*desc);
                if(!copy || (*copy)->GetShapeType()!=expected)return false;
                shapes[i]=std::move(*copy);
            }
            body.m_Shape=shapes[i].get();body.m_Position=states[i]->position;body.m_Orientation=states[i]->orientation;
            return true;
        };
        std::size_t self=0,room=0,unknown=0;
        double maxDepth=0;
        for(std::size_t i=0;i<mapping.bodies.size();++i)for(std::size_t j=i+1;j<handles.size();++j) {
            if(j<mapping.bodies.size() && filtered[i][j])continue;
            const auto& a=*states[i];const auto& b=*states[j];
            if(!a.worldBounds.DoesIntersect(b.worldBounds))continue;
            double depth{};
            if(a.shapeType==ShapeType::Box && b.shapeType==ShapeType::Box) {
                depth=RagdollBoxOverlap(a,b);
                if(depth<0)continue;
            } else {
                RigidBody3D pa,pb;
                if(!proxy(i,pa) || !proxy(j,pb)){++unknown;continue;}
                Vec3f ptA{},ptB{};
                const auto outcome=GJK_GetContact(&pa,&pb,0.f,ptA,ptB);
                if(outcome==GjkContactStatus::Separated)continue;
                if(outcome==GjkContactStatus::Failed){++unknown;continue;}
                depth=glm::length(glm::dvec3(ptB)-glm::dvec3(ptA));
            }
            if(j<mapping.bodies.size())++self;else ++room;
            maxDepth=(std::max)(maxDepth,depth);
        }
        return std::format("Generation {}, tick {}. Geometric touching/overlap pairs: {} body-body, {} body-room. "
            "Maximum measured penetration {:.6g} m. Unresolved shape queries: {}.",generation,
            physics.GetCompletedSteps(),self,room,maxDepth,unknown);
    }
}

void RigidBodySimulationApp::DrawRagdollWorldSettings()
{
    namespace E=Rbs::Editor;
    if(!m_Ragdoll.draft || !m_ActiveScene || !E::Component("Scene Physics",true))return;
    auto* physics=m_ActiveScene->GetPhysicsSystem();
    const auto* world=physics?physics->GetPhysicsWorld():nullptr;
    if(!world){E::ReadOnly("Physics world","Unavailable");return;}
    if (m_ActiveSelection.IsRagdollScene() && world->GetBackend()==PhysicsBackend::Native)
        ImGui::TextWrapped("Native Passive Ragdoll is experimental. Its motion has not passed production qualification.");
    E::ReadOnly("Applied gravity (m/s^2)",RagdollVectorText(world->GetGravity()).c_str());
    E::ReadOnly("Fixed step (s)",std::format("{:.9g}",m_ActiveScene->GetPhysicsStep().count()).c_str());
    if(world->GetBackend()==PhysicsBackend::Native)
        E::ReadOnly("Native solver iterations",std::to_string(physics->GetSolverIterations()).c_str());
    else if(const auto settings=world->ReadJoltSettings())
        E::ReadOnly("Jolt velocity / position / collision steps",
            std::format("{} / {} / {}",settings->velocitySteps,settings->positionSteps,settings->collisionSteps).c_str());
    ImGui::BeginDisabled(m_SceneRequest || m_BackendRequest || m_EditorMode!=EditorMode::Edit ||
        (m_SimulationState!=Rbs::SimulationState::Ready && m_SimulationState!=Rbs::SimulationState::Paused));
    m_Ragdoll.dirty|=E::Number("Draft gravity Y (m/s^2)",m_Ragdoll.draft->gravityY,.1f,
        "Applies to the complete reconstructed world.",E::Numeric::Closed(-10.f,0.f));
    ImGui::EndDisabled();
}

void RigidBodySimulationApp::DrawRagdollProperties(std::size_t selected)
{
    namespace E=Rbs::Editor;
    if(RagdollBoneFor(m_HoveredEntity)!=selected || !m_Ragdoll.committed)return;
    auto& draft=*m_Ragdoll.draft;
    auto& body=draft.bodies[selected];
    const auto handle=m_RagdollBindings.mapping.bodies[selected];
    auto& physics=*m_ActiveScene->GetPhysicsSystem();
    auto& world=*physics.GetPhysicsWorld();
    const bool canEdit=m_EditorMode==EditorMode::Edit && !m_SceneRequest && !m_BackendRequest &&
        (m_SimulationState==Rbs::SimulationState::Ready || m_SimulationState==Rbs::SimulationState::Paused);
    const Vec3f placement{0,Rbs::SandboxEnvironment::FloorTop+draft.dropOffset,0};
    ImGui::PushID(static_cast<int>(m_SceneGeneration));
    ImGui::PushID(static_cast<int>(selected));
    ImGui::TextWrapped("%s",m_HoveredEntity.GetName().c_str());
    E::ReadOnly("Authored bone",RagdollName(body.key));
    if(m_Ragdoll.dirty)ImGui::TextWrapped("Pending changes. Apply reconstructs the assembly at Tick 0.");
    if(!canEdit)ImGui::TextWrapped("Pause and enter Edit mode to change the ragdoll.");
    ImGui::BeginDisabled(!canEdit);
    if(E::Component("Transform",true)) {
        ImGui::TextWrapped("Translation and rotation move the six connected bones around this selected bone. Scale resizes this bone and its collider.");
        auto position=body.center+placement;
        auto rotation=glm::degrees(glm::eulerAngles(body.orientation));
        auto scale=Rbs::RagdollRenderScale(body);
        if(E::XYZ("Translation (m)",&position.x,0.f,.05f))StageRagdollTransform(selected,TransformTool::Translate,position-placement);
        if(E::XYZ("Rotation (deg)",&rotation.x,0.f,.25f))StageRagdollTransform(selected,TransformTool::Rotate,glm::radians(rotation));
        if(body.shape==ShapeType::Box) {
            if(E::XYZ("Scale",&scale.x,1.f,.01f,"Dimensions of the unit box.",E::Numeric::Positive<float>()))
                StageRagdollTransform(selected,TransformTool::Scale,scale);
        } else if(E::Number("Uniform scale",scale.x,.005f,"Scales the primitive and its collider.",E::Numeric::Positive<float>()))
            StageRagdollTransform(selected,TransformTool::Scale,Vec3f{scale.x});
    }
    ImGui::EndDisabled();
    DrawContextProperties(ContextSection::Surface);
    ImGui::BeginDisabled(!canEdit);
    if(E::Component("Collider",true)) {
        int policy=draft.anchorResizePolicy==RagdollAnchorResizePolicy::ScaleWithBody?1:0;
        {E::PropertyRow row("Anchor resize policy","Review both endpoints and their staged world pivots below.");
            if(ImGui::Combo("##policy",&policy,"Preserve local metres\0Scale with body\0")) {
                draft.anchorResizePolicy=policy?RagdollAnchorResizePolicy::ScaleWithBody:RagdollAnchorResizePolicy::PreserveLocalMeters;
                m_Ragdoll.dirty=true;
            }}
        auto shape=body.shape;
        {E::PropertyRow row("Shape");
            if(ImGui::BeginCombo("##shape",Rbs::ColliderTypeName(shape))) {
                for(auto type:{ShapeType::Box,ShapeType::Sphere,ShapeType::Capsule})
                    if(ImGui::Selectable(Rbs::ColliderTypeName(type),shape==type))shape=type;
                ImGui::EndCombo();
            }}
        auto half=body.halfExtents,center=body.colliderCenter;
        float radius=body.radius,length=body.halfLength;
        bool dimensions=body.shape!=shape;
        if(shape==ShapeType::Box) {
            dimensions|=E::XYZ("Half extents (m)",&half.x,.25f,.005f,"75% to 125% of each reference dimension.",E::Numeric::Positive<float>());
            if(E::XYZ("Local center (m)",&center.x,0.f,.005f,"Mesh-local offset. The connected anchors are expressed about the COM.")) {
                Rbs::SetRagdollColliderCenter(draft,body.key,center);
                m_Ragdoll.dirty=true;
            }
        } else {
            E::ReadOnly("Local center (m)","At primitive origin");
            dimensions|=E::Number("Radius (m)",radius,.005f,"Primitive radius.",E::Numeric::Positive<float>());
            if(shape==ShapeType::Capsule)
                dimensions|=E::Number("Half length (m)",length,.005f,"Center-line half length, excluding caps.",E::Numeric::Positive<float>());
        }
        if(ImGui::Button("Fit to current mesh")) {
            const auto fitted=Rbs::PrepareMeshColliderAdmission(*m_ActiveScene,*m_SceneResources,m_HoveredEntity,shape);
            if(!fitted)m_Ragdoll.diagnostic=fitted.error().reason;
            else {
                if(const auto* box=std::get_if<BoxShapeDesc>(&*fitted)) {
                    half=box->halfExtents;
                    Rbs::SetRagdollColliderCenter(draft,body.key,box->center);
                } else if(const auto* sphere=std::get_if<SphereShapeDesc>(&*fitted))radius=sphere->radius;
                else if(const auto* capsule=std::get_if<CapsuleShapeDesc>(&*fitted)) {
                    radius=capsule->radius;length=capsule->halfLength;
                }
                dimensions=true;
            }
        }
        if(dimensions) {
            if(shape!=ShapeType::Box && body.colliderCenter!=Vec3f{0})
                m_Ragdoll.diagnostic="Set the Box local center to zero before switching to a centered primitive.";
            else {
                Rbs::SetRagdollShapeDimensions(draft,body.key,shape,half,radius,length);
                m_Ragdoll.dirty=true;
            }
        }
    }
    if(E::Component("RigidBody",true))m_Ragdoll.dirty|=DrawRagdollBodyFields(body,world.GetBackend());
    if(E::Component("Constraints / Joints",true)) {
        for(auto& joint:draft.joints) {
            if(joint.bodyA!=body.key && joint.bodyB!=body.key)continue;
            ImGui::PushID(static_cast<int>(joint.key));
            if(ImGui::TreeNode(RagdollName(joint.key))) {
                m_Ragdoll.dirty|=DrawRagdollJointFields(joint);
                const auto pivot=[&](RagdollBone bone,const Vec3f& anchor) {
                    const auto& endpoint=draft.bodies[static_cast<std::size_t>(bone)];
                    return placement+endpoint.center+endpoint.orientation*(endpoint.colliderCenter+anchor);
                };
                E::ReadOnly("Staged pivot A / B (world m)",std::format("{} / {}",
                    RagdollVectorText(pivot(joint.bodyA,joint.localAnchorA)),
                    RagdollVectorText(pivot(joint.bodyB,joint.localAnchorB))).c_str());
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    if(E::Component("Passive ragdoll",false)) {
        static const auto reference=Rbs::ReferenceRagdollPacket();
        const auto& committed=*m_Ragdoll.committed;
        const bool referencePose=committed.dropOffset==reference.dropOffset &&
            std::equal(committed.bodies.begin(),committed.bodies.end(),reference.bodies.begin(),
                [](const RagdollBodyDesc& a,const RagdollBodyDesc& b){return a.center==b.center && a.orientation==b.orientation;});
        E::ReadOnly("Initial pose source",std::format("{} | applied revision {}",
            referencePose?"Chapter 21 reference, pp. 82-84 (Y-up)":"Authored from Chapter 21, pp. 82-84",m_Ragdoll.revision).c_str());
        m_Ragdoll.dirty|=E::Number("Drop offset (m)",draft.dropOffset,.025f,"Vertical offset from the reference in the common room.",E::Numeric::Closed(0.f,2.f));
        for(const auto& member:draft.bodies) {
            const auto parent=RagdollParent(member.key);
            const auto binding=m_RagdollBindings.mapping.bodies[static_cast<std::size_t>(member.key)];
            E::ReadOnly(RagdollName(member.key),std::format("{} | {} | body {}:{}",
                parent?RagdollName(*parent):"Root",world.IsBodyValid(binding)?"Bound":"Missing",binding.index,binding.generation).c_str());
        }
        for(const auto& joint:draft.joints)
            E::ReadOnly(RagdollName(joint.key),std::format("{} to {}",RagdollName(joint.bodyA),RagdollName(joint.bodyB)).c_str());
        E::ReadOnly("Nonadjacent pairs","10 pairs retain ordinary body collision filtering");
    }
    ImGui::EndDisabled();
    DrawImpulseProperties();
    DrawContextProperties(ContextSection::View);
    DrawSceneAuthoringActions();
    if(!m_Ragdoll.diagnostic.empty())ImGui::TextWrapped("%s",m_Ragdoll.diagnostic.c_str());
    if(!m_SceneDiagnostic.empty())ImGui::TextWrapped("%s",m_SceneDiagnostic.c_str());
    if(E::Component("Advanced / Backend Readback",false)) {
        E::ReadOnly("Applied revision / generation",std::format("{} / {}",m_Ragdoll.revision,m_SceneGeneration).c_str());
        E::ReadOnly("Completed fixed steps",std::to_string(physics.GetCompletedSteps()).c_str());
        E::ReadOnly("Body handle",std::format("{} / {}",handle.index,handle.generation).c_str());
        if(const auto actual=world.ReadEffectiveBody(handle)) {
            E::ReadOnly("Position (m)",RagdollVectorText(actual->position).c_str());
            E::ReadOnly("Orientation (w,x,y,z)",QuaternionText(actual->orientation).c_str());
            E::ReadOnly("Center of mass (m)",RagdollVectorText(actual->centerOfMass).c_str());
            E::ReadOnly("Effective shape",Rbs::ColliderTypeName(actual->shapeType));
            E::ReadOnly("Local bounds (m)",std::format("{} to {}",RagdollVectorText(actual->localBounds.mins),RagdollVectorText(actual->localBounds.maxs)).c_str());
            E::ReadOnly("Inverse mass",std::format("{:.6g}",actual->inverseMass).c_str());
            const auto inertia=[](const Mat3& matrix){return std::format("{} / {} / {}",
                RagdollVectorText(matrix[0]),RagdollVectorText(matrix[1]),RagdollVectorText(matrix[2]));};
            E::ReadOnly("Body inverse inertia (columns)",inertia(actual->inverseInertiaBody).c_str());
            E::ReadOnly("World inverse inertia (columns)",inertia(actual->inverseInertiaWorld).c_str());
            E::ReadOnly("Linear velocity (m/s)",RagdollVectorText(actual->linearVelocity).c_str());
            E::ReadOnly("Angular velocity (rad/s)",RagdollVectorText(actual->angularVelocity).c_str());
            E::ReadOnly("Sleep state",actual->type!=BodyType::Dynamic?"Fixed or kinematic":actual->sleeping?"Sleeping":"Awake");
        } else E::ReadOnly("Body readback","Unavailable");
        if(const auto actual=world.ReadBodyProperties(handle)) {
            E::ReadOnly("Motion / authored mass (kg)",std::format("{} / {:.5g}",
                actual->type==BodyType::Dynamic?"Dynamic":actual->type==BodyType::Static?"Static":"Kinematic",actual->mass).c_str());
            E::ReadOnly("Friction / restitution",std::format("{} / {}",actual->material.friction,actual->material.restitution).c_str());
            E::ReadOnly("Damping (linear / angular)",std::format("{} / {}",actual->linearDamping,actual->angularDamping).c_str());
            E::ReadOnly("Gravity / CCD / sleeping",std::format("{} / {} / {}",actual->gravityFactor,actual->continuousCollision,actual->allowSleeping).c_str());
            E::ReadOnly("Iteration overrides",std::format("{} / {}",actual->velocityStepsOverride,actual->positionStepsOverride).c_str());
            if(world.GetBackend()==PhysicsBackend::Native)
                E::ReadOnly("Sleep thresholds (m/s, rad/s, s)",std::format("{} / {} / {}",actual->sleep.linearSpeedThreshold,
                    actual->sleep.angularSpeedThreshold,actual->sleep.inactivitySeconds).c_str());
        }
        for(const auto& joint:draft.joints) {
            const auto actual=m_RagdollBindings.mapping.ReadJoint(world,joint.key);
            if(!actual){E::ReadOnly(RagdollName(joint.key),"Missing or stale joint");continue;}
            E::ReadOnly(RagdollName(joint.key),std::format("Anchor error {:.5g} m",actual->error).c_str());
            if(actual->descriptor.bodyA!=handle && actual->descriptor.bodyB!=handle)continue;
            ImGui::PushID(static_cast<int>(joint.key));
            if(ImGui::TreeNode("Provider details")) {
                DrawRagdollJointReadback(*actual,world,physics.GetCompletedSteps());
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        std::size_t sleeping=0,live=0;
        for(const auto member:m_RagdollBindings.mapping.bodies)
            if(const auto actual=world.ReadEffectiveBody(member)){++live;if(actual->sleeping)++sleeping;}
        E::ReadOnly("Assembly sleeping / bound",std::format("{} / {}",sleeping,live).c_str());
        if(ImGui::Button("Measure contact geometry")) {
            std::array<PhysicsBodyHandle,11> handles{};
            std::copy(m_RagdollBindings.mapping.bodies.begin(),m_RagdollBindings.mapping.bodies.end(),handles.begin());
            for(std::size_t i=0;i<m_RagdollBindings.environmentEntities.size();++i) {
                const _Entity object{m_RagdollBindings.environmentEntities[i],m_ActiveScene.get()};
                if(object)handles[i+6]=m_ActiveScene->GetBodyBinding(object).value_or(PhysicsBodyHandle{});
            }
            try {m_Ragdoll.contactDiagnostic=RagdollContactSnapshot(physics,m_RagdollBindings.mapping,handles,m_SceneGeneration);}
            catch(const std::bad_alloc&){m_Ragdoll.contactDiagnostic="Contact snapshot allocation failed. No measurement available.";}
        }
        E::Tooltip("Measure geometry of live enabled ragdoll pairs and the room at the current tick. Solver manifolds are reported separately.");
        if(!m_Ragdoll.contactDiagnostic.empty())ImGui::TextWrapped("%s",m_Ragdoll.contactDiagnostic.c_str());
        if(world.GetBackend()==PhysicsBackend::Jolt) {
            const auto view=world.ReadJoltDiagnostics();
            if(view && view->enabled) {
                std::size_t count=0;
                float deepest=0;
                for(const auto& contact:view->contacts) {
                    const auto& bones=m_RagdollBindings.mapping.bodies;
                    if(std::find(bones.begin(),bones.end(),contact.bodyA)==bones.end() &&
                       std::find(bones.begin(),bones.end(),contact.bodyB)==bones.end())continue;
                    ++count;deepest=(std::max)(deepest,contact.penetration);
                }
                E::ReadOnly("Jolt contact samples",std::format("Tick {}: {} samples, maximum depth {:.5g} m, {} overflow",
                    view->step,count,deepest,view->overflow).c_str());
            } else E::ReadOnly("Jolt contact samples","Provider contact sampling is off");
        } else {
            std::size_t count=0;
            const auto member=[&](RigidBodyIdentity id){
                return std::any_of(m_RagdollBindings.mapping.bodies.begin(),m_RagdollBindings.mapping.bodies.end(),
                    [&](PhysicsBodyHandle h){return id.IsValid() && id.GetSlot()-1==h.index && id.GetGeneration()==h.generation;});
            };
            for(const auto& island:physics.GetContactIslands())for(const auto& pair:island.contactPairs)
                if(member(pair.bodyA) || member(pair.bodyB))++count;
            E::ReadOnly("Native resting-solve contact pairs",std::to_string(count).c_str());
        }
    }
    ImGui::PopID();ImGui::PopID();
}

void RigidBodySimulationApp::DrawSceneAuthoringActions()
{
    namespace E=Rbs::Editor;
    const bool angular=m_ActiveSelection.IsAngularScene();
    const bool ragdoll=m_Ragdoll.draft.has_value();
    const bool pending=m_SceneDefaultsPending || (angular && m_Angular.dirty) || (ragdoll && m_Ragdoll.dirty);
    const bool switching=m_SceneRequest.has_value() || m_BackendRequest.has_value();
    const bool canAuthor=!switching && m_EditorMode==EditorMode::Edit &&
        (m_SimulationState==Rbs::SimulationState::Ready || m_SimulationState==Rbs::SimulationState::Paused);
    const bool pendingComponent=HasPendingObjectProperties(!ragdoll);
    if(pendingComponent)ImGui::TextWrapped("Apply or Revert the selected component before resetting or restoring the scene.");
    if(m_SceneDefaultsPending)ImGui::TextWrapped("Scene defaults are staged. Apply to restore them, or Revert to keep the current scene.");
    if(angular || ragdoll || pending) {
        ImGui::BeginDisabled(!canAuthor || !pending || pendingComponent || E::Numeric::HasError());
        if(ImGui::Button("Apply##scene")) {
            if(ragdoll)RequestRagdollApply();else if(angular)RequestAngularApply();else ApplySceneDefaults();
        }
        RBS_PHASE28_WIDGET("apply");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(switching || (!pending && !E::Numeric::HasError()));
        if(ImGui::Button("Revert##scene")) {
            if(ragdoll)RevertRagdollDraft();else if(angular)RevertAngularDraft();else RevertSceneDefaults();
        }
        RBS_PHASE28_WIDGET("revert");
        ImGui::EndDisabled();
    }
    ImGui::BeginDisabled(!canAuthor || pendingComponent);
    if(ImGui::Button("Restore Scene Defaults")) {
        if(ragdoll)RestoreRagdollDefaults();else if(angular)RestoreAngularDefaults();else StageSceneDefaults();
    }
    RBS_PHASE28_WIDGET("restore-defaults");
    ImGui::EndDisabled();
    if(angular || ragdoll) {
        ImGui::BeginDisabled(switching || pending || pendingComponent);
        if(ImGui::Button("Reset Simulation"))RequestScene(m_ActiveSelection);
        RBS_PHASE28_WIDGET("reset");
        ImGui::EndDisabled();
    }
}

void RigidBodySimulationApp::DrawEditorProperties()
{
    namespace E = Rbs::Editor;
    DrawCameraPilotControls();
    if(m_CameraPilot)ImGui::TextWrapped("Navigation authors this Camera pose. Commit or Cancel Pilot before other property edits.");
    ImGui::BeginDisabled(m_CameraPilot.has_value());
    struct PilotPropertyScope {~PilotPropertyScope(){ImGui::EndDisabled();}} pilotPropertyScope;
    if (m_HoveredEntity.GetSceneContext()!=m_ActiveScene.get() || !m_HoveredEntity)
    {
        ImGui::TextUnformatted("No entity selected");
        ImGui::TextWrapped("Select an entity in Hierarchy or click an object in the Viewport.");
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Ctrl+click adds to selection. Esc clears.");
        ImGui::PopStyleColor();
        DrawSceneAuthoringActions();
        return;
    }
    const bool multiple=m_EditorSelection.count>1;
    if(!multiple)if(const auto bone=RagdollBoneFor(m_HoveredEntity)){DrawRagdollProperties(*bone);return;}
    if(!multiple && IsAngularEndpoint(m_HoveredEntity)){DrawAngularProperties();return;}
    ImGui::TextWrapped("%s", m_HoveredEntity.GetName().c_str());
    const bool protectedTransform=SelectionProtectsAngularTransform();
    if(multiple && protectedTransform) {
        ImGui::TextWrapped("This selection includes a connected joint endpoint or its parent. Select one endpoint for joint authoring, or select the other objects separately for a group edit.");
        if(E::Component("Visibility",true))DrawEntityPresentationProperties();
        DrawSceneAuthoringActions();
        return;
    }
    const bool hasMesh=m_HoveredEntity.HasAllComponents<MeshRendererComponent>();
    const char* type=m_HoveredEntity.HasAllComponents<RenderCameraComponent>()?"Camera":
        m_HoveredEntity.HasAllComponents<RenderLightComponent>()?"Light":hasMesh?"Mesh":"Entity";
    ImGui::TextDisabled("%s | %zu selected",type,m_EditorSelection.count);
    if (m_ColliderOutlines.capacityReached) ImGui::TextWrapped("Selection limit reached (4096).");
    if(!m_PropertyDiagnostic.empty())ImGui::TextWrapped("%s",m_PropertyDiagnostic.c_str());
    if(m_EditorSelection.count>1)ImGui::TextWrapped("Group Transform: one atomic gesture. History: up to 32 commands / 8 MiB. Unsupported batches retain the full selection.");
    const auto pose=m_ActiveScene->ReadBodyTransform(m_HoveredEntity);
    auto* world=m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    const auto body=pose?world->ReadBody(pose->body):PhysicsBodyResult<PhysicsBodyState>{
        std::unexpected(PhysicsBodyError{PhysicsBodyErrorCode::InvalidHandle})};
    ImGui::Separator();
    const bool switching=m_SceneRequest.has_value() || m_BackendRequest.has_value() ||
        m_SimulationState==Rbs::SimulationState::Switching;
    ImGui::BeginDisabled(switching);
    if (m_HoveredEntity==m_SkyBoxEntity)
        ImGui::TextWrapped("The sky follows the camera. Use sky visibility to control the background.");
    else if (m_HoveredEntity.HasAllComponents<Transform3DComponent>() && E::Component("Transform"))
    {
        if(protectedTransform)ImGui::TextWrapped("Moving this parent would move a joint endpoint. Select the endpoint to edit the connected transforms.");
        const bool numericPose=TransformEditingAllowed() && m_SimulationState!=Rbs::SimulationState::Running;
        if(!numericPose)ImGui::TextWrapped("Pause and enter Edit mode for numeric transforms.");
        ImGui::BeginDisabled(multiple || protectedTransform || !numericPose);
        auto position = m_HoveredEntity.Transform().Translation;
        const bool componentPose=m_HoveredEntity.HasAnyComponents<RenderCameraComponent,RenderLightComponent>();
        const auto* light=m_HoveredEntity.HasAllComponents<RenderLightComponent>()?
            &m_HoveredEntity.GetComponent<RenderLightComponent>():nullptr;
        const bool directional=light && light->kind==RenderLightKind::Directional;
        const bool point=light && light->kind==RenderLightKind::Point;
        // Authored lights may set the quaternion directly. Display the actual
        // local orientation used by RenderState, not a stale Euler convenience field.
        auto rotation = glm::degrees(componentPose?glm::eulerAngles(m_HoveredEntity.Transform().QuatRotation):m_HoveredEntity.Transform().EulerRotation);
        auto scale = m_HoveredEntity.Transform().Scale;
        ImGui::BeginDisabled(directional);
        if (E::XYZ("Translation (m)", &position.x, 0.f, .05f,
            directional?"Directional lighting uses rotation.":"Drag or Ctrl+click to edit position. Click an axis to reset it.",
            {},E::Numeric::Accept{},nullptr,[&](int axis){
                if(axis==0){RBS_PHASE28_WIDGET("reference-position-x");RBS_PHASE28_WIDGET("generic-position-x");}
                if(axis==1)RBS_PHASE28_WIDGET("generic-position-y");
                if(axis==2)RBS_PHASE28_WIDGET("generic-position-z");
            }))
            QueueTransformEdit(TransformTool::Translate, position);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(point);
        if (E::XYZ("Rotation (deg)", &rotation.x, 0.f, .25f,
            point?"A point light radiates equally in every direction.":"Drag or Ctrl+click to edit the selected object's local rotation.",
            {},E::Numeric::Accept{},nullptr,[&](int axis){
                if(axis==0)RBS_PHASE28_WIDGET("generic-rotation-x");
                if(axis==1)RBS_PHASE28_WIDGET("generic-rotation-y");
                if(axis==2)RBS_PHASE28_WIDGET("generic-rotation-z");}))
            QueueTransformEdit(TransformTool::Rotate, glm::radians(rotation));
        ImGui::EndDisabled();
        ImGui::BeginDisabled(componentPose);
        if(body && (body->shapeType==ShapeType::Sphere || body->shapeType==ShapeType::Capsule)) {
            E::PropertyRow row("Uniform scale","Apply one positive scale to all axes of the mesh and its collider.");
            if(E::Numeric::Scalar("##value",scale.x,.02f,E::Numeric::Positive<float>()))
                QueueTransformEdit(TransformTool::Scale,Vec3f{scale.x});
            RBS_PHASE28_WIDGET("generic-scale-x");
        }
        else if (E::XYZ("Scale", &scale.x, 1.f, .02f, componentPose ?
            "Edit the camera projection or the light's applicable range and cone settings." :
            "Scale changes the selected mesh and its bound collider.",
            {},E::Numeric::Accept{},nullptr,[&](int axis){if(axis==0)RBS_PHASE28_WIDGET("generic-scale-x");}))
            QueueTransformEdit(TransformTool::Scale, scale);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if(directional)ImGui::TextWrapped("Rotate this directional light to change the direction of illumination.");
        if(point)ImGui::TextWrapped("Translate this point light to move its center of illumination.");
        if(m_EditorSelection.count>1)ImGui::TextWrapped("Numeric Transform fields describe the active object. Use the toolbar gizmo for an atomic group gesture (up to 4096 roots; collider/query budgets apply).");
    }
    // Context editing remains single-object; no fabricated common material values.
    if(!multiple) {
        DrawContextProperties(ContextSection::Surface);
    }
    else if(E::Component("Visibility",true))DrawEntityPresentationProperties();
    if (pose && !multiple) {
        const bool colliderOpen=E::Component("Collider",false);
        RBS_PHASE28_WIDGET("generic-collider");
        if(colliderOpen)Rbs::DrawColliderInspector(Rbs::colliderEditor, *m_ActiveScene, *m_SceneResources,
            m_HoveredEntity, pose->body, m_IsPause);
    }
    if(multiple)DrawBulkBodyProperties();
    bool rigidBodyOpen=false;
    if(body && !multiple) {
        rigidBodyOpen=E::Component("Rigidbody",false);
        RBS_PHASE28_WIDGET("generic-rigidbody");
    }
    if (rigidBodyOpen)
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
            int type=int(p.type);if(ImGui::Combo("##Motion",&type,"Static\0Dynamic\0Kinematic\0")){p.type=BodyType(type);edit.dirty=true;}
            RBS_PHASE28_WIDGET("generic-motion");}
        edit.dirty |= E::Number("Mass (kg)",p.mass,.05f,"Positive authored mass. Recomputes inertia; retained while fixed.",E::Numeric::Positive<float>(),[](float v)->const char*{return std::isfinite(1.f/v)&&1.f/v>0?nullptr:"Mass must have a finite, positive inverse.";});
        RBS_PHASE28_WIDGET("generic-mass");
        ImGui::EndDisabled();
        auto linear = body->linearVelocity, angular = body->angularVelocity;
        ImGui::BeginDisabled(body->type == BodyType::Static);
        bool changed = E::XYZ("Velocity (m/s)", &linear.x, 0.f,.05f,"Drag or Ctrl+click a value to edit. Click an axis to reset only that axis.",{},E::Numeric::Accept{},nullptr,
            [&](int axis){if(axis==0)RBS_PHASE28_WIDGET("generic-linear-velocity-x");});
        changed |= E::XYZ("Angular (rad/s)", &angular.x, 0.f, .02f,"Drag or Ctrl+click a value to edit. Click an axis to reset only that axis.",{},E::Numeric::Accept{},nullptr,
            [&](int axis){if(axis==0)RBS_PHASE28_WIDGET("generic-angular-velocity-x");});
        if (changed)
        {m_VelocityEdit=VelocityEdit{m_HoveredEntity,pose->body,linear,angular,m_SceneGeneration};m_PropertyDiagnostic="Velocity edit queued for Update.";}
        ImGui::EndDisabled();
        ImGui::BeginDisabled(edit.queued);
        {
            E::PropertyRow row("Friction","Finite and nonnegative.");
            edit.dirty|=E::Numeric::Scalar("##value",p.material.friction,.01f,E::Numeric::Nonnegative<float>());
            RBS_PHASE28_WIDGET("generic-friction");
        }
        edit.dirty |= E::Number("Restitution",p.material.restitution,.01f,"Range 0 to 1.",E::Numeric::Closed(0.f,1.f));
        RBS_PHASE28_WIDGET("generic-restitution");
        const bool jolt=m_Backend==PhysicsBackend::Jolt;
        ImGui::BeginDisabled(!jolt);
        ImGui::BeginDisabled(p.type!=BodyType::Dynamic);
        edit.dirty |= E::Number("Linear damping",p.linearDamping,.01f,"Jolt only; range 0 to 1.",E::Numeric::Closed(0.f,1.f));
        RBS_PHASE28_WIDGET("generic-linear-damping");
        edit.dirty |= E::Number("Angular damping",p.angularDamping,.01f,"Jolt only; range 0 to 1.",E::Numeric::Closed(0.f,1.f));
        RBS_PHASE28_WIDGET("generic-angular-damping");
        edit.dirty |= E::Number("Gravity factor",p.gravityFactor,.05f,"Jolt only; range 0 to 10.",E::Numeric::Closed(0.f,10.f));
        RBS_PHASE28_WIDGET("generic-gravity-factor");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(p.type==BodyType::Static);
        {E::PropertyRow row("CCD");edit.dirty |= ImGui::Checkbox("##CCD",&p.continuousCollision);RBS_PHASE28_WIDGET("generic-ccd");}
        {E::PropertyRow row("Allow sleeping");edit.dirty |= ImGui::Checkbox("##Sleep",&p.allowSleeping);RBS_PHASE28_WIDGET("generic-allow-sleeping");}
        ImGui::EndDisabled();
        if(jolt && p.type==BodyType::Static)ImGui::TextWrapped("Static bodies have no effective damping, gravity, CCD or sleeping behavior.");
        const bool solverOpen=ImGui::TreeNode("Solver overrides");
        RBS_PHASE28_WIDGET("generic-solver");
        if(solverOpen) {
            ImGui::BeginDisabled(p.type!=BodyType::Dynamic);
            edit.dirty|=E::Number("Velocity override",p.velocityStepsOverride,1.f,"0 inherits world; 2..255. Maximum override can affect the entire contact/constraint island.",E::Numeric::Closed(0,255),[](int v)->const char*{return v==1?"Use 0 (world) or at least 2 so friction can operate.":nullptr;});
            RBS_PHASE28_WIDGET("generic-velocity-steps");
            edit.dirty|=E::Number("Position override",p.positionStepsOverride,1.f,"0 inherits world. Jolt uses the maximum across the island, not an isolated per-body solver.",E::Numeric::Closed(0,255));
            RBS_PHASE28_WIDGET("generic-position-steps");
            ImGui::EndDisabled();
            ImGui::TextWrapped("Experimental. Public Jolt MotionProperties setters; safe boundary, no body rebuild. Dynamic colliding bodies only. May change the whole island.");
            if(actual)ImGui::TextWrapped("Effective overrides: %d / %d. Jolt native: 0 / 0 (inherit).",actual->velocityStepsOverride,actual->positionStepsOverride);
            ImGui::TreePop();
        }
        ImGui::EndDisabled();
        if(!jolt)ImGui::TextWrapped("Native: damping 0, gravity factor 1, CCD and sleep policy are fixed. Sleep thresholds are editable below.");
        const bool sleepOpen=ImGui::TreeNode("Sleep thresholds");
        RBS_PHASE28_WIDGET("generic-sleep");
        if(sleepOpen)
        {
            ImGui::BeginDisabled(jolt);
            edit.dirty |= E::Number("Linear (m/s)",p.sleep.linearSpeedThreshold,.01f,"Finite, nonnegative Native threshold.",E::Numeric::Nonnegative<float>());
            RBS_PHASE28_WIDGET("generic-sleep-linear");
            edit.dirty |= E::Number("Angular (rad/s)",p.sleep.angularSpeedThreshold,.01f,"Finite, nonnegative Native threshold.",E::Numeric::Nonnegative<float>());
            RBS_PHASE28_WIDGET("generic-sleep-angular");
            edit.dirty |= E::Number("Dwell (s)",p.sleep.inactivitySeconds,.01f,"Finite, positive Native dwell time.",E::Numeric::Positive<double>());
            RBS_PHASE28_WIDGET("generic-sleep-dwell");
            ImGui::EndDisabled();
            if(jolt)ImGui::TextWrapped("Jolt world thresholds are under World Settings > Physics > Sleeping. These Native per-body thresholds cannot be mapped.");
            ImGui::TreePop();
        }
        ImGui::BeginDisabled(!edit.dirty || E::Numeric::HasError());
        if(ImGui::Button("Apply Rigidbody"))edit.queued=true;
        RBS_PHASE28_WIDGET("generic-body-apply");
        ImGui::EndDisabled();
        if(ImGui::GetContentRegionAvail().x>E::Px(280))ImGui::SameLine();
        if(ImGui::Button("Revert Rigidbody") && actual){p=*actual;edit.dirty=false;E::Numeric::Reset();}
        RBS_PHASE28_WIDGET("generic-body-revert");
        E::Tooltip("Discard this draft and read the active body.");
        const auto baseline=std::find_if(m_JoltSettingsEditor.bodyBaseline.begin(),m_JoltSettingsEditor.bodyBaseline.end(),[&](const auto& b){return b.first==pose->body;});
        ImGui::BeginDisabled(baseline==m_JoltSettingsEditor.bodyBaseline.end());
        if(ImGui::Button("Reset to Baseline##Body")){p=baseline->second;edit.dirty=true;E::Numeric::Reset();}
        RBS_PHASE28_WIDGET("generic-body-baseline");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        E::Tooltip("Stage the preserved world-entry body settings, then Apply Rigidbody.");
        ImGui::TextWrapped("%s",edit.queued?"Apply queued for simulation boundary.":edit.dirty?"Unapplied draft":"Active body settings");
        const bool resistanceOpen=ImGui::TreeNodeEx("Material details",0,"Contact resistance");
        RBS_PHASE28_WIDGET("generic-resistance");
        if (resistanceOpen)
        {
            E::ReadOnly("Spin length (m)", std::format("{:.3f}", body->material.spinResistanceLength).c_str());
            RBS_PHASE28_WIDGET("generic-spin-readback");
            E::ReadOnly("Rolling length (m)", std::format("{:.3f}", body->material.rollingResistanceLength).c_str());
            RBS_PHASE28_WIDGET("generic-rolling-readback");
            ImGui::TreePop();
        }
    }
    if (pose && !multiple)
    {
        bool foundJoint=false,showJoints=false;
        for (std::size_t i = 0; i < world->GetJointCount(); ++i)
        {
            const auto handle = world->JointAt(i);
            if (!handle) continue;
            const auto joint = world->ReadJoint(*handle);
            if (!joint || (joint->descriptor.bodyA != pose->body && joint->descriptor.bodyB != pose->body)) continue;
            if(!foundJoint){showJoints=E::Component("Constraints / Joints",false);foundJoint=true;}
            if(!showJoints)break;
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("%s Joint %zu",joint->descriptor.kind==JointKind::Distance?"Distance":"Ball-and-Socket",i+1);
            {
                const auto& desc = joint->descriptor;
                E::ReadOnly("Endpoints", std::format("Body {} / Body {}", desc.bodyA.index, desc.bodyB.index).c_str());
                if(desc.kind==JointKind::Distance) E::ReadOnly("Length (m)", std::format("{:.3f}", desc.targetDistance).c_str());
                else E::ReadOnly("Rotation", "Free on all three axes");
                auto a = desc.localAnchorA, b = desc.localAnchorB;
                ImGui::BeginDisabled();
                E::XYZ("Anchor A (m)", &a.x, 0.f, .01f, "Read-only: the current joint API has no live descriptor update operation.");
                E::XYZ("Anchor B (m)", &b.x, 0.f, .01f, "Read-only: the current joint API has no live descriptor update operation.");
                ImGui::EndDisabled();
                E::ReadOnly("Connected collision", desc.collideConnected ? "Enabled" : "Disabled");
                E::ReadOnly(desc.kind==JointKind::Distance?"Length error (m)":"Anchor residual (m)", std::format("{:.5f}", joint->error).c_str());
                if(desc.kind==JointKind::BallSocket) {
                    ImGui::TextColored({.1f,.85f,1.f,1.f},"A: cyan ring / COM helper / collider");
                    ImGui::TextColored({1.f,.55f,.1f,1.f},"B: orange cross / COM helper / collider");
                    E::ReadOnly("Anchor A world (m)",std::format("{:.5f}, {:.5f}, {:.5f}",joint->worldAnchorA.x,joint->worldAnchorA.y,joint->worldAnchorA.z).c_str());
                    E::ReadOnly("Anchor B world (m)",std::format("{:.5f}, {:.5f}, {:.5f}",joint->worldAnchorB.x,joint->worldAnchorB.y,joint->worldAnchorB.z).c_str());
                    ImGui::TextWrapped("Pink segment: anchor error. Viewport helpers use current Physics poses.");
                    if(m_ActiveSelection.scene==Rbs::RbsScenePreset::BallSocketContact ||
                       m_ActiveSelection.scene==Rbs::RbsScenePreset::BallSocketContactEnabled) {
                        const auto bodyA=world->ReadBody(desc.bodyA),bodyB=world->ReadBody(desc.bodyB);
                        const auto shapeA=m_ActiveScene->GetPhysicsSystem()->ReadBodyCollider(desc.bodyA);
                        const auto shapeB=m_ActiveScene->GetPhysicsSystem()->ReadBodyCollider(desc.bodyB);
                        const auto* sphereA=shapeA?std::get_if<SphereShapeDesc>(&shapeA->dimensions):nullptr;
                        const auto* sphereB=shapeB?std::get_if<SphereShapeDesc>(&shapeB->dimensions):nullptr;
                        if(bodyA && bodyB && sphereA && sphereB) {
                            const auto distance=glm::length(bodyB->position-bodyA->position);
                            const auto radii=sphereA->radius+sphereB->radius;
                            E::ReadOnly("Center distance (m)",std::format("{:.5f}",distance).c_str());
                            E::ReadOnly("Sum of sphere radii (m)",std::format("{:.5f}",radii).c_str());
                            E::ReadOnly("Sphere overlap (m)",std::format("{:.5f}",(std::max)(0.f,radii-distance)).c_str());
                        } else E::ReadOnly("Sphere overlap", "Unavailable: requires two live sphere colliders");
                    }
                }
                ImGui::TextWrapped("Read-only joint descriptor. Change the scene preset to load another configuration.");
            }
            ImGui::PopID();
        }
    }
    if(body || multiple)DrawImpulseProperties();
    if(!multiple)DrawContextProperties(ContextSection::View);
    DrawSceneAuthoringActions();
    if(E::Component("Advanced / Diagnostics",false))
    {
        if(multiple)ImGui::TextWrapped("Identifiers below describe the active object, not a common value.");
        if(const auto id=m_ActiveScene->RenderData().Identify(m_HoveredEntity))
            E::ReadOnly("Entity identity",std::format("{} / {} / {}",id->registry,id->index,id->generation).c_str());
        E::ReadOnly("Scene generation",std::format("{}",m_SceneGeneration).c_str());
        if(pose)E::ReadOnly("Body identity",std::format("{} / {}",pose->body.index,pose->body.generation).c_str());
        if(body && !multiple)
        {
            if(auto effective=world->ReadEffectiveBody(pose->body)) {
                E::ReadOnly("Effective position (m)",std::format("({:.4f}, {:.4f}, {:.4f})",
                    effective->position.x,effective->position.y,effective->position.z).c_str());
                E::ReadOnly("Orientation (w,x,y,z)", QuaternionText(effective->orientation).c_str());
                E::ReadOnly("Linear velocity (m/s)", std::format("({:.4g}, {:.4g}, {:.4g})",
                    effective->linearVelocity.x, effective->linearVelocity.y, effective->linearVelocity.z).c_str());
                E::ReadOnly("Angular velocity (rad/s)", std::format("({:.4g}, {:.4g}, {:.4g})",
                    effective->angularVelocity.x, effective->angularVelocity.y, effective->angularVelocity.z).c_str());
                E::ReadOnly("Inverse mass (1/kg)", std::format("{:.6g}", effective->inverseMass).c_str());
                if (effective->type == BodyType::Dynamic) {
                    const glm::dmat3 inverseInertia{effective->inverseInertiaBody};
                    const double determinant = glm::determinant(inverseInertia);
                    if (std::isfinite(determinant) && determinant > 0.0) {
                        const auto inertia = glm::inverse(inverseInertia);
                        E::ReadOnly("Body inertia (kg m^2)", std::format(
                            "({:.4g}, {:.4g}, {:.4g})\n({:.4g}, {:.4g}, {:.4g})\n({:.4g}, {:.4g}, {:.4g})",
                            inertia[0][0], inertia[1][0], inertia[2][0], inertia[0][1], inertia[1][1], inertia[2][1],
                            inertia[0][2], inertia[1][2], inertia[2][2]).c_str(), "Rows of the applied body-local inertia tensor.");
                    } else E::ReadOnly("Body inertia (kg m^2)", "Finite tensor unavailable");
                    E::ReadOnly("Sleep state", effective->sleeping ? "Sleeping" : "Awake");
                } else E::ReadOnly("Sleep state", "Fixed or kinematic");
                E::ReadOnly("Effective collider",Rbs::ColliderTypeName(effective->shapeType));
                E::ReadOnly("Effective friction / restitution",std::format("{:.4f} / {:.4f}",
                    effective->material.friction,effective->material.restitution).c_str());
            } else E::ReadOnly("Effective body","Backend readback unavailable");
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
    // Viewport-owned controls temporarily consume pointer/keyboard input without
    // retiring activation. Closing their popup must not cost the next scene click.
    const bool navInViewport=viewportWindow && uiContext.NavWindow &&
        ImGui::IsWindowChildOf(uiContext.NavWindow,const_cast<ImGuiWindow*>(viewportWindow),true,false);
    const bool overlay = navInViewport && (m_ViewportOverlayInteracting || m_ViewportPopupOpen || m_GizmoUsing);
    if (!visible || (!viewGesture && !overlay && !navInViewport && (active || popup || text)))
        SetViewportActive(false);
    const bool uiOwnsInput = !viewGesture && (active || popup || text);
    const InputRouting routing{
        GetWindow()->GetWindowID(),
        m_ViewportBounds[0].x - origin.x,
        m_ViewportBounds[0].y - origin.y,
        m_ViewportBounds[1].x - origin.x,
        m_ViewportBounds[1].y - origin.y,
        visible,
        m_ViewportActive,
        uiOwnsInput,
        uiOwnsInput || (!viewGesture && io.WantCaptureKeyboard && !m_ViewportActive)};
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
            if ((color & IM_COL32_A_MASK) == 0 || maximum.x <= minimum.x || maximum.y <= minimum.y) return;
            const float pixels = (std::max)(1.f, std::round(pixelsAt1x * dpi));
            const float x = (std::min)(pixels / pixelScale.x, (maximum.x - minimum.x) * .5f);
            const float y = (std::min)(pixels / pixelScale.y, (maximum.y - minimum.y) * .5f);
            // Reserve all four filled rectangles together in the current ImGui draw command.
            borderDraw->PrimReserve(24, 16);
            borderDraw->PrimRect(minimum, {maximum.x, minimum.y + y}, color);
            borderDraw->PrimRect({minimum.x, maximum.y - y}, maximum, color);
            borderDraw->PrimRect({minimum.x, minimum.y + y}, {minimum.x + x, maximum.y - y}, color);
            borderDraw->PrimRect({maximum.x - x, minimum.y + y}, {maximum.x, maximum.y - y}, color);
        };
        borderDraw->PushClipRect(clipMin, clipMax, false);
        // Opaque state indication is independent of the clock and Style.Alpha.
        borderOpacity = 1.f;
        float width = 1.f;
        ImU32 color = IM_COL32(71, 85, 105, 255);
        if (m_ViewportActive)
        {
            width = 3.f;
            color = IM_COL32(56, 189, 248, 255);
        }
        else if (m_ViewportHovered && ImRect(clipMin, clipMax).Contains(io.MousePos))
        {
            width = 2.f;
            color = Rbs::Editor::ControlAccent;
        }
        ring(width, color);
        if (m_ViewportActive && m_ViewportHighlightStart >= 0)
        {
            const float remaining = std::clamp(float(1.0 -
                (ImGui::GetTime() - m_ViewportHighlightStart) / .25), 0.f, 1.f);
            if (remaining > 0.f)
            {
                flashOpacity = remaining * remaining * (3.f - 2.f * remaining);
                ring(4.f, IM_COL32(125, 211, 252, int(std::round(255.f * flashOpacity))));
            }
        }
        borderDraw->PopClipRect();
    }
#ifdef GENGINE_LIFECYCLE_VALIDATION
    m_ViewportHighlightOpacity = flashOpacity;
    if (!PreEditorLifecycle::ObserveViewportFocus(m_ViewportActive, drawable, m_ViewportActivations,
            m_ViewportHighlightStart, flashOpacity, borderOpacity, borderDraw,
            firstBorderVertex, firstBorderIndex,
            {imageMin.x, imageMin.y, imageMax.x, imageMax.y},
            {clipMin.x, clipMin.y, clipMax.x, clipMax.y}, m_ViewportHovered))
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
    if(m_CameraPilot){DrawCameraPilotControls();ImGui::EndMenu();return;}
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
        if(ImGui::MenuItem("Pilot Camera",nullptr,false,live && !m_CameraPilot))(void)BeginCameraPilot(id);
        if (ImGui::MenuItem("Edit Transform", nullptr, false, live))
            (void)EditSceneCamera(id);
        if (ImGui::MenuItem("Open Camera View", nullptr, false, live))
            OpenCameraPanel(id);
        if (ImGui::MenuItem("Make Active", nullptr, active && *active && **active == id, live))
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
#ifdef GENGINE_PHASE28_QUALIFICATION
    // Exercise actual backing-target allocation and coordinate conversion together.
    // Ordinary Editor builds never alter the platform-reported framebuffer scale.
    if(scale) {const float factor=Rbs::Phase28OutlineViewportScale();scale->X*=factor;scale->Y*=factor;}
#endif
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
        draw->ChannelsSetCurrent(0);
        DrawViewportGizmo();
        if(m_ViewportOwnsSelection)DrawImpulseOverlay();
        DrawComponentOverlays();DrawViewportMarquee();
        draw->ChannelsSetCurrent(1);
        DrawGroupTransformActions();
        draw->ChannelsMerge();
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
