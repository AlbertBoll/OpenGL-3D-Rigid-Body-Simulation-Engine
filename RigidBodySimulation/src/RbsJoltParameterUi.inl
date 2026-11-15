// Existing World Settings panel and owner-thread Update command.
void RigidBodySimulationApp::ProcessJoltSettingsCommand() {
    if(!m_ActiveScene)return;
    auto& e=m_JoltSettingsEditor;auto& w=*m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    if(e.generation!=m_SceneGeneration) {
        try {
            JoltSettingsEditor next;next.generation=m_SceneGeneration;
            if(auto p=w.ReadJoltSettings())next.draft=next.baseline=*p;
            next.bodyBaseline.reserve(w.GetBodyCount());
            for(std::size_t i=0;i<w.GetBodyCount();++i)if(auto h=w.BodyAt(i))if(auto p=w.ReadBodyProperties(*h))next.bodyBaseline.emplace_back(*h,*p);
            e=std::move(next);
        }catch(const std::bad_alloc&){e.feedback="Baseline allocation failed; parameter editing unavailable.";return;}
    }
    if(e.diagnosticsRequest){const bool value=*e.diagnosticsRequest;e.diagnosticsRequest.reset();
        if(auto r=w.EnableJoltDiagnostics(value);!r)e.feedback=std::string(r.error().operation);else e.diagnostics=value;}
    if(!std::exchange(e.queued,false))return;
    if(m_SceneRequest || m_BackendRequest || !SandboxEnabled()){e.feedback="Scene transaction active; parameter Apply rejected.";return;}
    if(m_SimulationState==Rbs::SimulationState::Running){e.feedback="Pause before applying experimental world settings.";return;}
    if(const auto* why=ValidateJoltSettings(e.draft)){e.feedback=why;return;}
    if(auto r=w.UpdateJoltSettings(e.draft);!r){e.feedback=std::string(r.error().operation);return;}
    if(auto actual=w.ReadJoltSettings()){e.draft=*actual;e.dirty=false;e.feedback="Applied at safe boundary; authoritative readback refreshed. Contacts invalidated; sleep tests restarted.";}
}
void RigidBodySimulationApp::DrawJoltSettings() {
    namespace E=Rbs::Editor;
    if(!m_ActiveScene || !ImGui::TreeNode("Jolt Physics"))return;
    auto& e=m_JoltSettingsEditor;auto& w=*m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    const auto actual=w.ReadJoltSettings(),defaults=w.ReadJoltNativeDefaults();
    if(actual && !e.dirty && !e.queued)e.draft=*actual;
    auto& p=e.draft;
    ImGui::TextWrapped("Experimental world overrides. Baseline preserved; no preset or qualification changes. Pause to Apply.");
    ImGui::BeginDisabled(!actual || e.generation!=m_SceneGeneration || e.queued || !SandboxEnabled() || m_SceneRequest.has_value());
    const auto check=[&](const char* label,bool& v,const char* tip){E::PropertyRow row(label,tip);e.dirty|=ImGui::Checkbox("##value",&v);};
    if(ImGui::TreeNodeEx("Solver",ImGuiTreeNodeFlags_DefaultOpen)) {
        e.dirty|=E::Number("Velocity iterations",p.velocitySteps,1.f,"mNumVelocitySteps. Native Jolt default 10. At least 2 for friction; editor maximum 255.",E::Numeric::Closed(2,255));
        e.dirty|=E::Number("Position iterations",p.positionSteps,1.f,"mNumPositionSteps. Native Jolt default 2. Zero disables position correction (diagnostic).",E::Numeric::Closed(0,255));
        e.dirty|=E::Number("Collision substeps",p.collisionSteps,1.f,"Actual collision-step argument to PhysicsSystem::Update. Baseline 1; editor budget 16. Fixed scheduler dt stays 1/60 s.",E::Numeric::Closed(1,16));ImGui::TreePop();
    }
    if(ImGui::TreeNodeEx("Collision Response",ImGuiTreeNodeFlags_DefaultOpen)) {
        e.dirty|=E::Number("Restitution min (m/s)",p.minRestitutionVelocity,.05f,"World threshold, not body restitution. Native Jolt default 1 m/s. Strictly positive.",E::Numeric::Positive<float>());
        e.dirty|=E::Number("Baumgarte",p.baumgarte,.01f,"Position error correction fraction. Jolt default .2. Experimental, not material stiffness.",E::Numeric::Closed(0.f,1.f));
        const auto square=[](float v)->const char*{return std::isfinite(v*v)?nullptr:"Squared distance must remain finite.";};
        e.dirty|=E::Number("Penetration slop (m)",p.penetrationSlop,.001f,"Jolt default .02 m. Allowed solver penetration, not editor placement tolerance.",E::Numeric::Nonnegative<float>(),square);
        e.dirty|=E::Number("Speculative dist (m)",p.speculativeDistance,.001f,"Jolt default .02 m. Large distances can create ghost contacts.",E::Numeric::Nonnegative<float>(),square);ImGui::TreePop();
    }
    if(ImGui::TreeNode("Sleeping")) {
        check("Allow sleeping",p.allowSleeping,"World gate; per-body Sleeping Enabled remains independent. Existing scheduler master gate still applies.");
        e.dirty|=E::Number("Time before sleep (s)",p.timeBeforeSleep,.01f,"Native Jolt default .5 s; positive.",E::Numeric::Positive<float>());
        e.dirty|=E::Number("Point speed (m/s)",p.pointVelocitySleepThreshold,.001f,"Three-point sleep test; native Jolt default .03 m/s, not a per-body threshold.",E::Numeric::Positive<float>());ImGui::TreePop();
    }
    if(ImGui::TreeNode("Advanced Diagnostics")) {
        ImGui::TextWrapped("Jolt DEBUG controls. Changing these can reduce stability or increase CPU cost.");
        check("Warm starting",p.warmStarting,"mConstraintWarmStart. Default true.");check("Body pair cache",p.bodyPairCache,"mUseBodyPairContactCache. Default true.");check("Manifold reduction",p.manifoldReduction,"mUseManifoldReduction. Default true.");
        const auto observed=w.ReadJoltDiagnostics();
        bool enabled=observed && observed->enabled;if(ImGui::Checkbox("Read-only contact sampling",&enabled))e.diagnosticsRequest=enabled;
        E::Tooltip("Opt-in observer overhead; bounded 8192 callback records per update. Does not modify contact settings. Post velocity is after the whole Update, not an isolated pair impulse.");
        if(observed && observed->enabled)ImGui::TextWrapped("Step %llu: %zu callbacks, %zu dropped; Jolt Update %.3f ms",observed->step,observed->callbacks,observed->overflow,observed->updateMilliseconds);
        ImGui::TreePop();
    }
    const auto* invalid=ValidateJoltSettings(p);
    ImGui::BeginDisabled(!e.dirty || invalid || E::Numeric::HasError() || m_SimulationState==Rbs::SimulationState::Running);
    if(ImGui::Button("Apply Jolt Settings"))e.queued=true;ImGui::EndDisabled();
    if(ImGui::Button("Revert Jolt Settings") && actual){p=*actual;e.dirty=false;e.feedback.clear();E::Numeric::Reset();}
    if(ImGui::Button("Reset to Preserved Baseline")){p=e.baseline;e.dirty=true;E::Numeric::Reset();}
    E::Tooltip("Stage world-entry baseline; Apply required. Main Reset/scene load rebuilds authored presets. Experimental settings do not persist across restart.");
    ImGui::EndDisabled();
    if(!actual)ImGui::TextWrapped("Native backend: Jolt settings unavailable. Native per-body sleep thresholds remain in Rigidbody Properties.");
    if(invalid)ImGui::TextWrapped("Invalid draft: %s",invalid);
    if(actual && defaults && ImGui::TreeNode("Effective / Jolt native defaults")) {
        const auto& a=*actual;const auto& d=*defaults;
        ImGui::TextWrapped("Velocity: %d / %d | Position: %d / %d | Collision substeps: %d / baseline 1",a.velocitySteps,d.velocitySteps,a.positionSteps,d.positionSteps,a.collisionSteps);
        ImGui::TextWrapped("Restitution minimum: %.6g / %.6g m/s | Baumgarte: %.6g / %.6g",a.minRestitutionVelocity,d.minRestitutionVelocity,a.baumgarte,d.baumgarte);
        ImGui::TextWrapped("Slop: %.6g / %.6g m | Speculative: %.6g / %.6g m",a.penetrationSlop,d.penetrationSlop,a.speculativeDistance,d.speculativeDistance);
        ImGui::TextWrapped("Sleeping: %s / ON | Dwell: %.6g / %.6g s | Point speed: %.6g / %.6g m/s",a.allowSleeping?"ON":"OFF",a.timeBeforeSleep,d.timeBeforeSleep,a.pointVelocitySleepThreshold,d.pointVelocitySleepThreshold);
        ImGui::TextWrapped("Warm start: %s / ON | Pair cache: %s / ON | Reduction: %s / ON",a.warmStarting?"ON":"OFF",a.bodyPairCache?"ON":"OFF",a.manifoldReduction?"ON":"OFF");ImGui::TreePop();
    }
    ImGui::TextWrapped("%s",e.queued?"Queued at safe boundary":e.dirty?"Unapplied experimental draft":"Effective values; preserved baseline unchanged");
    if(!e.feedback.empty())ImGui::TextWrapped("%s",e.feedback.c_str());
    ImGui::TreePop();
}
