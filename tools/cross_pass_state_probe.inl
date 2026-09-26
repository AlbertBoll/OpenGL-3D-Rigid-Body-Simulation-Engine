// Included in the existing production-library probe after its resource helpers.
namespace CrossPassState
{
    constexpr GLenum disabled[]{GL_SCISSOR_TEST,GL_STENCIL_TEST,GL_RASTERIZER_DISCARD,
        GL_POLYGON_OFFSET_FILL,GL_POLYGON_OFFSET_LINE,GL_POLYGON_OFFSET_POINT,
        GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK,GL_DEPTH_CLAMP,
        GL_FRAMEBUFFER_SRGB,GL_COLOR_LOGIC_OP,GL_PRIMITIVE_RESTART,GL_PRIMITIVE_RESTART_FIXED_INDEX,
        GL_POLYGON_SMOOTH,GL_PROGRAM_POINT_SIZE,GL_SAMPLE_SHADING,GL_SAMPLE_ALPHA_TO_ONE};
    void Contaminate(bool dirty)
    {
        for(auto cap:disabled) { if(dirty) glEnable(cap);else glDisable(cap); }
        GLint clips{};glGetIntegerv(GL_MAX_CLIP_DISTANCES,&clips);
        for(int i=0;i<clips;++i) {if(dirty) glEnable(GL_CLIP_DISTANCE0+i);else glDisable(GL_CLIP_DISTANCE0+i);}
        glClipControl(dirty?GL_UPPER_LEFT:GL_LOWER_LEFT,dirty?GL_ZERO_TO_ONE:GL_NEGATIVE_ONE_TO_ONE);
        glLogicOp(GL_CLEAR);glPrimitiveRestartIndex(0);glSampleMaski(0,0);glSampleCoverage(0,GL_FALSE);
        glScissor(0,0,0,0);glStencilFunc(GL_NEVER,0,~0u);
        glDepthMask(!dirty);glColorMask(!dirty,!dirty,!dirty,!dirty);
        glDepthFunc(dirty?GL_NEVER:GL_LESS);glDepthRange(dirty?1:0,dirty?0:1);glClearDepth(dirty?0:1);
        glPolygonMode(GL_FRONT_AND_BACK,dirty?GL_LINE:GL_FILL);glFrontFace(dirty?GL_CW:GL_CCW);
        glCullFace(dirty?GL_FRONT:GL_BACK);glLineWidth(dirty?3:1);
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);glBlendFunc(GL_ZERO,GL_ZERO);
        glViewport(3,5,1,1);glActiveTexture(GL_TEXTURE3);
    }
    void FixedState(bool ui=false)
    {
        for(auto cap:disabled) if(cap!=GL_SCISSOR_TEST)
            Check(!glIsEnabled(cap),"cross-pass disables inherited raster feature");
        GLint clips{},value{};glGetIntegerv(GL_MAX_CLIP_DISTANCES,&clips);
        for(int i=0;i<clips;++i) Check(!glIsEnabled(GL_CLIP_DISTANCE0+i),"cross-pass disables every user clip plane");
        glGetIntegerv(GL_CLIP_ORIGIN,&value);Check(value==GL_LOWER_LEFT,"cross-pass lower-left clip origin");
        glGetIntegerv(GL_CLIP_DEPTH_MODE,&value);Check(value==GL_NEGATIVE_ONE_TO_ONE,"cross-pass clip depth convention");
        GLdouble range[2]{};glGetDoublev(GL_DEPTH_RANGE,range);Check(range[0]==0&&range[1]==1,"cross-pass depth range");
        GLboolean mask[4]{};glGetBooleanv(GL_COLOR_WRITEMASK,mask);
        Check(mask[0]&&mask[1]&&mask[2]&&mask[3],"cross-pass full color write mask");
        if(ui) Check(glIsEnabled(GL_BLEND)&&!glIsEnabled(GL_DEPTH_TEST)&&!glIsEnabled(GL_CULL_FACE),"UI draw blend/depth/cull contract");
    }
    unsigned errors{}, driverRecompiles{};
    void APIENTRY Debug(GLenum,GLenum type,GLuint id,GLenum severity,GLsizei,const GLchar* message,const void*)
    {
        // NVIDIA's program specialization notice is performance information,
        // also present in the approved submission fixtures. Keep it observable.
        if(type==GL_DEBUG_TYPE_PERFORMANCE && id==131218 && severity==GL_DEBUG_SEVERITY_MEDIUM) {
            ++driverRecompiles;return;
        }
        if(type==GL_DEBUG_TYPE_ERROR || type==GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR
            || severity==GL_DEBUG_SEVERITY_HIGH || severity==GL_DEBUG_SEVERITY_MEDIUM) {
            ++errors;std::println("[FAIL] cross-pass GL diagnostic: {}",message);
        }
    }
    void Run(EngineContext& root)
    {
        std::println("Cross-pass renderer={} vendor={} version={}; 64x64 linear RGBA8; exact same-driver comparisons; camera eye=(0,2,8), FOV=45, near=.1 far=20",
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),reinterpret_cast<const char*>(glGetString(GL_VENDOR)),reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);glDebugMessageCallback(Debug,nullptr);
        auto resources=Take(SceneRenderResources::Create(root),"cross-pass resources");
        const auto box=Take(resources->PublishShape("Box"),"cross-pass box");
        const auto axis=Take(resources->PublishShape("AxisHelper"),"cross-pass axis");
        const auto skyMesh=Take(resources->PublishShape("SkyBox"),"cross-pass sky");
        const char* names[]{"albedoMap","normalMap","metallicMap","roughnessMap","aoMap"};
        const char* paths[]{"PBR/rustediron/rustediron2_basecolor","PBR/rustediron/rustediron2_normal",
            "PBR/rustediron/rustediron2_metallic","PBR/rustediron/rustediron2_roughness","PBR/subtle_black_granite/subtle-black-granite_ao"};
        std::vector<MaterialTextureAssignment> textures;
        for(unsigned i=0;i<5;++i) {
            auto* image=Take(Manager::AssetsManager::GetTexture(paths[i],names[i]),"cross-pass image");
            auto sample=Take(Manager::AssetsManager::SampleTexture(image->View()),"cross-pass sampler");
            textures.push_back({names[i],{sample.TextureIdentity(),sample.SamplerIdentity()}});
        }
        const MaterialParameterDecl parameters[]{{"metalness",MaterialParameterType::Float3,std::array<float,3>{.8f,.8f,.8f}},
            {"u_tiling",MaterialParameterType::Float2,std::array<float,2>{1,1}}};
        const auto opaque=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures}),"cross-pass opaque");
        const auto masked=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Masked,.5f,.75f}),"cross-pass mask");
        const auto transparent=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Transparent,.5f,.5f}),"cross-pass alpha");
        const MaterialParameterDecl helperParams[]{{"u_baseColor",MaterialParameterType::Float4,std::array<float,4>{1,1,1,1}},
            {"u_useVertexColor",MaterialParameterType::Boolean,true}};
        const auto helper=Take(resources->PublishMaterial({SceneMaterialKind::Helper,helperParams,{},true,3}),"cross-pass helper");
        TextureDesc skyDesc;skyDesc.kind=TextureKind::Cube;skyDesc.colorSpace=TextureColorSpace::Linear;
        skyDesc.mips=TextureMipIntent::None;skyDesc.orientation=ImageOrientation::TopLeft;
        auto* skyImage=Take(Manager::AssetsManager::GetTexture("SkyBox/Day/","u_skyBoxDay",".png",skyDesc),"cross-pass sky image");
        auto skySample=Take(Manager::AssetsManager::SampleTexture(skyImage->View()),"cross-pass sky sampler");
        const MaterialTextureAssignment skyBinding{"u_skyBoxDay",{skySample.TextureIdentity(),skySample.SamplerIdentity()}};
        const auto sky=Take(resources->PublishMaterial({SceneMaterialKind::Sky,{},{&skyBinding,1},true}),"cross-pass sky material");
        RenderTargetDesc td;td.Storage.Width=td.Storage.Height=64;td.Storage.ColorCount=1;
        td.Storage.Colors[0]=FramebufferFormat::RGBA8;td.Storage.Depth=FramebufferFormat::Depth24Stencil8;
        auto target=Take(RenderTarget::Create(td),"cross-pass target");
        auto picking=Take(MousePickFrameBuffer::Create(64,64),"cross-pass pick");
        auto point=Take(PointShadowFrameBuffer::Create(256,256),"cross-pass point");
        auto cascade=Take(CascadeShadowFrameBuffer::Create(256,256,5),"cross-pass cascade");
        const float splits[]{.5f,1.f,2.f,5.f,20.f};
        FrameSubmissionDesc desc{target,picking,point,cascade,resources->Pipelines(),splits,glm::radians(45.f),1,.1f,20,.1f,100};
        auto submission=Take(FrameSubmission::Create(),"cross-pass submission");EntityPickTable picks;
        _Scene scene;const auto camera=Camera(scene);
        auto sun=SceneOperationChecked([&] { return scene.CreateEntity("cross-pass sun"); });RenderLightComponent sunlight;sunlight.castShadows=true;
        sun.AddComponent<RenderLightComponent>(sunlight);
        sun.Transform().QuatRotation=glm::rotation(glm::vec3(0,0,-1),-glm::normalize(glm::vec3(20,50,20)));
        auto bulb=SceneOperationChecked([&] { return scene.CreateEntity("cross-pass bulb"); });RenderLightComponent pointlight;pointlight.kind=RenderLightKind::Point;
        pointlight.castShadows=true;pointlight.range=20;bulb.AddComponent<RenderLightComponent>(pointlight);bulb.Transform().Translation={0,3,2};
        auto make=[&](const char* name,MeshHandle mesh,MaterialInstanceHandle material,glm::vec3 position) {
            auto entity=SceneOperationChecked([&] { return scene.CreateEntity(name); });entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
            entity.AddComponent<VisibilityComponent>();entity.Transform().Translation=position;return entity;
        };
        std::array entities{make("sky",skyMesh,sky,{0,0,0}),make("alpha",box,transparent,{.5f,0,0}),
            make("debug",axis,helper,{0,1,0}),make("mask",box,masked,{-1.5f,0,0}),make("opaque",box,opaque,{0,-1,0})};
        // Seed both optional depth images, then visit every presence mask in a
        // permutation so a present/absent boundary sees different predecessors.
        auto run=[&] {
            auto access=resources->Publication().BeginFrame();auto frame=Take(Extract(scene,*resources,access,camera),"cross-pass extract");
            return Take(submission.Submit(frame,desc,picks),"cross-pass submit");
        };
        Contaminate(false);run();
        auto pickImage=[&] {
            std::vector<int> pixels(64*64);
            glGetTextureImage(FramebufferDetail::Backend::Color(picking.Buffer(),0),0,GL_RED_INTEGER,GL_INT,
                static_cast<GLsizei>(pixels.size()*sizeof(int)),pixels.data());return pixels;
        };
        std::set<std::vector<std::byte>> distinct;
        for(unsigned sequence=0;sequence<256;++sequence) {
            const unsigned mask=(sequence*73+19)&255;
            sun.GetComponent<RenderLightComponent>().castShadows=bool(mask&1);
            bulb.GetComponent<RenderLightComponent>().castShadows=bool(mask&2);desc.pickingEnabled=bool(mask&4);
            for(unsigned i=0;i<entities.size();++i) entities[i].GetComponent<VisibilityComponent>().enabled=bool(mask&(8u<<i));
            Contaminate(false);submission.InvalidatePassContents();const auto clean=run();
            const auto color=Pixels(target);const auto depth=ShadowDepth(point,cascade);
            const auto pick=desc.pickingEnabled?pickImage():std::vector<int>{};
            if(sequence<16) distinct.insert(color);
            for(unsigned repeat=0;repeat<2;++repeat) {
                Contaminate(true);if(repeat==0) submission.InvalidatePassContents();const auto contaminated=run();
                FixedState();
                Check(Pixels(target)==color && ShadowDepth(point,cascade)==depth,"optional pass order preserves exact color and layered depth under contamination");
                if(desc.pickingEnabled) Check(pickImage()==pick,"optional pass order preserves exact picking under contamination");
                for(unsigned i=0;i<3;++i)
                    Check(contaminated.decisions[i].executed==(repeat==0 && bool(mask&(1u<<i))),"optional pass executes or reuses precisely");
                Check(contaminated.colorDraws==clean.colorDraws && contaminated.helperDraws==clean.helperDraws
                    && contaminated.skyDraws==clean.skyDraws,"optional color/sky/debug presence counts");
                Check(!RenderBackend::GLStateCache::Current(),"submission cache never survives into external clients");
            }
            Check(glGetError()==GL_NO_ERROR,"cross-pass matrix driver clean");
        }
        Check(distinct.size()>4,"presence matrix produces visibly distinct reference outputs");
        std::println("[PASS] cross-pass-state 256-presence-masks/768-submissions/exact-color-picking-depth/cached-and-dirty");
        Contaminate(false);
        StateCacheTests();
        // Extend cache invalidation coverage to the newly declared fixed state.
        {
            RenderBackend::GLStateCache cache;
            cache.Toggle(GL_COLOR_LOGIC_OP,false);cache.Toggle(GL_PRIMITIVE_RESTART,false);
            cache.ClipControl(GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE);
            Contaminate(true);cache.Invalidate();
            cache.Toggle(GL_COLOR_LOGIC_OP,false);cache.Toggle(GL_PRIMITIVE_RESTART,false);
            cache.ClipControl(GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE);
            GLint origin{};glGetIntegerv(GL_CLIP_ORIGIN,&origin);
            Check(!glIsEnabled(GL_COLOR_LOGIC_OP)&&!glIsEnabled(GL_PRIMITIVE_RESTART)&&origin==GL_LOWER_LEFT,"new cache entries resync after explicit invalidation");
        }
        Contaminate(false);
        struct External { RenderTarget* target; unsigned legacy{},ui{},draws{},platform{}; bool failLegacy{},failUI{}; SDL_GLContext main; } external{&target,0,0,0,0,false,false,SDL_GL_GetCurrentContext()};
        RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager(),&target};
        context.legacyScene={&external,[](void* user)->ScheduleResult {
            auto& e=*static_cast<External*>(user);++e.legacy;FixedState();
            GLboolean write{};glGetBooleanv(GL_DEPTH_WRITEMASK,&write);
            Check(write&&glIsEnabled(GL_DEPTH_TEST)&&glIsEnabled(GL_BLEND),"legacy establishes clear masks and conventional blend/depth");
            e.target->Bind();glClearColor(.25f,.5f,.75f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            Contaminate(true);
            if(e.failLegacy) return std::unexpected(ScheduleError{FrameStage::Pass,ScheduleCode::InvalidInput});
            return {};
        }};
        const auto drawCallback=[](const ImDrawList*,const ImDrawCmd* command) {
            auto& e=*static_cast<External*>(command->UserCallbackData);++e.draws;
            const bool platform=SDL_GL_GetCurrentContext()!=e.main;e.platform+=platform;
            Check(GLContextThread::IsCurrentOwner(),"UI viewport is on the registered owning context thread");
            glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);glDebugMessageCallback(Debug,nullptr);
            FixedState(true);
            // The rectangle covers the drawable center in both main and detached views.
            GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);std::array<unsigned char,4> pixel{};
            glReadPixels(viewport[2]/2,viewport[3]/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
            Check(pixel[0]==0 && pixel[1]==255 && pixel[2]==0,"real UI backend draws full-write green into each context");
            Contaminate(true); // Backend restore omits these states; next entry must reset them.
            Check(glGetError()==GL_NO_ERROR,"UI context draw and contamination driver clean");
        };
        struct Author {External* external; decltype(drawCallback)* callback;} author{&external,&drawCallback};
        context.editorUI={&author,[](void* user)->ScheduleResult {
            auto& a=*static_cast<Author*>(user);auto& e=*a.external;++e.ui;
            FixedState(true);Check(!RenderBackend::GLStateCache::Current(),"no live cache across UI callbacks");
            ImGui::GetIO().ConfigViewportsNoAutoMerge=true;
            auto* main=ImGui::GetMainViewport();auto* background=ImGui::GetForegroundDrawList(main);
            background->AddRectFilled(main->Pos,ImVec2(main->Pos.x+main->Size.x,main->Pos.y+main->Size.y),IM_COL32(0,255,0,255));
            background->AddCallback(*a.callback,&e);
            ImGui::SetNextWindowPos(ImVec2(main->Pos.x+150,main->Pos.y+150),ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(180,120),ImGuiCond_Always);
            ImGui::Begin("Phase 63 detached context",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDecoration);
            auto* viewport=ImGui::GetWindowViewport();
            if(viewport!=main) {
                auto* draw=ImGui::GetForegroundDrawList(viewport);
                draw->AddRectFilled(viewport->Pos,ImVec2(viewport->Pos.x+viewport->Size.x,viewport->Pos.y+viewport->Size.y),IM_COL32(0,255,0,255));
                draw->AddCallback(*a.callback,&e);
            }
            ImGui::End();
            Contaminate(true); // Authoring itself must not poison the upcoming backend draw.
            if(e.failUI) return std::unexpected(ScheduleError{FrameStage::Pass,ScheduleCode::InvalidInput});
            return {};
        }};
        const auto legacy=context.legacyScene,ui=context.editorUI;
        for(unsigned sequence=0;sequence<16;++sequence) {
            const unsigned mask=(sequence*5+7)&15;
            context.visible=bool(mask&1);context.legacyScene=(mask&2)?legacy:FrameCallback{};
            context.editorUI=(mask&4)?ui:FrameCallback{};
            Check(target.SetSamples((mask&8)?4:1),"cross-pass resolve sample change");
            Contaminate(true);Take(FrameScheduler::Render(context),"external combination");
            if(context.visible&&context.legacyScene.invoke) {
                const auto pixels=Pixels(target);
                for(unsigned i=0;i<pixels.size();i+=4)
                    Check(std::abs(int(pixels[i])-64)<=1 && std::abs(int(pixels[i+1])-128)<=1 && std::abs(int(pixels[i+2])-191)<=1,"legacy dirty exit does not clip or corrupt single/multisample resolve");
            }
            Check(root.MainWindow()->IsCurrent()&&!RenderBackend::GLStateCache::Current(),"external sequence restores main context and retires all caches");
            Check(glGetError()==GL_NO_ERROR,"external combination driver clean");
        }
        context.visible=true;context.legacyScene=legacy;context.editorUI=ui;
        for(unsigned i=0;i<4;++i) {Contaminate(true);Take(FrameScheduler::Render(context),"detached UI repeat");}
        Check(external.platform>=2 && external.draws>external.platform && ImGui::GetPlatformIO().Viewports.Size>=2,"actual detached ImGui viewport draws repeatedly");
        external.failLegacy=true;auto failure=FrameScheduler::Render(context);
        Check(!failure&&root.MainWindow()->IsCurrent()&&!RenderBackend::GLStateCache::Current(),"legacy failure ends boundary/cache");
        external.failLegacy=false;external.failUI=true;failure=FrameScheduler::Render(context);
        Check(!failure&&root.MainWindow()->IsCurrent()&&!RenderBackend::GLStateCache::Current(),"UI author failure balances draw and restores context");
        external.failUI=false;Take(FrameScheduler::Render(context),"external retry after failures");
        std::println("[PASS] cross-pass-state external-16-combinations/resolve/legacy/ImGui-platform-windows/failure-retry ui={} platform-draws={}",external.ui,external.platform);
        // A subsequent real submission must start unknown after the foreign contexts.
        Contaminate(true);run();FixedState();
        Check(errors==0&&glGetError()==GL_NO_ERROR,"synchronous GL debug and final driver state clean");
        glDebugMessageCallback(nullptr,nullptr);
        std::println("[PASS] cross-pass-state complete checks={} GL-errors={} NVIDIA-specialization-notices={}",checks,errors,driverRecompiles);
    }
}
