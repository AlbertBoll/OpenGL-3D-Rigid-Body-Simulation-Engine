// Included by RbsEditorUi.cpp: contextual UI plus the existing safe Update boundary.
bool RigidBodySimulationApp::ResolveImpulseTargets(bool saved,std::vector<Rbs::Editor::ImpulseTarget>& out)
{
    namespace E=Rbs::Editor;
    if(!m_ActiveScene || m_SceneRequest || m_BackendRequest){m_Impulse.diagnostic="Wait for the scene/backend transaction.";return false;}
    if(saved && (m_Impulse.savedTargets.empty() || m_Impulse.savedPreset!=EditorPresetKey())) {
        m_Impulse.diagnostic="Saved targets belong to a different scene configuration. Select current bodies and save a new experiment.";return false;
    }
    auto& scene=*m_ActiveScene;auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();
    if(saved && m_Impulse.savedGeneration!=m_SceneGeneration) {
        // Re-resolve preset keys AND immutable authored signatures; never old handles/names.
        std::map<std::uint64_t,EntityRenderId> identities;
        for(auto id:scene.GetAllEntitiesWith<E::PresetIdentity>()) {
            const _Entity e{id,&scene};const auto key=e.GetComponent<E::PresetIdentity>().key;
            if(!identities.emplace(key,scene.RenderData().Identify(e).value_or(EntityRenderId{})).second) {
                m_Impulse.diagnostic="Ambiguous preset identities; saved target resolution rejected.";return false;
            }
        }
        out.reserve(m_Impulse.savedTargets.size());
        for(const auto& old:m_Impulse.savedTargets) {
            const auto found=identities.find(old.key);
            if(!old.key || found==identities.end()) {m_Impulse.diagnostic="A saved target was transient or removed. Reselect current bodies; no impulse applied.";return false;}
            const auto resolved=scene.RenderData().Resolve(found->second);
            if(!resolved || _Entity{*resolved,&scene}.GetComponent<E::PresetIdentity>().signature!=old.signature) {
                m_Impulse.diagnostic="Authored target changed; signature mismatch. No impulse applied.";return false;
            }
            out.push_back({found->second});
        }
    } else if(saved)out=m_Impulse.savedTargets;
    else {
        out.reserve(m_EditorSelection.count);
        for(auto id:m_EditorSelection.ids)out.push_back({id});
    }
    if(out.empty() || out.size()>PhysicsWorld::MaxImpulseBodies){m_Impulse.diagnostic="Select 1..4096 Dynamic rigidbodies. No subset is applied.";return false;}
    for(auto& target:out) {
        const auto id=scene.RenderData().Resolve(target.entity);
        if(!id){m_Impulse.diagnostic="A selected entity is stale; entire impulse rejected.";return false;}
        const _Entity entity{*id,&scene};const auto body=scene.GetBodyBinding(entity);
        if(!body){m_Impulse.diagnostic="Every target must have a live Dynamic Rigidbody; non-physics targets reject the complete batch.";return false;}
        const auto motion=world.ReadImpulseMotion(*body);
        if(!motion){m_Impulse.diagnostic=std::string(motion.error().operation);return false;}
        target.body=*body;target.name=entity.Name();
        if(entity.HasAllComponents<E::PresetIdentity>()) {
            const auto& preset=entity.GetComponent<E::PresetIdentity>();target.key=preset.key;target.signature=preset.signature;
        }
    }
    return true;
}

bool RigidBodySimulationApp::SaveImpulseExperiment()
{
    if(const auto* why=m_Impulse.parameters.Validate()){m_Impulse.diagnostic=why;return false;}
    try {
        std::vector<Rbs::Editor::ImpulseTarget> targets(std::size_t(0));
        if(!ResolveImpulseTargets(false,targets))return false;
        m_Impulse.savedTargets=std::move(targets);m_Impulse.savedParameters=m_Impulse.parameters;
        m_Impulse.savedGeneration=m_SceneGeneration;m_Impulse.savedPreset=EditorPresetKey();
        m_Impulse.diagnostic="Experiment saved for this session. Reset does not replay it; Apply Saved Once resolves targets again.";return true;
    } catch(const std::bad_alloc&){m_Impulse.diagnostic="Saving experiment failed to allocate; no physics action.";return false;}
}

bool RigidBodySimulationApp::QueueImpulse(bool saved)
{
    if(!SandboxEnabled() || m_Impulse.pending || m_GroupTransform.dirty || m_GroupTransform.released) {
        m_Impulse.diagnostic="Finish the current action/Transform gesture before applying an impulse.";return false;
    }
    const auto parameters=saved?m_Impulse.savedParameters:m_Impulse.parameters;
    if(const auto* why=parameters.Validate()){m_Impulse.diagnostic=why;return false;}
    try {
        auto action=std::make_unique<Rbs::Editor::ImpulseRecord>();
        if(!ResolveImpulseTargets(saved,action->targets))return false;
        action->parameters=parameters;action->generation=m_SceneGeneration;action->preset=EditorPresetKey();action->backend=m_Backend;
        action->results.resize(action->targets.size());action->properties.resize(action->targets.size());
        if(action->Bytes()>Rbs::Editor::ImpulseEditor::RecordBytes) {m_Impulse.diagnostic="Impulse record exceeds 8 MiB budget; no partial action.";return false;}
        if(m_Impulse.nextSerial==UINT64_MAX){m_Impulse.diagnostic="Impulse sequence exhausted; restart editor.";return false;}
        action->serial=m_Impulse.nextSerial++;
        m_Impulse.pendingSelectionRevision=m_SelectionRevision;m_Impulse.pendingUsesSaved=saved;m_Impulse.pending=std::move(action);
        m_Impulse.diagnostic="Queued once for the next safe Update boundary.";return true;
    } catch(const std::bad_alloc&){m_Impulse.diagnostic="Impulse command allocation failed; no physics action.";return false;}
}

void RigidBodySimulationApp::ProcessImpulseCommand()
{
    auto action=std::move(m_Impulse.pending);if(!action)return; // Consume, including rejection; never retry each frame.
    if(!m_ActiveScene || m_SceneRequest || m_BackendRequest || action->generation!=m_SceneGeneration ||
        action->backend!=m_Backend || (!m_Impulse.pendingUsesSaved && m_Impulse.pendingSelectionRevision!=m_SelectionRevision)) {
        m_Impulse.diagnostic="Scene/backend/selection changed; queued impulse cancelled without execution.";return;
    }
    try {
        auto& scene=*m_ActiveScene;auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();
        std::vector<PhysicsImpulseCommand> commands(action->targets.size());
        for(std::size_t i=0;i<commands.size();++i) {
            const auto& target=action->targets[i];const auto id=scene.RenderData().Resolve(target.entity);
            if(!id || scene.GetBodyBinding(_Entity{*id,&scene})!=target.body) {m_Impulse.diagnostic="Stale ECS/body binding; entire impulse cancelled.";return;}
            const auto motion=world.ReadImpulseMotion(target.body);
            const auto props=world.ReadBodyProperties(target.body);
            if(!motion || !props){m_Impulse.diagnostic="A target is no longer an eligible Dynamic body; no subset applied.";return;}
            commands[i]=action->parameters.Command(target.body,*motion);action->properties[i]=*props;
        }
        if(m_Backend==PhysicsBackend::Jolt) {
            const auto settings=world.ReadJoltSettings();if(!settings){m_Impulse.diagnostic="Jolt settings readback unavailable; experiment cancelled.";return;}action->settings=*settings;
        }
        action->tick=scene.GetPhysicsSystem()->GetCompletedSteps();
        std::string success="Applied once (#"+std::to_string(action->serial)+") to "+std::to_string(commands.size())+" / "+std::to_string(commands.size())+" bodies; authoritative velocity readback verified. No physics step added.";
        auto result=world.ApplyImpulseBatch(commands);
        if(!result) {
            m_Impulse.diagnostic=std::string(result.error().operation);
            if(result.error().code==PhysicsBodyErrorCode::NonFiniteResult)SetSimulationState(Rbs::SimulationState::Paused);
            return;
        }
        action->results.swap((*result)->bodies);
        m_Impulse.Store(std::move(action));
        m_Impulse.diagnostic=std::move(success);
    } catch(const std::bad_alloc&) {m_Impulse.diagnostic="Impulse preparation allocation failed; inspect latest record before retrying.";}
}

void RigidBodySimulationApp::DrawImpulseProperties()
{
    namespace E=Rbs::Editor;auto& editor=m_Impulse;auto& p=editor.parameters;
    if(!E::Component("Physics Actions",false,"Physics / Apply Impulse"))return;
    ImGui::TextUnformatted("Apply Impulse");
    std::size_t dynamic=0,fixed=0,nonBodies=0;
    auto& world=*m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    for(auto id:m_EditorSelection.ids) {
        const auto resolved=m_ActiveScene->RenderData().Resolve(id);
        if(!resolved){++nonBodies;continue;}
        const auto handle=m_ActiveScene->GetBodyBinding(_Entity{*resolved,m_ActiveScene.get()});
        if(!handle){++nonBodies;continue;}
        const auto props=world.ReadBodyProperties(*handle);
        if(!props)++nonBodies;else if(props->type==BodyType::Dynamic)++dynamic;else ++fixed;
    }
    ImGui::TextWrapped("Selected %zu | Dynamic %zu | Static/Kinematic %zu | Unavailable/non-physics %zu",m_EditorSelection.count,dynamic,fixed,nonBodies);
    ImGui::TextWrapped("Apply to each selected body. Mixed unsupported targets reject the entire action. Joint-connected Dynamic bodies are supported.");
    ImGui::BeginDisabled(bool(editor.pending));
    {E::PropertyRow row("Mode");int mode=int(p.mode);if(ImGui::Combo("##ImpulseMode",&mode,"Impulse\0Velocity Change\0"))p.mode=PhysicsImpulseMode(mode);}
    E::Number(p.mode==PhysicsImpulseMode::Impulse?"Magnitude (Ns)":"Velocity delta (m/s)",p.magnitude,.1f,"Direction is normalized. Physical impulse depends on mass; Velocity Change converts through authoritative mass.",E::Numeric::Nonnegative<float>());
    {E::PropertyRow row("Fixed direction");if(ImGui::Button("+X"))p.direction={1,0,0};ImGui::SameLine();if(ImGui::Button("+Y"))p.direction={0,1,0};ImGui::SameLine();if(ImGui::Button("+Z"))p.direction={0,0,1};}
    E::XYZ("Custom direction",&p.direction.x,0,.05f,"Finite nonzero vector; normalized at Apply. World direction is independent of body and Editor Camera rotation.");
    {E::PropertyRow row("Space");int local=p.local;if(ImGui::Combo("##ImpulseSpace",&local,"World\0Local\0"))p.local=local!=0;}
    {E::PropertyRow row("Application point");int point=p.customPoint;if(ImGui::Combo("##ImpulsePoint",&point,"Center of Mass\0Custom offset from COM\0"))p.customPoint=point!=0;}
    if(p.customPoint)E::XYZ("COM offset (m)",&p.pointOffset.x,0,.05f,"Offset from each body's actual COM in the selected World/Local axes. No Transform scale multiplier.");
    ImGui::Checkbox("Optional angular action",&p.angularEnabled);
    if(p.angularEnabled)E::XYZ(p.mode==PhysicsImpulseMode::Impulse?"Angular impulse (Nms)":"Angular delta (rad/s)",&p.angular.x,0,.05f,"Independent angular action in the chosen axes; off-center linear impulse also adds torque. Backend speed limits remain enforced.");
    ImGui::Checkbox("Show impulse arrows",&editor.showArrows);
    if(const auto* why=p.Validate())ImGui::TextWrapped("%s",why);
    ImGui::BeginDisabled(!dynamic || fixed || nonBodies || p.Validate() || E::Numeric::HasError() || m_GroupTransform.dirty);
    if(ImGui::Button("Apply Once to Selection"))QueueImpulse();
    if(ImGui::Button("Save Temporary Experiment"))SaveImpulseExperiment();
    ImGui::EndDisabled();
    if(ImGui::TreeNode("Repeat experiment")) {
        ImGui::TextWrapped("Saved targets: %zu. Session-only; reset never replays. Transient/new entities cannot be resolved after replacement.",editor.savedTargets.size());
        ImGui::BeginDisabled(editor.savedTargets.empty());
        if(ImGui::Button("Load Saved Parameters")){p=editor.savedParameters;E::Numeric::Reset();}
        if(ImGui::Button("Restore Saved Selection")) {
            try {std::vector<E::ImpulseTarget> targets;if(ResolveImpulseTargets(true,targets)) {
                std::vector<EntityRenderId> ids;ids.reserve(targets.size());for(const auto& t:targets)ids.push_back(t.entity);
                if(m_EditorSelection.SelectSet(m_ActiveScene->RenderData(),ids,E::EntitySelection::SetOperation::Replace)){SynchronizeEditorSelection();m_HierarchyRevealed=m_ActiveScene->RenderData().Identify(m_HoveredEntity).value_or(EntityRenderId{});}
            }} catch(const std::bad_alloc&){editor.diagnostic="Selection allocation failed.";}
        }
        if(ImGui::Button("Apply Saved Once"))QueueImpulse(true);
        ImGui::EndDisabled();
        if(ImGui::Button("Reset Scene (no replay)"))RequestScene(m_ActiveSelection);
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
    if(editor.recordCount && editor.records[editor.recordCount-1]->generation!=m_SceneGeneration)
        ImGui::TextWrapped("Last accepted record belongs to a previous scene generation. Reset did not replay it; current velocities are below.");
    if(!editor.diagnostic.empty())ImGui::TextWrapped("%s",editor.diagnostic.c_str());
    if(const auto handle=m_ActiveScene->GetBodyBinding(m_HoveredEntity)) {
        const auto motion=world.ReadImpulseMotion(*handle);
        if(motion) {
            ImGui::Text("Active v (m/s): %.4g, %.4g, %.4g",motion->linear.x,motion->linear.y,motion->linear.z);
            ImGui::Text("Active w (rad/s): %.4g, %.4g, %.4g",motion->angular.x,motion->angular.y,motion->angular.z);
        } else ImGui::TextWrapped("Velocity readback: %s",std::string(motion.error().operation).c_str());
    }
    ImGui::TextWrapped("Impulse is a one-shot action, not a material or Transform edit. Edit/Paused stores velocity without stepping. Resume observes the motion. Undo does not rewind physics; Reset restores authored conditions.");
    if(editor.recordCount && ImGui::TreeNode("Accepted experiment records")) {
        ImGui::TextWrapped("%zu records / %zu bytes (8 records / 8 MiB). Complete oldest records evicted; session-only.",editor.recordCount,editor.Bytes());
        for(std::size_t n=0;n<editor.recordCount;++n) {
            const auto& r=*editor.records[n];ImGui::PushID(int(n));
            if(ImGui::TreeNode("record","#%llu | %zu bodies | tick %llu | %s",static_cast<unsigned long long>(r.serial),r.results.size(),static_cast<unsigned long long>(r.tick),PhysicsBackendName(r.backend).data())) {
                ImGui::Text("Preset key %u | scene generation %llu",r.preset,static_cast<unsigned long long>(r.generation));
                if(r.settings)ImGui::TextWrapped("World velocity/position steps %d/%d, restitution threshold %.4g, collision substeps %d",r.settings->velocitySteps,r.settings->positionSteps,r.settings->minRestitutionVelocity,r.settings->collisionSteps);
                ImGui::BeginChild("ImpulseReadback",{0,E::Px(230)},true,ImGuiWindowFlags_HorizontalScrollbar);
                ImGuiListClipper clip;clip.Begin(int(r.results.size()),E::Px(110));
                while(clip.Step())for(int i=clip.DisplayStart;i<clip.DisplayEnd;++i) {
                    const auto y=ImGui::GetCursorPosY();const auto& a=r.results[i];const auto& t=r.targets[i];const auto& material=r.properties[i];
                    ImGui::Text("%s | entity %u:%u | body %u:%llu",t.name.c_str(),t.entity.index,t.entity.generation,a.body.index,static_cast<unsigned long long>(a.body.generation));
                    ImGui::Text("J = %.4g %.4g %.4g | L = %.4g %.4g %.4g",a.impulse.x,a.impulse.y,a.impulse.z,a.angularImpulse.x,a.angularImpulse.y,a.angularImpulse.z);
                    ImGui::Text("v: %.4g %.4g %.4g -> %.4g %.4g %.4g",a.before.linear.x,a.before.linear.y,a.before.linear.z,a.after.linear.x,a.after.linear.y,a.after.linear.z);
                    ImGui::Text("w: %.4g %.4g %.4g -> %.4g %.4g %.4g",a.before.angular.x,a.before.angular.y,a.before.angular.z,a.after.angular.x,a.after.angular.y,a.after.angular.z);
                    ImGui::Text("mass %.4g | friction %.4g | restitution %.4g | damping %.4g/%.4g",material.mass,material.material.friction,material.material.restitution,material.linearDamping,material.angularDamping);
                    ImGui::SetCursorPosY(y+E::Px(110));
                }
                ImGui::EndChild();ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

void RigidBodySimulationApp::DrawImpulseOverlay()
{
    if(!m_Impulse.showArrows || !m_CameraSnapshot || !m_ActiveScene || m_Impulse.parameters.Validate())return;
    namespace E=Rbs::Editor;const auto& p=m_Impulse.parameters;
    const auto vp=m_CameraSnapshot->Projection()*m_CameraSnapshot->View();const auto lo=m_ViewportBounds[0],hi=m_ViewportBounds[1];
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect({lo.x,lo.y},{hi.x,hi.y},true);
    const auto pixel=[&](const glm::vec4& v){return ImVec2{lo.x+(v.x/v.w*.5f+.5f)*(hi.x-lo.x),lo.y+(.5f-v.y/v.w*.5f)*(hi.y-lo.y)};};
    std::size_t arrows{};
    for(auto id:m_EditorSelection.ids) {
        if(!EditorEntityVisible(id))continue;
        const auto entity=m_ActiveScene->RenderData().Resolve(id);if(!entity)continue;
        const auto handle=m_ActiveScene->GetBodyBinding(_Entity{*entity,m_ActiveScene.get()});if(!handle)continue;
        const auto motion=m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->ReadImpulseMotion(*handle);if(!motion)continue;
        const auto c=p.Command(*handle,*motion);const Vec3f start=c.point.value_or(motion->center);
        const auto direction=p.local?motion->orientation*Vec3f(glm::normalize(glm::dvec3(p.direction))):Vec3f(glm::normalize(glm::dvec3(p.direction)));
        if(!Math::IsFinite(start)||!Math::IsFinite(direction))continue;
        auto a=vp*glm::vec4(start,1),b=vp*glm::vec4(start+direction*3.f,1);bool visible=true;
        const auto finite=[](const glm::vec4& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w);};
        if(!finite(a)||!finite(b))continue;
        for(int axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
            const float x=a.w+sign*a[axis],y=b.w+sign*b[axis];
            if(x<0&&y<0){visible=false;break;}
            if((x<0)!=(y<0)){const auto cut=a+(b-a)*(x/(x-y));if(x<0)a=cut;else b=cut;}
        }
        if(!visible || !finite(a)||!finite(b) || a.w<=.001f || b.w<=.001f)continue;
        const auto from=pixel(a),to=pixel(b);const Vec2f delta{to.x-from.x,to.y-from.y};const float length=glm::length(delta);
        if(length<E::Px(2))continue;const auto unit=delta/length;const Vec2f side{-unit.y,unit.x};const float head=std::min(E::Px(8),length*.35f);
        const auto color=IM_COL32(107,204,255,235);draw->AddLine(from,to,color,E::Px(1.8f));
        draw->AddTriangleFilled(to,{to.x-unit.x*head+side.x*head*.45f,to.y-unit.y*head+side.y*head*.45f},{to.x-unit.x*head-side.x*head*.45f,to.y-unit.y*head-side.y*head*.45f},color);++arrows;
    }
    char label[160];std::snprintf(label,sizeof(label),"Impulse preview: %zu / %zu | %.4g %s | %s | arrows show direction",arrows,m_EditorSelection.count,p.magnitude,p.mode==PhysicsImpulseMode::Impulse?"Ns":"m/s",p.local?"Local":"World");
    const float wrap=std::max(E::Px(60),hi.x-lo.x-E::Px(24));
    const auto size=ImGui::CalcTextSize(label,nullptr,false,wrap);
    const ImVec2 pos{lo.x+E::Px(12),hi.y-size.y-E::Px(10)};
    draw->AddRectFilled({pos.x-E::Px(4),pos.y-E::Px(3)},{pos.x+size.x+E::Px(4),pos.y+size.y+E::Px(3)},IM_COL32(23,28,35,225),E::Px(3));
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),pos,IM_COL32(160,220,255,255),label,nullptr,wrap);draw->PopClipRect();
}
