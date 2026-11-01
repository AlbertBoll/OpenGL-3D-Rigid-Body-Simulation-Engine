#pragma once
// Included only by RbsUi.cpp in the isolated smoke build. Drives the actual
// Inspector widgets using ImGui input events. Selection is supplied by fixtures;
// this does not qualify GPU picking, pixels, human readability or acceptance.
#include <cstdio>
#include <map>
#include <string>
#include "ColliderEditorOriginalSnapshot.h"

namespace Rbs::ColliderEditorProbe
{
    inline std::map<std::string, ImVec2> controls;
    inline std::map<std::string, ImRect> rectangles;
    inline bool layoutContained{true};
    inline bool observing{};
    inline FILE* output{};
    inline unsigned checks{};

    void Observe(const char* label)
    {
        if (!observing) return;
        const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        controls[label] = {(lo.x + hi.x) * .5f, (lo.y + hi.y) * .5f};
        rectangles[label] = {lo, hi};
        const auto* window = ImGui::GetCurrentWindow();
        if (!window->IsFallbackWindow && !(window->Flags & ImGuiWindowFlags_Popup))
            layoutContained &= lo.x >= window->InnerRect.Min.x - 1 && hi.x <= window->InnerRect.Max.x + 1;
    }

    inline bool Check(bool passed, const char* name)
    {
        ++checks;
        std::fprintf(output, "CHECK\t%s\t%s\n", passed ? "PASS" : "FAIL", name);
        std::fflush(output);
        return passed;
    }

    inline void ReproduceOriginal(_Scene& scene, SceneRenderResources& resources, _Entity entity)
    {
        auto* previous = ImGui::GetCurrentContext();
        auto* context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);
        auto& io = ImGui::GetIO();
        io.DisplaySize = {1000, 1100}; io.DeltaTime = 1.f / 60; io.IniFilename = nullptr;
        unsigned char* pixels{}; int width{}, height{};
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        OriginalWidgetRepro::ColliderInspector editor;
        bool paused = true, outlines = false;
        const auto handle = *scene.GetBodyBinding(entity);
        const auto frame = [&]
        {
            controls.clear(); rectangles.clear(); observing = true;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({650, 1050});
            ImGui::Begin("Collider widget smoke", nullptr, ImGuiWindowFlags_NoSavedSettings);
            OriginalWidgetRepro::DrawColliderInspector(editor, scene, resources, entity, handle, paused, outlines);
            ImGui::End(); ImGui::Render(); observing = false;
        };
        const auto click = [&](const char* label)
        {
            if (!controls.contains(label)) return;
            const auto oldPoint = controls.at(label);
            io.AddMousePosEvent(oldPoint.x, oldPoint.y); frame();
            const bool inside = rectangles.contains(label) && rectangles.at(label).Contains(oldPoint);
            const auto now = controls.contains(label) ? controls.at(label) : ImVec2{};
            std::fprintf(output, "ORIGINAL_POINTER\t%s\tstored=%.3f,%.3f\tcurrent=%.3f,%.3f\tinside=%d\n",
                label, double(oldPoint.x), double(oldPoint.y), double(now.x), double(now.y), int(inside));
            io.AddMouseButtonEvent(0, true); frame();
            io.AddMouseButtonEvent(0, false); frame();
        };
        frame(); frame(); click("New Collider Type"); click("Capsule");
        std::fprintf(output, "ORIGINAL_PREVIEW_ASSERTION\tpassed=%d\tpending=%s\tpreview=%d\tvisualControl=%d\tactive=%s\tstatus=%s\n",
            int(editor.preview && controls.contains("Visual preview")), ColliderTypeName(editor.PendingType()),
            int(bool(editor.preview)), int(controls.contains("Visual preview")),
            ColliderTypeName(scene.GetPhysicsSystem()->ReadBodyCollider(handle)->type), editor.status);
        std::fflush(output);
        ImGui::DestroyContext(context); ImGui::SetCurrentContext(previous);
    }

    struct Harness
    {
        _Scene& scene;
        SceneRenderResources& resources;
        _Entity selected;
        ColliderInspector localEditor;
        ColliderInspector& editor;
        ColliderOutlineSelection selection;
        bool shared{};
        bool paused{true};
        float panelWidth{650}, dpi{1};
        ImGuiStyle baseStyle;
        ImGuiContext* previous{ImGui::GetCurrentContext()};
        ImGuiContext* context{ImGui::CreateContext()};

        Harness(_Scene& s, SceneRenderResources& r, _Entity e, bool multi = false)
            : scene(s), resources(r), selected(e), editor(multi ? colliderEditor : localEditor), shared(multi)
        {
            ImGui::SetCurrentContext(context);
            auto& io = ImGui::GetIO();
            io.DisplaySize = {2400, 2600};
            io.DeltaTime = 1.f / 60;
            io.IniFilename = nullptr;
            unsigned char* pixels{}; int width{}, height{};
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            scene.SetPaused(true);
            if (shared)
            {
                ResetColliderEditor(scene, paused);
                selection.Select(*scene.GetBodyBinding(e), false, *scene.RenderData().Identify(e));
            }
            Frame(); Frame();
        }
        ~Harness()
        {
            observing = false;
            if (shared) ResetColliderEditor(scene, paused);
            ImGui::DestroyContext(context);
            ImGui::SetCurrentContext(previous);
        }
        void Frame()
        {
            controls.clear();
            rectangles.clear();
            layoutContained = true;
            ImGui::GetIO().FontGlobalScale = dpi;
            ImGui::GetStyle() = baseStyle;
            ImGui::GetStyle().ScaleAllSizes(dpi);
            observing = true;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({panelWidth, 2400});
            ImGui::Begin("Collider widget smoke", nullptr, ImGuiWindowFlags_NoSavedSettings);
            if (shared)
            {
                selection.Prune(*scene.GetPhysicsSystem());
                SyncColliderEditors(scene, resources, selection, selected, paused);
                HandleColliderUiEscape(scene, paused);
            }
            const auto handle = scene.GetBodyBinding(selected);
            if (handle) DrawColliderInspector(editor, scene, resources, selected, *handle, paused);
            else { EndColliderConfirmation(editor, scene, paused); editor = ColliderInspector{}; }
            if (shared) SyncColliderEditors(scene, resources, selection, selected, paused);
            ImGui::End();
            ImGui::Render();
            observing = false;
        }
        bool Click(const char* label)
        {
            const auto found = controls.find(label);
            if (!Check(found != controls.end(), label)) return false;
            const auto early = found->second;
            // ImGui positions an appearing combo popup on its following frame.
            // Observe the settled item before any mouse-button transition.
            Frame(); Frame();
            const auto current = controls.find(label);
            if (!Check(current != controls.end(), "settled widget still exists")) return false;
            auto point = current->second;
            const bool earlyInside = rectangles.at(label).Contains(early);
            std::fprintf(output, "POINTER\t%s\tearly=%.3f,%.3f\tsettled=%.3f,%.3f\tearlyInside=%d\n",
                label, double(early.x), double(early.y), double(point.x), double(point.y), int(earlyInside));
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y); Frame();
            point = controls.at(label);
            io.AddMousePosEvent(point.x, point.y); Frame();
            io.AddMouseButtonEvent(0, true); Frame();
            io.AddMouseButtonEvent(0, false); Frame();
            return true;
        }
        bool Type(const char* type)
        {
            if (!Click("New Collider Type") || !Click(type)) return false;
            Record("combo event outcome");
            return Check(std::strcmp(ColliderTypeName(editor.PendingType()), type) == 0,
                "combo actually selected the requested pending type");
        }
        bool Number(const char* control, const char* value)
        {
            if (!Click(control)) return false;
            auto& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiKey_ModCtrl, true);
            io.AddKeyEvent(ImGuiKey_A, true); Frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiKey_ModCtrl, false); Frame();
            io.AddInputCharactersUTF8(value); Frame();
            io.AddKeyEvent(ImGuiKey_Enter, true); Frame();
            io.AddKeyEvent(ImGuiKey_Enter, false); Frame();
            return true;
        }
        void Record(const char* operation)
        {
            auto handle = scene.GetBodyBinding(selected);
            auto shape = scene.GetPhysicsSystem()->ReadBodyCollider(*handle);
            std::fprintf(output, "ACTION\t%s\tentity=%u\tbody=%u\tgeneration=%llu\tworld=%llu\tpending=%s\tfit=%d\tactive=%s\tshape=%llu\trevision=%llu\n",
                operation, unsigned(selected), handle->index, static_cast<unsigned long long>(handle->generation),
                static_cast<unsigned long long>(handle->registry), ColliderTypeName(editor.PendingType()),
                int(editor.fitMesh), shape ? ColliderTypeName(shape->type) : "unavailable",
                shape ? static_cast<unsigned long long>(shape->identity) : 0,
                shape ? static_cast<unsigned long long>(shape->revision) : 0);
            std::fprintf(output, "PREVIEW\tpresent=%d\ttype=%s\tcontrol=%d\tuserPaused=%d\ttemporaryPause=%d\tscenePaused=%d\tstatus=%s\n",
                int(bool(editor.preview)), editor.preview ? ColliderTypeName(editor.preview->GetShapeType()) : "none",
                int(controls.contains("Visual preview")), int(paused), int(editor.temporaryPause), int(scene.IsPaused()), editor.status);
            if (const auto state = scene.GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(*handle))
                std::fprintf(output, "BODY\ttype=%d\tinverseMass=%.9g\tposition=%.9g,%.9g,%.9g\tworldY=%.9g,%.9g\n",
                    int(state->type), double(state->inverseMass), double(state->position.x), double(state->position.y),
                    double(state->position.z), double(state->worldBounds.mins.y), double(state->worldBounds.maxs.y));
            if (shape && shape->type == ShapeType::Capsule)
            {
                const auto c = std::get<CapsuleShapeDesc>(shape->dimensions);
                std::fprintf(output, "READBACK\tradius=%.9g\thalfLength=%.9g\n", double(c.radius), double(c.halfLength));
            }
            if (editor.result)
            {
                const auto& result = *editor.result;
                std::fprintf(output, "RESULT\toutcome=%d\terror=%d\tcommitted=%d\tcomplete=%d\treason=%s\n",
                    int(result.outcome), int(result.error), int(result.committed), int(result.complete), result.reason);
                for (const auto& pair : result.pairs)
                    std::fprintf(output, "PAIR\tbody=%u\told=%d\tnew=%d\toldMeasure=%.17g\tnewMeasure=%.17g\treason=%s\n",
                        pair.other.index, int(pair.oldOutcome), int(pair.newOutcome), pair.oldMeasure, pair.newMeasure, pair.reason);
            }
            std::fflush(output);
        }
    };

#define UX_CHECK(condition, name) do { if (!Check(bool(condition), name)) return false; } while (false)
    inline bool LayersAndMultiPreview(_Scene& live, SceneRenderResources& resources)
    {
        _Entity sourceSphere{}, sourceBox{};
        for (const auto id : live.ReadRegistry().view<RigidBody3DComponent, MeshRendererComponent>())
        {
            _Entity e{id, &live};
            const auto h = live.GetBodyBinding(e);
            if (!h) continue;
            const auto shape = live.GetPhysicsSystem()->ReadBodyCollider(*h);
            if (shape && shape->type == ShapeType::Sphere) sourceSphere = e;
            if (shape && shape->type == ShapeType::Box && e.GetComponent<RigidBody3DComponent>().Type == BodyType::Dynamic) sourceBox = e;
        }
        UX_CHECK(sourceSphere && sourceBox, "multi-preview mesh sources available");
        {
            _Scene scene;
            auto sphere = scene.CreateEntity("preview Sphere"), box = scene.CreateEntity("preview Box");
            UX_CHECK(sphere && box, "create independent multi-preview entities");
            RigidBody3DComponent dynamic; dynamic.Type = BodyType::Dynamic;
            sphere->AddOrReplaceComponent<RigidBody3DComponent>(dynamic);
            sphere->AddOrReplaceComponent<SphereFixture3DComponent>();
            sphere->AddOrReplaceComponent<MeshRendererComponent>(sourceSphere.GetComponent<MeshRendererComponent>());
            sphere->Transform().Translation = {-4, 5, 0};
            SphereFixture3DComponent ball; ball.Property.m_Position = sphere->Transform().Translation;
            sphere->AddOrReplaceComponent<SphereFixture3DComponent>(ball);
            box->AddOrReplaceComponent<RigidBody3DComponent>(dynamic);
            BoxFixture3DComponent cube; cube.Property.m_Position = {4, 5, 0};
            box->Transform().Translation = cube.Property.m_Position;
            box->Transform().Scale = {2, 2, 2};
            box->AddOrReplaceComponent<BoxFixture3DComponent>(cube);
            box->AddOrReplaceComponent<MeshRendererComponent>(sourceBox.GetComponent<MeshRendererComponent>());
            UX_CHECK(scene.OnRuntimeStart(), "multi-preview runtime started");
            const auto a = *scene.GetBodyBinding(*sphere), b = *scene.GetBodyBinding(*box);
            auto& physics = *scene.GetPhysicsSystem();
            const auto oldA = *physics.ReadBodyCollider(a), oldB = *physics.ReadBodyCollider(b);
            Harness ui(scene, resources, *sphere, true);
            ui.selection.Select(b, true, *scene.RenderData().Identify(*box));
            ui.selected = *box; ui.Frame(); ui.Frame();
            UX_CHECK(ui.selection.count == 2 && ui.Type("Capsule"), "shared type changed through actual Inspector combo");
            ColliderInspector* other = nullptr;
            for (auto& draft : otherColliderEditors) if (draft.target == a) other = &draft;
            UX_CHECK(other && other->preview && ui.editor.preview && other->PendingType() == ShapeType::Capsule &&
                ui.editor.PendingType() == ShapeType::Capsule, "all selected proposals receive explicit type change");
            UX_CHECK(std::get<CapsuleShapeDesc>(other->result->descriptor).halfLength == 0 &&
                std::get<CapsuleShapeDesc>(ui.editor.result->descriptor).radius > 1.4f,
                "each selected mesh supplies its own fitted Capsule dimensions");
            std::array<FrameDebugLine, 2048> lines;
            DebugGeometry previews(lines);
            UX_CHECK(AppendColliderEditorPreviews(scene, ui.selection, previews) && previews.Lines().size() > 192,
                "both cached proposed Capsule geometries emitted");
            bool left = false, right = false;
            for (const auto& line : previews.Lines()) { left |= line.start.x < 0; right |= line.start.x > 0; }
            UX_CHECK(left && right, "proposals use independent authoritative world poses");
            ui.selected = *sphere; ui.Frame(); ui.Frame();
            UX_CHECK(ui.selection.count == 2 && ui.editor.PendingType() == ShapeType::Capsule && ui.editor.preview,
                "changing active retains complete selection and existing previews");
            UX_CHECK(physics.ReadBodyCollider(a)->identity == oldA.identity && physics.ReadBodyCollider(b)->identity == oldB.identity,
                "shared previews do not commit either Shape");
            UX_CHECK(ui.Click("Apply") && ui.editor.result->committed && physics.ReadBodyCollider(a)->type == ShapeType::Capsule &&
                physics.ReadBodyCollider(b)->identity == oldB.identity, "Apply to Active Object commits only its own native Shape");
            ui.selection.Select(b, true); ui.Frame();
            UX_CHECK(ui.selection.count == 1, "toggle removal prunes inactive proposal");
            bool removed = true;
            for (const auto& draft : otherColliderEditors) removed &= draft.target != b;
            UX_CHECK(removed, "removed body has no retained draft");
            UX_CHECK(scene.DestroyEntity(*sphere), "delete active multi-preview object");
            ui.Frame();
            UX_CHECK(ui.selection.count == 0 && !hasColliderDrafts, "deletion clears selected identities and cached proposals");
        }
        {
            _Scene scene;
            auto sphere = scene.CreateEntity("contact Sphere"), floor = scene.CreateEntity("contact floor");
            UX_CHECK(sphere && floor, "layer contact fixtures created");
            RigidBody3DComponent dynamic; dynamic.Type = BodyType::Dynamic;
            sphere->AddOrReplaceComponent<RigidBody3DComponent>(dynamic);
            SphereFixture3DComponent ball; ball.Property.m_Position = {0, 2.99f, 0};
            sphere->Transform().Translation = ball.Property.m_Position;
            sphere->AddOrReplaceComponent<SphereFixture3DComponent>(ball);
            floor->AddOrReplaceComponent<RigidBody3DComponent>();
            BoxFixture3DComponent slab; slab.Shape.halfExtents = {20, 2, 20}; slab.Property.m_InvMass = 0;
            floor->AddOrReplaceComponent<BoxFixture3DComponent>(slab);
            UX_CHECK(scene.OnRuntimeStart(), "layer contact runtime started");
            scene.SetPaused(false); scene.Update(Timestep(1.0 / 60)); scene.SetPaused(true);
            auto& physics = *scene.GetPhysicsSystem();
            const std::array<PhysicsBodyHandle, 2> both{*scene.GetBodyBinding(*sphere), *scene.GetBodyBinding(*floor)};
            std::array<FrameDebugLine, 2048> allStorage, selectedStorage;
            const auto equal = [](std::span<const FrameDebugLine> a, std::span<const FrameDebugLine> b)
            {
                if (a.size() != b.size()) return false;
                for (std::size_t i = 0; i < a.size(); ++i)
                    if (a[i].start != b[i].start || a[i].end != b[i].end || a[i].color != b[i].color || a[i].depth != b[i].depth) return false;
                return true;
            };
            for (unsigned layer = 0; layer < 4; ++layer)
            {
                PhysicsSystem::DebugLayers flags{layer == 0, layer == 1, layer == 2, layer == 3, false};
                DebugGeometry all(allStorage), selected(selectedStorage);
                UX_CHECK(physics.AppendDebugGeometry(all, flags, {}, true) &&
                    physics.AppendDebugGeometry(selected, flags, both, false) && equal(all.Lines(), selected.Lines()),
                    "Selected all members equals All for this individual layer");
                UX_CHECK(!all.Lines().empty(), "each enabled layer has actual geometry including retained contacts");
                if (layer == 1) UX_CHECK(selected.Lines().size() == 24, "AABBs emitted for both selected bodies");
                if (layer >= 2)
                    for (const auto member : both)
                    {
                        selected.Clear();
                        UX_CHECK(physics.AppendDebugGeometry(selected, flags, {&member, 1}, false) && equal(all.Lines(), selected.Lines()),
                            "incident contact appears once with unchanged normal orientation for either endpoint selection");
                    }
                selected.Clear();
                UX_CHECK(physics.AppendDebugGeometry(selected, flags, {}, false) && selected.Lines().empty(),
                    "empty Selected emits no diagnostic geometry");
            }
            Harness ui(scene, resources, *sphere, true);
            ui.paused = false; scene.SetPaused(false);
            UX_CHECK(ui.Type("Capsule") && ui.Number("Half Length", "1.2") && ui.Click("Apply") && ui.editor.confirmation,
                "shared Inspector opens actual replacement confirmation");
            const auto old = physics.ReadBodyCollider(both[0])->identity;
            auto& io = ImGui::GetIO(); io.AddKeyEvent(ImGuiKey_Escape, true); ui.Frame();
            io.AddKeyEvent(ImGuiKey_Escape, false); ui.Frame();
            UX_CHECK(!ui.editor.confirmation && !ui.editor.preview && !scene.IsPaused() && ui.selection.count == 1 &&
                physics.ReadBodyCollider(both[0])->identity == old, "Esc first cancels confirmation safely and restores Running without clearing selection");
        }
        return true;
    }

    inline bool Run(_Scene& live, SceneRenderResources& resources)
    {
        using namespace ::GEngine::Component;
        _Entity sourceSphere{}, stackBox{};
        for (const auto id : live.ReadRegistry().view<RigidBody3DComponent, MeshRendererComponent>())
        {
            _Entity entity{id, &live};
            const auto binding = live.GetBodyBinding(entity);
            if (!binding) continue;
            const auto shape = live.GetPhysicsSystem()->ReadBodyCollider(*binding);
            if (shape && shape->type == ShapeType::Sphere) sourceSphere = entity;
            if (shape && shape->type == ShapeType::Box &&
                entity.GetComponent<RigidBody3DComponent>().Type == BodyType::Dynamic) stackBox = entity;
        }
        UX_CHECK(sourceSphere && stackBox, "ordinary SphereBoxStack fixture entities resolved");
        // Numerical display geometry checks are independent of ImGui projection.
        for (const bool capsule : {false, true})
        {
            const CollisionShapeDesc desc = capsule ? CollisionShapeDesc{CapsuleShapeDesc{2, 3}}
                : CollisionShapeDesc{SphereShapeDesc{2}};
            const auto shape = CreateCollisionShape(desc);
            UX_CHECK(shape, "create geometry-only probe shape");
            std::array<FrameDebugLine, 1024> storage;
            DebugGeometry geometry(storage);
            const Vec3f position{4, -3, 8};
            const Quat rotation = glm::angleAxis(.73f, glm::normalize(Vec3f{1, 2, 3}));
            UX_CHECK(PhysicsSystem::AppendColliderPreview(geometry, **shape, position, rotation, {1, .5f, 0, 1}),
                "smooth display geometry generated without live body");
            UX_CHECK(geometry.Lines().size() == (capsule ? 260 : 192), "bounded 64-segment circle topology");
            bool valid = true;
            for (const auto& line : geometry.Lines())
                for (const Vec3f point : {line.start, line.end})
                {
                    auto local = glm::inverse(rotation) * (point - position);
                    if (capsule) local.y -= (std::clamp)(local.y, -3.f, 3.f);
                    valid &= std::abs(glm::length(local) - 2.f) < .00001f;
                }
            UX_CHECK(valid, "all wire vertices lie on actual transformed sphere or capsule surface");
        }
        ColliderOutlineSelection outlines;
        const auto sphereHandle = *live.GetBodyBinding(sourceSphere);
        const auto boxHandle = *live.GetBodyBinding(stackBox);
        outlines.Select(sphereHandle, false);
        UX_CHECK(outlines.count == 1 && outlines.Contains(sphereHandle), "normal outline click selects only one body");
        outlines.Select(boxHandle, true);
        UX_CHECK(outlines.count == 2, "Ctrl outline click adds a second target");
        outlines.Select(sphereHandle, true);
        UX_CHECK(outlines.count == 1 && outlines.Contains(boxHandle), "Ctrl outline click toggles a target off");
        {
            _Scene empty;
            UX_CHECK(empty.OnRuntimeStart(), "empty replacement scene starts");
            outlines.Prune(*empty.GetPhysicsSystem());
            UX_CHECK(outlines.count == 0, "outline handles expire across world identity changes");
        }
        // A truly one-body world: all capsule dimensions remain production values.
        {
            _Scene scene;
            auto sphere = scene.CreateEntity("isolated sphere");
            UX_CHECK(sphere, "create isolated sphere");
            RigidBody3DComponent dynamic;
            dynamic.Type = BodyType::Dynamic;
            sphere->AddOrReplaceComponent<RigidBody3DComponent>(dynamic);
            sphere->AddOrReplaceComponent<SphereFixture3DComponent>();
            sphere->AddOrReplaceComponent<MeshRendererComponent>(sourceSphere.GetComponent<MeshRendererComponent>());
            UX_CHECK(scene.OnRuntimeStart(), "start one-body scene");
            const auto isolatedBody = scene.GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(*scene.GetBodyBinding(*sphere));
            UX_CHECK(isolatedBody && isolatedBody->type == BodyType::Dynamic && isolatedBody->inverseMass > 0,
                "isolated fixture is authoritatively Dynamic with positive inverse mass");
            ReproduceOriginal(scene, resources, *sphere);
            Harness ui(scene, resources, *sphere);
            const auto handle = *scene.GetBodyBinding(*sphere);
            const auto before = *scene.GetPhysicsSystem()->ReadBodyCollider(handle);
            const auto stateBeforePreview = *scene.GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(handle);
            const auto contextBeforePreview = scene.GetPhysicsSystem()->ColliderContextRevision();
            UX_CHECK(ui.editor.fitMesh, "new valid mesh proposal defaults Fit to Mesh ON");
            std::unique_ptr<PhysicalShape> independent;
            auto assessment = scene.AssessBodyColliderReplacement(*sphere, CapsuleShapeDesc{1, .5f}, &independent);
            std::fprintf(output, "INDEPENDENT\tpreview=%d\toutcome=%d\terror=%d\treason=%s\n",
                int(bool(independent)), int(assessment.outcome), int(assessment.error), assessment.reason);
            UX_CHECK(independent && independent->GetShapeType() == ShapeType::Capsule && !assessment.committed,
                "independent Scene staging returns disposable Capsule without commit");
            UX_CHECK(ui.Type("Capsule"), "select pending Capsule with ordinary combo");
            ui.Record("preview assertion operands");
            UX_CHECK(scene.GetPhysicsSystem()->ReadBodyCollider(handle)->type == ShapeType::Sphere,
                "automatic preview leaves authoritative Sphere active");
            UX_CHECK(ui.editor.preview && ui.editor.preview->GetShapeType() == ShapeType::Capsule,
                "widget path retains actual staged Capsule");
            UX_CHECK(controls.contains("Visual preview"), "compact 3D preview widget is submitted");
            for (const float scale : {1.f, 2.f})
                for (const float width : {240.f, 420.f, 720.f})
                {
                    ui.dpi = scale; ui.panelWidth = width * scale; ui.Frame(); ui.Frame();
                    std::fprintf(output, "LAYOUT\twidth=%.0f\tfontScale=%.1f\tcontained=%d\n",
                        double(ui.panelWidth), double(scale), int(layoutContained));
                    UX_CHECK(layoutContained && controls.contains("Apply") && controls.contains("Revert") &&
                        controls.contains("Current Collider"), "essential widget rectangles fit narrow standard wide and high DPI");
                }
            ui.dpi = 1; ui.panelWidth = 650; ui.Frame(); ui.Frame();
            UX_CHECK(ui.Click("Fit to Mesh") && !ui.editor.fitMesh, "manual override disables fitting");
            UX_CHECK(ui.editor.radius == 1 && ui.editor.halfLength == .5f, "manual values survived fitting redraws");
            const auto previewState = *scene.GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(handle);
            UX_CHECK(contextBeforePreview == scene.GetPhysicsSystem()->ColliderContextRevision() &&
                before.identity == scene.GetPhysicsSystem()->ReadBodyCollider(handle)->identity &&
                previewState.position == stateBeforePreview.position && previewState.orientation == stateBeforePreview.orientation &&
                previewState.linearVelocity == stateBeforePreview.linearVelocity &&
                previewState.angularVelocity == stateBeforePreview.angularVelocity &&
                previewState.sleeping == stateBeforePreview.sleeping &&
                previewState.inactiveSeconds == stateBeforePreview.inactiveSeconds,
                "preview and redraw preserve native identity pose velocity sleep and world context");
            for (const float scale : {1.f, 2.f})
                for (const float width : {240.f, 420.f, 720.f})
                {
                    ui.dpi = scale; ui.panelWidth = width * scale; ui.Frame(); ui.Frame();
                    UX_CHECK(layoutContained && controls.contains("Radius") && controls.contains("Half Length"),
                        "manual dimension controls fit narrow standard wide normal and high DPI");
                }
            ui.dpi = 1; ui.panelWidth = 650; ui.Frame(); ui.Frame();
            UX_CHECK(ui.Click("Apply"), "click Apply");
            ui.Record("isolated manual Sphere to Capsule");
            auto active = scene.GetPhysicsSystem()->ReadBodyCollider(handle);
            UX_CHECK(active && active->type == ShapeType::Capsule && active->identity != before.identity &&
                ui.editor.result && ui.editor.result->committed && ui.editor.after,
                "actual owned Capsule after widget commit and readback");
            UX_CHECK(!ui.editor.preview, "successful Apply clears candidate preview");
            const auto dims = std::get<CapsuleShapeDesc>(active->dimensions);
            UX_CHECK(dims.radius == 1 && dims.halfLength == .5f, "manual dimensions unchanged");
            UX_CHECK(ui.Click("Fit to Mesh") && ui.Click("Apply"), "fit mesh through widgets");
            ui.Record("isolated fitted Capsule");
            UX_CHECK(ui.editor.result && ui.editor.result->committed &&
                std::get<CapsuleShapeDesc>(scene.GetPhysicsSystem()->ReadBodyCollider(handle)->dimensions).halfLength == 0,
                "spherical mesh fit produces actual zero-segment Capsule");
            UX_CHECK(ui.Click("Revert") && ui.Click("Fit to Mesh") && ui.Number("Radius", "-1") && ui.Click("Apply"),
                "malformed dimension entered through widget");
            ui.Record("invalid radius rejection");
            UX_CHECK(ui.editor.result && !ui.editor.result->committed && !ui.editor.confirmation &&
                ui.editor.result->error == ColliderAdmissionError::InvalidShape, "invalid shape cannot offer force");
            UX_CHECK(ui.Click("Revert"), "revert invalid pending dimensions");
            UX_CHECK(scene.DestroyEntity(*sphere), "delete selected entity");
            ui.Frame();
            UX_CHECK(!ui.editor.preview && !ui.editor.confirmation && !ui.editor.temporaryPause,
                "deletion clears Inspector preview consent and pause lease");
        }
        // Ground contact separates conservative uncertainty from a known witness.
        {
            _Scene scene;
            auto sphere = scene.CreateEntity("ground sphere"), floor = scene.CreateEntity("ground");
            UX_CHECK(sphere && floor, "ground fixtures created");
            RigidBody3DComponent dynamic;
            dynamic.Type = BodyType::Dynamic;
            sphere->AddOrReplaceComponent<RigidBody3DComponent>(dynamic);
            SphereFixture3DComponent ball;
            ball.Property.m_Position = {0, 2.99f, 0};
            sphere->AddOrReplaceComponent<SphereFixture3DComponent>(ball);
            sphere->Transform().Translation = ball.Property.m_Position;
            RigidBody3DComponent stationary;
            stationary.Type = BodyType::Static;
            floor->AddOrReplaceComponent<RigidBody3DComponent>(stationary);
            BoxFixture3DComponent ground;
            ground.Shape.halfExtents = {20, 2, 20};
            ground.Property.m_InvMass = 0;
            floor->AddOrReplaceComponent<BoxFixture3DComponent>(ground);
            UX_CHECK(scene.OnRuntimeStart(), "start contact scene");
            const auto sphereState = scene.GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(*scene.GetBodyBinding(*sphere));
            const auto groundState = scene.GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(*scene.GetBodyBinding(*floor));
            UX_CHECK(sphereState && groundState && sphereState->type == BodyType::Dynamic &&
                sphereState->inverseMass > 0 && groundState->type == BodyType::Static && groundState->inverseMass == 0,
                "ground fixture contains actual Dynamic Sphere and Static floor");
            UX_CHECK(std::abs(groundState->worldBounds.maxs.y - 2.f) < .00001f &&
                std::abs(sphereState->worldBounds.mins.y - 1.99f) < .00001f,
                "native ground bounds prove the intended resting contact before any Apply");
            Harness ui(scene, resources, *sphere);
            auto& physics = *scene.GetPhysicsSystem();
            const auto handle = *scene.GetBodyBinding(*sphere);
            const auto original = *physics.ReadBodyCollider(handle);
            ui.paused = false; scene.SetPaused(false);
            UX_CHECK(ui.Type("Capsule") && ui.Click("Apply"), "ground safe Apply");
            ui.Record("ground default Capsule safe rejection");
            UX_CHECK(ui.editor.confirmation && !ui.editor.result->committed,
                "ground uncertain risk is fail closed");
            UX_CHECK(!ui.paused && ui.editor.temporaryPause && scene.IsPaused(), "running simulation gets separate temporary confirmation pause");
            UX_CHECK(ui.Click("Cancel") && !ui.editor.confirmation &&
                !ui.editor.preview && physics.ReadBodyCollider(handle)->identity == original.identity,
                "cancel clears preview and leaves native shape unchanged");
            UX_CHECK(!ui.paused && !ui.editor.temporaryPause && !scene.IsPaused(), "Cancel restores Running");
            UX_CHECK(ui.Click("Apply") && ui.Number("Half Length", "1.2"), "edit warning dimensions");
            UX_CHECK(!ui.editor.confirmation && !ui.editor.temporaryPause && !scene.IsPaused(),
                "editing dimensions invalidates confirmation and restores Running");
            UX_CHECK(ui.Click("Apply"), "assess enlarged manual Capsule");
            ui.Record("ground proven worsened overlap");
            UX_CHECK(ui.editor.confirmation && ui.editor.result->outcome == ColliderAdmissionOutcome::RejectedRisk,
                "ground known overlap is classified separately");
            const auto stale = physics.ReplaceBodyColliderConfirmed(handle, CapsuleShapeDesc{1, 1.3f}, *ui.editor.confirmation);
            UX_CHECK(!stale.committed && stale.error == ColliderAdmissionError::StaleConfirmation &&
                physics.ReadBodyCollider(handle)->identity == original.identity, "API rejects stale descriptor consent");
            UX_CHECK(ui.Click("Replace Anyway"), "explicit known-risk confirmation widget");
            ui.Record("ground confirmed replacement");
            UX_CHECK(ui.editor.result->committed && physics.ReadBodyCollider(handle)->type == ShapeType::Capsule,
                "ground real Capsule commit");
            UX_CHECK(!ui.paused && !ui.editor.temporaryPause && !scene.IsPaused(), "confirmed readback automatically resumes Running");
            scene.SetPaused(false);
            for (unsigned step = 0; step < 60; ++step) scene.Update(Timestep(1.0 / 60));
            const auto body = physics.GetPhysicsWorld()->ReadBody(handle);
            UX_CHECK(body && Math::IsFinite(body->position) && Math::IsFinite(body->linearVelocity),
                "ground normal solver remains finite for 60 updates");
        }
        {
            Harness ui(live, resources, stackBox);
            auto& physics = *live.GetPhysicsSystem();
            const auto handle = *live.GetBodyBinding(stackBox);
            const auto original = *physics.ReadBodyCollider(handle);
            UX_CHECK(ui.Type("Capsule") && ui.editor.fitMesh && ui.Click("Apply"),
                "actual SphereBoxStack fitted request through widgets");
            ui.Record("SphereBoxStack safe fitted rejection");
            UX_CHECK(ui.editor.result && !ui.editor.result->committed && ui.editor.confirmation &&
                physics.ReadBodyCollider(handle)->identity == original.identity,
                "stack safe admission leaves Box unchanged");
            ui.selected = sourceSphere; ui.Frame();
            UX_CHECK(!ui.editor.confirmation, "selection change expires confirmation");
            ui.selected = stackBox; ui.Frame();
            UX_CHECK(ui.Type("Capsule") && ui.editor.fitMesh && ui.Click("Apply"), "reassess selected stack Box");
            if (ui.editor.result->outcome != ColliderAdmissionOutcome::RejectedRisk)
                UX_CHECK(ui.Click("Advanced"), "open explicit unverified-risk advanced warning");
            UX_CHECK(ui.Click(ui.editor.result->outcome == ColliderAdmissionOutcome::RejectedRisk
                ? "Replace Anyway" : "Force Replace"), "explicit stack confirmation widget");
            ui.Record("SphereBoxStack confirmed fitted replacement");
            auto active = physics.ReadBodyCollider(handle);
            UX_CHECK(ui.editor.result->committed && active && active->type == ShapeType::Capsule &&
                active->identity != original.identity, "actual stack Capsule owned by Physics");
            UX_CHECK(ui.paused && live.IsPaused() && !ui.editor.temporaryPause,
                "user paused before Apply remains paused after confirmation");
            std::array<FrameDebugLine, 1024> storage;
            DebugGeometry actual(storage);
            const auto pose = physics.ReadBodyPose(handle);
            UX_CHECK(physics.AppendPresentationGeometry(actual, handle, pose->position, pose->orientation) &&
                actual.Lines().size() > 12, "active Capsule drives debug geometry after commit");
            live.SetPaused(false);
            for (unsigned step = 0; step < 60; ++step) live.Update(Timestep(1.0 / 60));
            const auto body = physics.GetPhysicsWorld()->ReadBody(handle);
            UX_CHECK(body && Math::IsFinite(body->position) && Math::IsFinite(body->linearVelocity),
                "stack normal solver remains finite for 60 updates");
        }
        return true;
    }
#undef UX_CHECK
}

namespace Rbs
{
    bool ColliderSmokeControlPoint(const char* label, Vec2f& point)
    {
        const auto found = ColliderEditorProbe::controls.find(label);
        if (found == ColliderEditorProbe::controls.end()) return false;
        point = {found->second.x, found->second.y};
        return true;
    }

    std::size_t ColliderSmokePendingCapsules()
    {
        std::size_t count = colliderEditor.preview && colliderEditor.PendingType() == ShapeType::Capsule ? 1 : 0;
        for (const auto& draft : otherColliderEditors)
            count += draft.preview && draft.PendingType() == ShapeType::Capsule ? 1 : 0;
        return count;
    }

    bool RunColliderEditorSmoke(_Scene& scene, SceneRenderResources& resources, const char* path)
    {
        using namespace ColliderEditorProbe;
        if (fopen_s(&output, path, "w") || !output) return false;
        std::fprintf(output, "SCOPE\tActual ImGui widget event path; fixture selection; nonvisual; not Owner acceptance\n");
        const bool passed = Run(scene, resources) && LayersAndMultiPreview(scene, resources);
        std::fprintf(output, "SUMMARY\t%s\tchecks=%u\n", passed ? "PASS" : "FAIL", checks);
        std::fclose(output); output = nullptr;
        return passed;
    }
}
