#pragma once
#include "OwnerReviewProbe.h"
#ifdef GENGINE_OWNER_REVIEW_PROBE
#include "RbsEditorWidgets.h"
#include "../../external/glad/include/glad/glad.h"
#include <imgui/imgui_internal.h>
namespace Rbs
{
    struct OwnerReviewDriver
    {
        // selection: 0 none, 1 sphere, 2 ground, 3 eight spheres.
        struct Case {const char* name;int angle,selection;bool colliders{},profiler{},debug{},hideHighlight{},pick{},noShadows{},cohortOnly{},ortho{};};
        static constexpr Case cases[]{
            {"overhead-none",0,0},{"overhead-sphere",0,1},{"overhead-ground",0,2},{"overhead-multi",0,3},
            {"low-none",1,0},{"low-sphere",1,1},{"low-ground",1,2},{"low-multi",1,3},
            {"low-ground-colliders",1,2,true},{"low-ground-profiler",1,2,false,true},
            {"low-ground-debug",1,2,false,false,true},{"low-ground-no-highlight",1,2,false,false,false,true},
            {"low-ground-pick-once",1,2,false,false,false,false,true},
            {"low-ground-no-shadows",1,2,false,false,false,false,false,true}};
        // Opt-in bounded selection correction cases; the historical suite is unchanged.
        static constexpr Case selectionCases[]{
            {"none",0,0},{"one",0,101},{"64",0,164},{"65",0,165},{"180-hierarchy",0,280},
            {"180-highlight-off",0,280,false,false,false,true},
            {"181-with-helper",0,1001},{"181-highlight-off",0,1001,false,false,false,true},
            {"cohort-none",0,0,false,false,false,false,false,false,true},
            {"cohort-180-hierarchy",0,280,false,false,false,false,false,false,true},
            {"cohort-180-marquee",0,1000,false,false,false,false,false,false,true},
            {"cohort-180-off",0,280,false,false,false,true,false,false,true},
            {"ortho-180",0,280,false,false,false,false,false,false,false,true},
            {"ortho-181-helper",0,1001,false,false,false,false,false,false,false,true},
            {"ortho-181-off",0,1001,false,false,false,true,false,false,false,true}};
        static bool SelectionSuite(){return std::getenv("OWNER_REVIEW_SELECTION_SUITE")!=nullptr;}
        static bool TransformSuite(){return std::getenv("OWNER_REVIEW_TRANSFORM_SUITE")!=nullptr;}
        static constexpr Case transformCases[]{
            {"group-none",0,0},{"group-selected-180",0,280},{"group-idle-gizmo-180",0,280},
            {"group-preview-T-180",0,280},{"group-preview-R-180",0,280},{"group-preview-S-180",0,280}};
        static std::span<const Case> Cases(){return TransformSuite()?std::span<const Case>(transformCases):SelectionSuite()?std::span<const Case>(selectionCases):std::span<const Case>(cases);}
        static inline bool rectanglePending{};
        static inline PFNGLNAMEDBUFFERDATAPROC originalBufferData{};
        static inline PFNGLNAMEDBUFFERSUBDATAPROC originalBufferSubData{};
        static inline PFNGLBINDFRAMEBUFFERPROC originalBindFramebuffer{};
        static inline std::uint64_t bufferUpdates{},uploadBytes{},framebufferBinds{};
        static void APIENTRY BufferData(GLuint b,GLsizeiptr size,const void* data,GLenum usage){
            if(ReviewMetrics::counting){++bufferUpdates;uploadBytes+=data?size:0;}originalBufferData(b,size,data,usage);}
        static void APIENTRY BufferSubData(GLuint b,GLintptr offset,GLsizeiptr size,const void* data){
            if(ReviewMetrics::counting){++bufferUpdates;uploadBytes+=size;}originalBufferSubData(b,offset,size,data);}
        static void APIENTRY BindFramebuffer(GLenum target,GLuint b){
            if(ReviewMetrics::counting)++framebufferBinds;originalBindFramebuffer(target,b);}
        static inline unsigned frame{},index{},local{};
        static inline FILE* output{};
        static inline unsigned long long started{},previous{};
        static inline bool ready{};
        static bool RunningMode() {return std::getenv("OWNER_REVIEW_RUNNING")!=nullptr;}
        static bool Stiffness() {return std::getenv("OWNER_REVIEW_STIFFNESS")!=nullptr;}
        static void PhysicsSnapshot(RigidBodySimulationApp& app)
        {
            if(!Stiffness())return;
            static FILE* states{};static unsigned long long last=~0ull;
            auto* system=app.m_ActiveScene->GetPhysicsSystem();auto* world=system->GetPhysicsWorld();const auto tick=system->GetCompletedSteps();
            if(last==tick || tick%30)return;last=tick;
            if(!states){fopen_s(&states,"stiffness-readback.csv","w");if(!states)return;
                std::fprintf(states,"tick,index,type,mass,friction,restitution,linear_damping,angular_damping,gravity_factor,ccd,allow_sleep,shape,px,py,pz,vx,vy,vz,wx,wy,wz,sleeping\n");
                const auto gravity=world->GetGravity();const auto sleep=world->ReadWorldSleepSettings();
                std::fprintf(stdout,"STIFFNESS_READONLY backend=%s step=%.9g gravity=%.9g,%.9g,%.9g world_sleep=%.9g,%.9g\n",
                    ::GEngine::PhysicsBackendName(world->GetBackend()).data(),app.m_ActiveScene->GetPhysicsStep().count(),gravity.x,gravity.y,gravity.z,
                    sleep?sleep->pointVelocityThreshold:-1.f,sleep?sleep->timeBeforeSleep:-1.f);
            }
            for(std::size_t i=0;i<world->GetBodyCount();++i)
            {
                auto h=world->BodyAt(i);if(!h)continue;auto s=world->ReadBody(*h);auto p=world->ReadBodyProperties(*h);if(!s || !p)continue;
                std::fprintf(states,"%llu,%zu,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d\n",
                    tick,i,int(p->type),p->mass,p->material.friction,p->material.restitution,p->linearDamping,p->angularDamping,p->gravityFactor,
                    p->continuousCollision,p->allowSleeping,int(s->shapeType),s->position.x,s->position.y,s->position.z,
                    s->linearVelocity.x,s->linearVelocity.y,s->linearVelocity.z,s->angularVelocity.x,s->angularVelocity.y,s->angularVelocity.z,s->sleeping);
            }
            std::fflush(states);if(tick>=120){std::fclose(states);states=nullptr;}
        }
        static void Camera(RigidBodySimulationApp& app)
        {
            if(ready)app.FocusMaterialCamera(app.m_EditorCamera_,{3,8,3},Cases()[index].angle?.10f:1.30f,-.2f,55.f);
        }
        static inline PFNGLSHADERSOURCEPROC originalShaderSource{};
        static inline PFNGLUNIFORM1IPROC originalUniform{};
        static void APIENTRY Uniform(GLint location,GLint value)
        {
            if(ready && local==59 && value==1)
            {
                GLint program{};glGetIntegerv(GL_CURRENT_PROGRAM,&program);
                if(location==glGetUniformLocation(program,"useOccupiedTiles"))value=0;
            }
            originalUniform(location,value);
        }
        static void APIENTRY ShaderSource(GLuint shader,GLsizei count,const GLchar* const* strings,const GLint* lengths)
        {
            std::string source;
            for(int i=0;i<count;++i)source.append(strings[i],lengths && lengths[i]>=0?std::size_t(lengths[i]):std::strlen(strings[i]));
            const auto at=source.find("if(useOccupiedTiles)");
            if(at!=std::string::npos)source.replace(at,std::strlen("if(useOccupiedTiles)"),"if(false)");
            const auto* text=source.c_str();originalShaderSource(shader,1,&text,nullptr);
        }
        struct GpuSlot {GLuint q[2]{};bool pending{};unsigned caseIndex{},localFrame{};};
        static inline std::array<GpuSlot,16> gpu;
        static inline GpuSlot* currentGpu{};
        static inline FILE* gpuOutput{};
        static void BeginGpu()
        {
            for(auto& s:gpu)if(s.pending)
            {
                GLint a{},b{};glGetQueryObjectiv(s.q[0],GL_QUERY_RESULT_AVAILABLE,&a);glGetQueryObjectiv(s.q[1],GL_QUERY_RESULT_AVAILABLE,&b);
                if(a && b){GLuint64 begin{},end{};glGetQueryObjectui64v(s.q[0],GL_QUERY_RESULT,&begin);glGetQueryObjectui64v(s.q[1],GL_QUERY_RESULT,&end);
                    std::fprintf(gpuOutput,"%s,%u,%llu\n",Cases()[s.caseIndex].name,s.localFrame,end-begin);s.pending=false;}
            }
            currentGpu=nullptr;
            for(auto& s:gpu)if(!s.pending){currentGpu=&s;break;}
            if(currentGpu){auto& s=*currentGpu;if(!s.q[0])glGenQueries(2,s.q);s.caseIndex=index;s.localFrame=local;glQueryCounter(s.q[0],GL_TIMESTAMP);}
        }
        static bool Begin(RigidBodySimulationApp& app)
        {
            using namespace ::GEngine;
            if(Stiffness()){PhysicsSnapshot(app);return true;}
            if(std::getenv("OWNER_REVIEW_NO_VSYNC"))app.SetManualFrameRateLimit(0);
            if(!output)
            {
                if((SelectionSuite() || TransformSuite()) && !originalBufferData){
                    originalBufferData=glad_glNamedBufferData;glad_glNamedBufferData=BufferData;
                    originalBufferSubData=glad_glNamedBufferSubData;glad_glNamedBufferSubData=BufferSubData;
                    originalBindFramebuffer=glad_glBindFramebuffer;glad_glBindFramebuffer=BindFramebuffer;
                }
                FILE* info{};fopen_s(&info,"gl-identity.txt","w");
                if(info){std::fprintf(info,"vendor=%s\nrenderer=%s\nversion=%s\nswap_interval=%d\nmode=%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION),app.GetWindow()->GetSwapInterval(),RunningMode()?"running-fixed-tick":"paused-tick-0");std::fclose(info);}
                // Separate image-equivalence runs only; never enabled in timing runs.
                if(std::getenv("OWNER_REVIEW_REFERENCE_COMPOSITE") && !originalShaderSource)
                {originalShaderSource=glad_glShaderSource;glad_glShaderSource=ShaderSource;}
                if(std::getenv("OWNER_REVIEW_PAIR_CAPTURE") && !originalUniform)
                {originalUniform=glad_glUniform1i;glad_glUniform1i=Uniform;}
                fopen_s(&output,"owner-review-frames.csv","w");
                if(!output){app.ShutDown();return false;}
                fopen_s(&gpuOutput,"owner-review-gpu-frame.csv","w");
                if(!gpuOutput){app.ShutDown();return false;}
                std::fprintf(gpuOutput,"case,local,gpu_frame_ns\n");
                std::fprintf(output,"frame,case,local,warm,wall_ns,cpu_ns,physics_ns,ui_ns,sync_ns,draft_ns,pick_read_ns,scene_ns,allocs,bytes,ui_allocs,sync_allocs,draft_allocs,draft_calls,draws,pick_draws,outline_draws,outline_passes,scratch_allocs,region_x,region_y,region_w,region_h,viewport_w,viewport_h,tick,selected,active,shadow_draws,color_draws,visible,culled,instances,raw_delta_ns,imgui_fps,imgui_samples,swap_interval,instanced_draws,rectangle_draws,rectangle_allocations,buffer_updates,upload_bytes,framebuffer_binds,render_passes,selected_entities\n");
            }
            if(frame++<12)return true; // initial docking and resource startup
            if(local==120)
            {
                local=0;++index;ready=false;
                if(RunningMode() && index<Cases().size())
                {app.RequestScene(app.m_ActiveSelection);return false;}
            }
            if(index==Cases().size())
            {
                std::fclose(output);output=nullptr;
                for(auto& s:gpu)if(s.q[0])glDeleteQueries(2,s.q);
                std::fclose(gpuOutput);gpuOutput=nullptr;app.ShutDown();return false;
            }
            const auto& c=Cases()[index];
            if(!ready)
            {
                if(TransformSuite()){app.CancelGroupTransform();app.SetEditorMode(RigidBodySimulationApp::EditorMode::Edit);app.m_TransformTool=RigidBodySimulationApp::TransformTool::Select;}
                app.SetSimulationState(RunningMode()?SimulationState::Running:SimulationState::Paused);
                app.ClearColliderSelection();
                app.FocusMaterialCamera(app.m_EditorCamera_,{3,8,3},c.angle?.10f:1.30f,-.2f,55.f);
                if(SelectionSuite()) {
                    app.m_EditorHidden.clear();rectanglePending=false;
                    if(c.cohortOnly)for(auto raw:app.m_ActiveScene->GetAllEntitiesWith<Component::TagComponent>()){
                        _Entity e{raw,app.m_ActiveScene.get()};auto id=*app.m_ActiveScene->RenderData().Identify(e);
                        if(!app.IsSphereLatticeEntity(id))app.m_EditorHidden.push_back(id);
                    }
                    std::sort(app.m_EditorHidden.begin(),app.m_EditorHidden.end());
                    app.SetCameraView(RigidBodySimulationApp::CameraView::Editor);
                    if(c.ortho)for(auto raw:app.m_ActiveScene->GetAllEntitiesWith<Component::TagComponent>()){
                        _Entity e{raw,app.m_ActiveScene.get()};if(e.GetName()=="Scene Camera B")app.SetCameraView(RigidBodySimulationApp::CameraView::Preview,*app.m_ActiveScene->RenderData().Identify(e));
                    }
                }
                int selected=0;
                for(auto id:app.m_ActiveScene->GetAllEntitiesWith<::GEngine::Component::TagComponent>())
                {
                    _Entity e{id,app.m_ActiveScene.get()};const auto& name=e.GetName();
                    if(((SelectionSuite() || TransformSuite()) && c.selection>=100 && c.selection!=1000 &&
                        app.IsSphereLatticeEntity(*app.m_ActiveScene->RenderData().Identify(e)) && selected<(c.selection==1001?180:c.selection-100)) ||
                       (c.selection==2 && name=="Sandbox Floor") ||
                       ((c.selection==1 || c.selection==3) && name.starts_with("wood_sphere") && selected<(c.selection==3?8:1)))
                    {app.SelectColliderEntity(e,selected++>0);}
                }
                if(SelectionSuite() && c.selection==1001)app.SelectColliderEntity(app.m_AxisEntity,true);
                if(TransformSuite() && index>=2) {
                    using Tool=RigidBodySimulationApp::TransformTool;
                    Editor::TransformPivot=Editor::TransformOrientation=0;
                    if(!app.ActivateTransformTool(index==4?Tool::Rotate:index==5?Tool::Scale:Tool::Translate) || !app.BeginGroupTransform()){
                        std::fprintf(stderr,"GROUP_PROBE_ADMISSION_FAILED\n");app.ShutDown();return false;}
                }
                if(c.selection && c.selection!=1000 && !selected){std::fprintf(stderr,"OWNER_REVIEW_TARGET_MISSING %s\n",c.name);app.ShutDown();return false;}
                app.m_ShowSelectedColliderOutlines=c.colliders;
                app.m_IsShowDebugBoundingBox=c.debug;
                app.m_ColliderLayers.colliders=true;
                app.m_DiagnosticsTabRequest=c.profiler?1:0;
                if(c.noShadows)
                    for(auto id:app.m_ActiveScene->GetAllEntitiesWith<::GEngine::Component::RenderLightComponent>())
                    {
                        _Entity e{id,app.m_ActiveScene.get()};
                        auto light=e.GetComponent<::GEngine::Component::RenderLightComponent>();light.castShadows=false;
                        e.AddOrReplaceComponent<::GEngine::Component::RenderLightComponent>(light);
                    }
                ready=true;
            }
            if(c.pick && local==30){app.SetViewportActive(true);app.m_ViewPickRequested=true;}
            bufferUpdates=uploadBytes=framebufferBinds=0;
            ReviewMetrics::ns={};ReviewMetrics::calls={};ReviewMetrics::allocs={};
            ReviewMetrics::allocations=ReviewMetrics::bytes=0;ReviewMetrics::counting=true;
            started=ReviewMetrics::Now();
            if(TransformSuite() && index>=3) {
                // Real candidate path, staged before ordinary safe Update processing.
                auto& s=app.m_GroupTransform;const auto origin=s.origin;const auto p=Vec3f(origin[3]);
                const float amount=.01f*std::sin(float(local)*.1f);
                const float factor=1.f+amount*.25f;
                Mat4 delta=index==3?glm::translate(Mat4(1),Vec3f(0,1.f+amount,0)):
                    glm::translate(Mat4(1),p)*(index==4?glm::mat4_cast(glm::angleAxis(amount,Vec3f(0,1,0))):glm::scale(Mat4(1),Vec3f(factor)))*glm::translate(Mat4(1),-p);
                if(!app.StageGroupTransform(delta*origin,index==5?factor:1.f)){
                    std::fprintf(stderr,"GROUP_PROBE_STAGE_FAILED\n");app.ShutDown();return false;}
            }
            if(rectanglePending){
                auto result=app.m_FrameSubmission->PollRectangleSelection();
                if(!result){std::fprintf(stderr,"OWNER_REVIEW_RECTANGLE_ERROR\n");app.ShutDown();return false;}
                if(*result){
                    if((*result)->entities.size()!=180){std::fprintf(stderr,"OWNER_REVIEW_RECTANGLE_COUNT %zu\n",(*result)->entities.size());app.ShutDown();return false;}
                    app.m_EditorSelection.SelectSet(app.m_ActiveScene->RenderData(),(*result)->entities,Rbs::Editor::EntitySelection::SetOperation::Replace);
                    app.SynchronizeEditorSelection();rectanglePending=false;
                    std::fprintf(stdout,"OWNER_REVIEW_RECTANGLE_RESULT local=%u selected=%zu\n",local,app.m_EditorSelection.count);
                } else if(local>=24){std::fprintf(stderr,"OWNER_REVIEW_RECTANGLE_TIMEOUT\n");app.ShutDown();return false;}
            }
            BeginGpu();
            return true;
        }
        static void Targets(RigidBodySimulationApp& app,::GEngine::FrameSubmissionDesc& target)
        {
            if(!ready)return;
            if(TransformSuite() && app.m_GroupTransform.invalid){std::fprintf(stderr,"GROUP_PROBE_INVALID %s\n",app.m_PropertyDiagnostic.c_str());app.ShutDown();return;}
            target.measureSubmission=true;
            if(SelectionSuite() && Cases()[index].selection==1000 && local==0){
                const auto size=app.m_CameraSnapshot->Description().viewport.pixelSize;
                target.pickingEnabled=true;target.rectangleSelection=::GEngine::RectangleSelectionRequest{999,{0,0,size.x,size.y},true};rectanglePending=true;
            }
            if(Cases()[index].hideHighlight){target.selectedEntity={};target.selectedEntities={};}
            if(Cases()[index].pick && local==30)
            {
                // Explicit one-shot ordinary ID pass/readback. No synthetic mouse
                // state; actual event routing is tested separately on the desktop.
                const auto size=app.m_CameraSnapshot->Description().viewport.pixelSize;
                app.m_FramePickPixel=decltype(app.m_FramePickPixel)::value_type{size.x/2,size.y/4};
                target.pickingEnabled=true;
            }
            if(const auto* scale=std::getenv("OWNER_REVIEW_PIXELS_PER_DIP"))target.selectionPixelsPerDip=glm::vec2(float(std::atof(scale)));
        }
        static void End(RigidBodySimulationApp& app,const ::GEngine::ScheduledFrameStats& stats)
        {
            if(!ready)return;
            if(currentGpu){glQueryCounter(currentGpu->q[1],GL_TIMESTAMP);currentGpu->pending=true;}
            const auto end=ReviewMetrics::Now();ReviewMetrics::counting=false;
            const auto& s=stats.submission;const auto& v=app.m_CameraSnapshot->Description().viewport;
            const auto tick=app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps();
            if((!RunningMode() && tick) || (RunningMode() && tick!=local+1))
            {std::fprintf(stderr,"OWNER_REVIEW_STATE_MISMATCH case=%s local=%u tick=%llu\n",Cases()[index].name,local,tick);app.ShutDown();return;}
            const auto& n=ReviewMetrics::ns;const auto& a=ReviewMetrics::allocs;
            const auto timing=app.GetWindow()->Timings().Current();
            const auto timerFrame=timing.empty()?0:timing.front().frame;
            const auto physicsNs=app.m_ActiveScene->GetPhysicsTiming().stepsLastUpdate ? app.m_ActiveScene->GetPhysicsSystem()->GetLastUpdateNanoseconds():0;
            std::fprintf(output,"%llu,%s,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%zu,%zu,%zu,%zu,%zu,%u,%u,%u,%u,%u,%u,%llu,%zu,%u,%zu,%zu,%zu,%zu,%zu,%.0f,%.6f,%d,%d,%zu,%zu,%zu,%llu,%llu,%llu,%zu,%zu\n",
                timerFrame,Cases()[index].name,local,local<24,end-previous,end-started,physicsNs,n[0],n[1],n[2],n[3],n[4],ReviewMetrics::allocations,ReviewMetrics::bytes,
                a[0],a[1],a[2],ReviewMetrics::calls[2],s.submittedDrawCalls,s.pickDraws,s.selectionDrawCalls,s.selectionPasses,s.selectionScratchAllocations,
                s.selectionRegion.x,s.selectionRegion.y,s.selectionRegion.width,s.selectionRegion.height,v.pixelSize.x,v.pixelSize.y,tick,app.m_ColliderOutlines.count,app.m_ViewportActive,
                s.shadowDraws,s.colorDraws,s.visibility.visible,s.visibility.culled,s.submittedInstances,
                app.GetFrameTime().rawDelta.count()*1e9,double(ImGui::GetIO().Framerate),ImGui::GetCurrentContext()->FramerateSecPerFrameCount,app.GetWindow()->GetSwapInterval(),
                s.instancedDrawCalls,s.rectangleSelectionDraws,s.rectangleQueryAllocations,bufferUpdates,uploadBytes,framebufferBinds,s.trace.count,app.m_EditorSelection.count);
            previous=end;++local;
            if((local==61 || (local==60 && originalUniform)) && std::getenv("OWNER_REVIEW_CAPTURE"))
            {
                // Deliberate synchronous validation capture, OUTSIDE performance runs.
                const auto& d=app.m_RenderTarget->Description().Storage;
                std::vector<std::byte> pixels(std::size_t(d.Width)*d.Height*4);
                if(!app.m_RenderTarget->ReadColor(pixels)){std::fprintf(stderr,"OWNER_REVIEW_CAPTURE_FAIL\n");app.ShutDown();return;}
                const auto name=std::string(local==60?"reference-":"")+Cases()[index].name+"-"+std::to_string(d.Width)+"x"+std::to_string(d.Height)+".rgba";
                FILE* image{};fopen_s(&image,name.c_str(),"wb");
                if(image){std::fwrite(pixels.data(),1,pixels.size(),image);std::fclose(image);}
            }
        }
    };
}
#endif
