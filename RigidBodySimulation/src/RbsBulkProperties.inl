// Included by RbsEditorUi.cpp; existing contextual Properties and safe Update boundary.
void RigidBodySimulationApp::RefreshBulkBodyProperties()
{
    namespace B=Rbs::Editor::BulkPhysics;auto& b=m_BulkBodyEditor;
    b.bodies.clear();b.entities.clear();b.fields=b.mixed=0;b.supported=B::All;b.nonBodies=b.unsupported=b.readbackCount=0;
    b.queued=b.resetBaseline=b.readError=false;b.generation=m_SceneGeneration;b.selectionRevision=m_SelectionRevision;
    if(!m_ActiveScene)return;
    auto& world=*m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    b.bodies.reserve(m_EditorSelection.count);b.entities.reserve(m_EditorSelection.count);
    for(std::size_t i=0;i<m_EditorSelection.count;++i) {
        const auto id=m_EditorSelection.ids[i];const auto resolved=m_ActiveScene->RenderData().Resolve(id);
        if(!resolved){b.readError=true;continue;}const _Entity e{*resolved,m_ActiveScene.get()};
        if(!e.HasAllComponents<RigidBody3DComponent>()){++b.nonBodies;continue;}
        const auto handle=m_ActiveScene->GetBodyBinding(e);if(!handle){b.readError=true;continue;}
        const auto p=world.ReadBodyProperties(*handle);if(!p){b.readError=true;continue;}
        if(b.bodies.empty())b.draft=*p;else b.mixed|=B::Difference(b.draft,*p);
        b.supported&=B::Supported(m_Backend,p->type);b.bodies.push_back({*handle,*p,*p});b.entities.push_back(id);
    }
}
void RigidBodySimulationApp::ProcessBulkBodyProperties()
{
    namespace B=Rbs::Editor::BulkPhysics;auto& b=m_BulkBodyEditor;if(!b.queued)return;b.queued=false;
    if(!m_ActiveScene || b.generation!=m_SceneGeneration || b.selectionRevision!=m_SelectionRevision || m_SceneRequest || m_BackendRequest) {
        b.diagnostic="Selection/world changed; entire property batch cancelled.";return;
    }
    if(b.readError || !b.fields || b.bodies.empty()){b.diagnostic="Incomplete or empty property batch rejected.";return;}
    auto& world=*m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    b.unsupported=0;
    for(std::size_t i=0;i<b.bodies.size();++i) {
        const auto resolved=m_ActiveScene->RenderData().Resolve(b.entities[i]);
        if(!resolved || m_ActiveScene->GetBodyBinding(_Entity{*resolved,m_ActiveScene.get()})!=b.bodies[i].body) {
            b.diagnostic="Stale ECS/body binding; entire batch retained unchanged.";return;
        }
        auto& edit=b.bodies[i];if(b.fields&~B::Supported(m_Backend,edit.before.type))++b.unsupported;
        auto values=b.draft;
        if(b.resetBaseline) {
            const auto saved=std::find_if(m_JoltSettingsEditor.bodyBaseline.begin(),m_JoltSettingsEditor.bodyBaseline.end(),[&](const auto& p){return p.first==edit.body;});
            if(saved==m_JoltSettingsEditor.bodyBaseline.end()){b.diagnostic="Missing preserved baseline; entire batch rejected.";return;}
            values=saved->second;
        }
        edit.after=B::Merge(edit.before,values,b.fields);
    }
    if(b.unsupported){b.diagnostic="Selected fields unsupported by "+std::to_string(b.unsupported)+" bodies; no subset applied.";return;}
    const auto result=world.UpdateBodyPropertiesBatch(b.bodies);
    if(!result){
        b.diagnostic="Entire batch rejected: "+std::string(result.error().operation);
        if(result.error().code==PhysicsBodyErrorCode::NonFiniteResult){SetSimulationState(Rbs::SimulationState::Paused);b.diagnostic+=" Simulation paused.";}
        else b.diagnostic+=". Previous values retained.";
        return;
    }
    const auto fields=b.fields;const auto count=b.bodies.size();std::size_t read{};
    for(const auto& edit:b.bodies) {
        const auto actual=world.ReadBodyProperties(edit.body);
        if(!actual || *actual!=edit.after) {
            b.diagnostic="ERROR: post-Apply readback differs. Simulation paused; do not treat this batch as validated.";
            SetSimulationState(Rbs::SimulationState::Paused);return;
        }
        ++read;
        Log::GetCoreLogger()->info("BULK_BODY_READBACK registry={} index={} generation={} friction={} restitution={} linear_damping={} angular_damping={} gravity={} ccd={} sleeping={} velocity_override={} position_override={}",
            edit.body.registry,edit.body.index,edit.body.generation,actual->material.friction,actual->material.restitution,actual->linearDamping,actual->angularDamping,actual->gravityFactor,actual->continuousCollision,actual->allowSleeping,actual->velocityStepsOverride,actual->positionStepsOverride);
    }
    RefreshBulkBodyProperties();b.readbackCount=read;b.diagnostic="Applied atomically to "+std::to_string(count)+" bodies. Authoritative readback: "+std::to_string(read)+" / "+std::to_string(count)+" verified.";
    Log::GetCoreLogger()->info("BULK_BODY_APPLY count={} readback={} fields={} physics_tick={} backend={}",count,read,fields,m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps(),PhysicsBackendName(m_Backend));
}
void RigidBodySimulationApp::DrawBulkBodyProperties()
{
    namespace E=Rbs::Editor;namespace B=E::BulkPhysics;auto& b=m_BulkBodyEditor;
    if(b.generation!=m_SceneGeneration || b.selectionRevision!=m_SelectionRevision) {RefreshBulkBodyProperties();b.diagnostic.clear();}
    if(!E::Component("Rigidbody",false,"Shared Physics Properties"))return;
    b.unsupported=0;for(const auto& e:b.bodies)if(b.fields&~B::Supported(m_Backend,e.before.type))++b.unsupported;
    ImGui::TextWrapped("Selected %zu | Eligible %zu | Non-physics %zu | Unsupported %zu",m_EditorSelection.count,b.bodies.size()-b.unsupported,b.nonBodies,b.unsupported);
    if(b.readError)ImGui::TextWrapped("A selected body is unavailable. Entire Apply is disabled; reselect after the scene transaction.");
    ImGui::TextWrapped("Only edited fields apply to every selected body. Mixed means values differ. Non-physics entities are excluded explicitly.");
    ImGui::BeginDisabled(b.queued || b.readError || b.bodies.empty());
    const auto label=[&](const char* text,unsigned field) {
        std::array<char,96> textBuffer{};std::snprintf(textBuffer.data(),textBuffer.size(),"%s%s###Bulk%u",text,((b.mixed&field)&&(b.resetBaseline || !(b.fields&field)))?" (Mixed)":"",field);return textBuffer;
    };
    const auto edited=[&](unsigned field){if(b.resetBaseline){b.fields=0;b.resetBaseline=false;b.diagnostic="Baseline staging discarded; only newly edited fields will apply.";}b.fields|=field;};
    const auto number=[&](const char* text,unsigned field,auto& value,auto bounds,const char* help) {
        ImGui::BeginDisabled(!(b.supported&field));
        if(E::Number(label(text,field).data(),value,.01f,help,bounds)){edited(field);}
        ImGui::EndDisabled();
    };
    number("Friction",B::Friction,b.draft.material.friction,E::Numeric::Nonnegative<float>(),"Finite nonnegative. Product contact combination is unchanged.");
    number("Restitution",B::Restitution,b.draft.material.restitution,E::Numeric::Closed(0.f,1.f),"Body/material coefficient [0,1]. The restitution velocity threshold belongs to World Settings.");
    number("Linear damping",B::LinearDamping,b.draft.linearDamping,E::Numeric::Closed(0.f,1.f),"Jolt Dynamic bodies only.");
    number("Angular damping",B::AngularDamping,b.draft.angularDamping,E::Numeric::Closed(0.f,1.f),"Jolt Dynamic bodies only.");
    number("Gravity factor",B::Gravity,b.draft.gravityFactor,E::Numeric::Closed(0.f,10.f),"Jolt Dynamic bodies only.");
    const auto checkbox=[&](const char* text,unsigned field,bool& value) {
        E::PropertyRow row(text);ImGui::BeginDisabled(!(b.supported&field));
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,(b.mixed&field)&&(b.resetBaseline || !(b.fields&field)));
        if(ImGui::Checkbox(label("",field).data(),&value)){edited(field);}
        ImGui::PopItemFlag();ImGui::EndDisabled();
    };
    checkbox("CCD / LinearCast",B::CCD,b.draft.continuousCollision);checkbox("Sleeping enabled",B::Sleeping,b.draft.allowSleeping);
    if(ImGui::TreeNode("Solver iteration overrides")) {
        ImGui::BeginDisabled(!(b.supported&B::VelocitySteps));
        if(E::Number(label("Velocity",B::VelocitySteps).data(),b.draft.velocityStepsOverride,1.f,"0 inherits world; 2..255. Can affect the whole contact/constraint island.",E::Numeric::Closed(0,255),[](int v)->const char*{return v==1?"Use 0 or 2..255.":nullptr;})){edited(B::VelocitySteps);}
        ImGui::EndDisabled();
        number("Position",B::PositionSteps,b.draft.positionStepsOverride,E::Numeric::Closed(0,255),"0 inherits world. Jolt Dynamic only; island maximum applies.");
        ImGui::TreePop();
    }
    if(m_Backend==PhysicsBackend::Native)ImGui::TextWrapped("Native supports shared Friction and Restitution. Damping, gravity, CCD, sleep enable and solver overrides are fixed/unsupported.");
    else if(b.supported!=B::All)ImGui::TextWrapped("Mixed motion types: damping/gravity/iterations need Dynamic bodies; CCD/sleep need moving bodies. Disabled fields cannot apply to this complete selection.");
    ImGui::BeginDisabled(!b.fields || b.unsupported || E::Numeric::HasError());
    if(ImGui::Button("Apply All Bodies"))b.queued=true;
    ImGui::EndDisabled();
    if(ImGui::Button("Revert / Refresh")){RefreshBulkBodyProperties();b.diagnostic="Draft reverted to authoritative values.";E::Numeric::Reset();}
    if(ImGui::Button("Reset Shared to Baseline")) {
        bool complete=true;unsigned mixed{};PhysicsBodyProperties first{};
        for(std::size_t i=0;i<b.bodies.size();++i) {
            const auto saved=std::find_if(m_JoltSettingsEditor.bodyBaseline.begin(),m_JoltSettingsEditor.bodyBaseline.end(),[&](const auto& v){return v.first==b.bodies[i].body;});
            if(saved==m_JoltSettingsEditor.bodyBaseline.end()){complete=false;break;}
            if(!i)first=saved->second;else mixed|=B::Difference(first,saved->second);
        }
        if(complete){b.fields=b.supported;b.resetBaseline=true;b.draft=first;b.mixed=mixed;E::Numeric::Reset();b.diagnostic="Preserved per-body baseline staged for all supported shared fields. Apply required.";}
        else b.diagnostic="Baseline unavailable for a selected body; no reset staged.";
    }
    ImGui::EndDisabled();
    if(b.fields)ImGui::TextWrapped("%s",b.queued?"Queued at safe physics boundary":b.resetBaseline?"Preserved baseline staged":"Unapplied explicitly edited fields");
    if(!b.diagnostic.empty())ImGui::TextWrapped("%s",b.diagnostic.c_str());
    ImGui::TextWrapped("Session-only. Main Reset/scene reload restores authored presets. Bulk Physics Properties have no Undo/Redo history; use Revert before Apply or Reset Shared to Baseline. Ctrl+Z/Y remain Transform/rename/visibility history only.");
}
