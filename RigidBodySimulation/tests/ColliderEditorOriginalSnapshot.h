#pragma once
// Frozen pre-revision Inspector, test-only. Replays the originally failed input
// sequence with added observation; never calls Apply. Source remains preserved
// in revision-01/source-before/RigidBodySimulation/src/RbsUi.cpp.
namespace Rbs::OriginalWidgetRepro
{
    struct ColliderInspector
    {
        PhysicsBodyHandle target{};
        int replacement{};
        bool fitMesh{};
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

    static glm::vec4 PreviewColor(const ColliderInspector& editor, const PhysicsSystem& physics)
    {
        if (editor.result && editor.previewContext == physics.ColliderContextRevision())
            for (const auto& pair : editor.result->pairs)
                if (pair.newOutcome == ColliderPairOutcome::Contact &&
                    (pair.oldOutcome == ColliderPairOutcome::SeparatedByBounds || pair.comparableMeasure))
                    return {1, .18f, .15f, 1};
        return {1, .65f, .15f, 1};
    }

    bool HasColliderEditorPreview(_Scene& scene, const _Entity& selected)
    {
        if (!colliderEditor.preview) return false;
        const auto binding = scene.GetBodyBinding(selected);
        if (!binding || *binding != colliderEditor.target)
        {
            colliderEditor.preview.reset();
            colliderEditor.confirmation.reset();
            return false;
        }
        return true;
    }

    DebugGeometryResult AppendColliderEditorPreview(_Scene& scene, const _Entity& selected,
        DebugGeometry& out)
    {
        if (!HasColliderEditorPreview(scene, selected)) return {};
        auto& physics = *scene.GetPhysicsSystem();
        const auto pose = physics.ReadBodyPose(colliderEditor.target);
        if (!pose) return std::unexpected(DebugGeometryCode::InvalidInput);
        std::array<FrameDebugLine, 512> storage;
        DebugGeometry lines(storage);
        if (auto result = physics.AppendPresentationGeometry(lines, colliderEditor.target,
                pose->position, pose->orientation); !result) return result;
        const auto currentCount = lines.Lines().size();
        if (auto result = PhysicsSystem::AppendColliderPreview(lines, *colliderEditor.preview,
                pose->position, pose->orientation, PreviewColor(colliderEditor, physics)); !result) return result;
        for (std::size_t i = 0; i < lines.Lines().size(); ++i)
        {
            storage[i].depth = DebugDepth::Overlay;
            if (i < currentCount) storage[i].color = {0, 1, 1, 1};
        }
        return out.Append(lines.Lines());
    }

    static void DrawColliderVisual(ColliderInspector& editor, PhysicsSystem& physics, PhysicsBodyHandle handle)
    {
        std::array<FrameDebugLine, 512> storage;
        DebugGeometry geometry(storage);
        const auto pose = physics.ReadBodyPose(handle);
        if (!pose) return;
        if (!physics.AppendPresentationGeometry(geometry, handle, {}, pose->orientation)) return;
        const auto currentCount = geometry.Lines().size();
        const auto proposedColor = PreviewColor(editor, physics);
        if (editor.preview && !PhysicsSystem::AppendColliderPreview(geometry, *editor.preview,
                {}, pose->orientation, proposedColor)) return;
        const auto start = ImGui::GetCursorScreenPos();
        const ImVec2 size{ImGui::GetContentRegionAvail().x, 145};
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
        const float zoom = (std::min)(size.x, size.y - 25) * .43f / extent;
        const auto project = [&](const Vec3f& point)
        {
            const auto rotated = rotation * point;
            return ImVec2{start.x + size.x * .5f + rotated.x * zoom,
                start.y + (size.y - 20) * .5f - rotated.y * zoom};
        };
        for (std::size_t i = 0; i < geometry.Lines().size(); ++i)
        {
            const auto& line = geometry.Lines()[i];
            const auto color = i < currentCount ? IM_COL32(0, 220, 230, 180)
                : ImGui::ColorConvertFloat4ToU32({proposedColor.r, proposedColor.g, proposedColor.b, 1});
            draw->AddLine(project(line.start), project(line.end), color, i < currentCount ? 1.f : 2.f);
        }
        draw->AddText({start.x + 7, start.y + size.y - 19}, IM_COL32(210, 215, 220, 255),
            editor.preview ? (proposedColor.g < .3f ? "Cyan: current | Red: overlap risk" :
                "Cyan: current | Orange: proposed") : "Current collider | Drag to rotate");
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

    static void ColliderDimensions(const ColliderShapeState& shape)
    {
        if (!shape.editable) return;
        if (const auto* sphere = std::get_if<SphereShapeDesc>(&shape.dimensions))
            ImGui::Text("Radius %.6g", double(sphere->radius));
        else if (const auto* capsule = std::get_if<CapsuleShapeDesc>(&shape.dimensions))
            ImGui::Text("Radius %.6g / segment half length %.6g", double(capsule->radius), double(capsule->halfLength));
        else if (const auto* box = std::get_if<BoxShapeDesc>(&shape.dimensions))
            ImGui::Text("Half extents %.6g, %.6g, %.6g", double(box->halfExtents.x),
                double(box->halfExtents.y), double(box->halfExtents.z));
    }

    static void DrawColliderInspector(ColliderInspector& editor, _Scene& scene,
        SceneRenderResources& resources, const _Entity& entity, PhysicsBodyHandle handle,
        bool& paused, bool& showCollider)
    {
        auto& physics = *scene.GetPhysicsSystem();
        auto current = physics.ReadBodyCollider(handle);
        if (!current)
        {
            editor.confirmation.reset();
            editor.preview.reset();
            ImGui::TextUnformatted("Current Collider: unavailable");
            return;
        }
        if (editor.target != handle)
        {
            editor = ColliderInspector{};
            editor.target = handle;
            editor.Revert(*current);
        }
        else if (editor.activeIdentity != current->identity)
        {
            editor.Revert(*current);
            editor.status = "Collider changed; edits refreshed.";
        }

        ImGui::Separator();
        ImGui::Text("Current Collider: %s", ColliderTypeName(current->type));
        constexpr const char* types[]{"Sphere", "Box", "Capsule"};
        bool edited = editor.preview && editor.activeRevision != current->revision;
        editor.activeRevision = current->revision;
        if (editor.preview && editor.fitMesh && entity.HasAllComponents<MeshRendererComponent>())
        {
            edited |= editor.previewScale != entity.GetComponent<Transform3DComponent>().Scale ||
                editor.previewMesh != entity.GetComponent<MeshRendererComponent>().mesh;
        }
        const bool typeOpen = ImGui::BeginCombo("Proposed Collider Type", types[editor.replacement]);
        NoteColliderControl("New Collider Type");
        if (typeOpen)
        {
            for (int index = 0; index < 3; ++index)
            {
                if (ImGui::Selectable(types[index], index == editor.replacement))
                {
                    editor.replacement = index;
                    edited = true;
                }
                NoteColliderControl(types[index]);
            }
            ImGui::EndCombo();
        }
        edited |= ImGui::Checkbox("Fit to Mesh", &editor.fitMesh);
        NoteColliderControl("Fit to Mesh");
        if (!editor.fitMesh)
        {
            if (editor.replacement == 1)
            {
                edited |= ImGui::InputFloat3("Half extents", &editor.halfExtents.x);
                edited |= ImGui::InputFloat3("Center", &editor.center.x);
            }
            else
            {
                edited |= ImGui::InputFloat("Radius", &editor.radius);
                NoteColliderControl("Radius");
                if (editor.replacement == 2)
                {
                    edited |= ImGui::InputFloat("Segment half length", &editor.halfLength);
                    NoteColliderControl("Segment half length");
                }
            }
        }
        if (edited)
        {
            editor.confirmation.reset();
            editor.status = "Pending edit";
        }

        const auto prepare = [&]() -> std::expected<CollisionShapeDesc, ColliderPreparationFailure>
        {
            if (editor.fitMesh) return PrepareMeshColliderAdmission(scene, resources, entity, editor.PendingType());
            return editor.Manual();
        };

        // Automatic read-only preview on edits; Apply always prepares and
        // assesses again. A preview can never display a committed status.
        if (edited)
        {
            editor.before.reset();
            editor.after.reset();
            editor.preview.reset();
            auto description = prepare();
            if (description)
            {
                editor.result = scene.AssessBodyColliderReplacement(entity, *description, &editor.preview);
                editor.previewContext = physics.ColliderContextRevision();
                editor.previewScale = entity.GetComponent<Transform3DComponent>().Scale;
                if (entity.HasAllComponents<MeshRendererComponent>())
                    editor.previewMesh = entity.GetComponent<MeshRendererComponent>().mesh;
                editor.status = editor.result->outcome == ColliderAdmissionOutcome::NoAddedOverlap
                    ? "Preview ready; not applied." : PhysicsSystem::CanConfirmColliderRisk(*editor.result)
                    ? "Preview has overlap risk; not applied." : ColliderFailureStatus(editor.result->error);
            }
            else
            {
                editor.result.reset();
                editor.status = ColliderFailureStatus(description.error().code);
            }
        }
        if (editor.fitMesh && editor.result)
        {
            ColliderShapeState fitted;
            fitted.editable = true;
            fitted.dimensions = editor.result->descriptor;
            ColliderDimensions(fitted);
        }
        DrawColliderVisual(editor, physics, handle);

        // Only a pending warning needs a context check. Ordinary rendering does
        // not assess geometry, fit meshes or scan neighboring bodies.
        if (editor.confirmation)
        {
            const auto description = prepare();
            if (!description || !physics.IsColliderConfirmationCurrent(*editor.confirmation, handle, *description))
            {
                editor.confirmation.reset();
                editor.status = "Context changed; apply again.";
            }
        }

        const auto finish = [&](ColliderAdmissionReport result, bool force)
        {
            editor.confirmation.reset();
            editor.after.reset();
            editor.result = std::move(result);
            if (editor.result->committed)
            {
                editor.preview.reset();
                auto readback = physics.ReadBodyCollider(handle);
                if (readback && readback->type == editor.PendingType())
                {
                    editor.after = *readback;
                    editor.activeIdentity = readback->identity;
                    editor.activeRevision = readback->revision;
                    editor.status = force ? "Applied (risk accepted)." : "Applied.";
                    showCollider = true;
                }
                else editor.status = "Commit readback failed; inspect Diagnostics.";
            }
            else if (PhysicsSystem::CanConfirmColliderRisk(*editor.result))
            {
                // Freeze only while consent is pending. The visible Pause
                // control stays on; the user resumes ordinary simulation.
                paused = true;
                scene.SetPaused(true);
                auto confirmation = physics.PrepareColliderConfirmation(*editor.result);
                if (confirmation)
                {
                    editor.confirmation = std::move(*confirmation);
                    editor.status = "Unchanged; confirmation required.";
                }
                else editor.status = ColliderFailureStatus(confirmation.error());
            }
            else editor.status = ColliderFailureStatus(editor.result->error);
        };

        const bool apply = ImGui::Button("Apply");
        NoteColliderControl("Apply");
        if (apply)
        {
            editor.before = *current;
            const auto description = prepare();
            if (description) finish(scene.ReplaceBodyColliderIfNoAddedOverlap(entity, *description), false);
            else
            {
                ColliderAdmissionReport failure;
                failure.error = description.error().code;
                failure.reason = description.error().reason;
                finish(std::move(failure), false);
            }
        }
        ImGui::SameLine();
        const bool revert = ImGui::Button("Revert");
        NoteColliderControl("Revert");
        if (revert)
        {
            editor.Revert(*current);
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
                editor.confirmation.reset();
                editor.preview.reset();
                editor.status = "Cancelled; collider unchanged.";
            }
            ImGui::SameLine();
            const bool advanced = known || ImGui::TreeNode("Advanced");
            if (!known) NoteColliderControl("Advanced");
            if (advanced)
            {
                if (!known) ImGui::TextUnformatted("Safety is not certified.");
                const char* confirmLabel = known ? "Replace Anyway" : "Force Replace";
                const bool confirm = ImGui::Button(confirmLabel);
                NoteColliderControl(confirmLabel);
                if (confirm)
                {
                    const auto description = prepare();
                    if (description && editor.confirmation)
                        finish(scene.ReplaceBodyColliderConfirmed(entity, *description, *editor.confirmation), true);
                }
                if (!known) ImGui::TreePop();
            }
        }
        ImGui::TextUnformatted(editor.status);
        if (ImGui::TreeNode("Diagnostics (Developer)"))
        {
            ImGui::Text("Body %u / generation %llu / world %llu", handle.index,
                static_cast<unsigned long long>(handle.generation), static_cast<unsigned long long>(handle.registry));
            // Read again after an action; do not display the pre-commit label as
            // current, or mistake the pending descriptor for actual dimensions.
            const auto active = physics.ReadBodyCollider(handle);
            if (active)
            {
                ImGui::Text("Physics Shape readback: %s", ColliderTypeName(active->type));
                ColliderDimensions(*active);
            }
            if (editor.result)
            {
                const auto& result = *editor.result;
                ImGui::Text("Outcome %d / error %d / committed %s", int(result.outcome), int(result.error),
                    result.committed ? "yes" : "no");
                ImGui::TextWrapped("%s", result.reason);
                if (const auto* capsule = std::get_if<CapsuleShapeDesc>(&result.descriptor))
                    ImGui::Text("Requested radius %.9g / half length %.9g", double(capsule->radius), double(capsule->halfLength));
                if (editor.before) ImGui::Text("Before: %s", ColliderTypeName(editor.before->type));
                if (editor.after) ImGui::Text("Confirmed after: %s", ColliderTypeName(editor.after->type));
                ImGui::Text("Assessment %.3f ms / %zu pairs", result.elapsedMs, result.pairs.size());
                for (const auto& pair : result.pairs)
                {
                    ImGui::TextWrapped("Body %u: %s", pair.other.index, pair.reason);
                    ImGui::Text("Classification %d -> %d / %.9g -> %.9g", int(pair.oldOutcome),
                        int(pair.newOutcome), pair.oldMeasure, pair.newMeasure);
                }
            }
            ImGui::TreePop();
        }
    }

}
