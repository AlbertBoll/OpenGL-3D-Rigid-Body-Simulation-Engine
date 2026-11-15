        // Same scene and submission owner, independent authoritative Camera views.
        struct ViewCheck { _Entity entity; RenderTarget color; CascadeShadowFrameBuffer depth; };
        auto view0=SceneOperationChecked([&]{return scene.CreateEntity("Perspective Camera");});
        auto view1=SceneOperationChecked([&]{return scene.CreateEntity("Orthographic Camera");});
        view0.AddComponent<RenderCameraComponent>();view1.AddComponent<RenderCameraComponent>();
        view0.GetComponent<RenderCameraComponent>().farPlane=20;
        view1.GetComponent<RenderCameraComponent>().farPlane=20;
        view1.GetComponent<RenderCameraComponent>().projection=CameraProjection::Orthographic;
        view1.GetComponent<RenderCameraComponent>().orthographicHeight=6;
        view0.Transform().Translation=view1.Transform().Translation={0,0,8};
        std::array<ViewCheck,2> views{{{view0,Take(RenderTarget::Create(td),"view color 0"),Take(CascadeShadowFrameBuffer::Create(256,256,5),"view cascade 0")},
                                      {view1,Take(RenderTarget::Create(td),"view color 1"),Take(CascadeShadowFrameBuffer::Create(256,256,5),"view cascade 1")}}};
        auto submitView=[&](unsigned i) {
            auto& view=views[i];auto access=resources->Publication().BeginFrame();
            const auto id=Take(scene.RenderData().Identify(view.entity),"view identity");
            CameraViewport viewport;viewport.pixelSize={unsigned(view.color.GetWidth()),unsigned(view.color.GetHeight())};viewport.logicalSize=glm::dvec2(viewport.pixelSize);
            auto snap=Take(scene.SnapshotSceneCamera(id,resources->ForFrame(access),viewport),"authoritative Camera snapshot");
            auto actual=FrameCamera::FromSnapshot(id,snap);auto frame=Take(Extract(scene,*resources,access,actual),"camera view extraction");
            FrameSubmissionDesc targetDesc{view.color,picking,pointDepth,view.depth,resources->Pipelines(),splits,
                float(snap.Description().verticalFovRadians),float(view.color.GetWidth())/view.color.GetHeight(),.1f,20,.1f,100,false};
            return Take(submitter.Submit(frame,targetDesc,picks),"camera view submit");
        };
        submit();auto visiblePixels=read();
        std::array<EntityRenderId,2> hidden{Take(scene.RenderData().Identify(casters[0]),"hidden body identity"),Take(scene.RenderData().Identify(lamp),"hidden Light identity")};
        std::sort(hidden.begin(),hidden.end());desc.editorHiddenEntities=hidden;desc.pickingEnabled=true;
        const auto hiddenStats=submit();
        Check(hiddenStats.colorDraws==4 && hiddenStats.pickDraws==4 && read()!=visiblePixels,"editor hiding filters main color and picking");
        Check(hiddenStats.decisions[3].requested && !hiddenStats.decisions[3].executed && !hiddenStats.decisions[0].executed && !hiddenStats.decisions[1].executed,"editor hide retains illumination and all shadow caches");
        Check(submitView(0).colorDraws==5 && submitView(1).colorDraws==5,"Camera outputs ignore Scene View visibility");
        desc.editorHiddenEntities={};desc.pickingEnabled=false;submit();
        Check(read()==visiblePixels,"show restores exact Scene View image");
        for(int round=0;round<3;++round) {
            for(unsigned i=0;i<2;++i){const auto value=submitView(i);
                Check(!value.decisions[0].executed && !value.decisions[1].executed && !value.decisions[3].executed,"independent cascade caches and shared Point/Spot exact reuse");}
            Check(!submit().decisions[0].executed,"main cascade cache survives auxiliary views");
        }
        const auto allocations0=views[0].color.ReallocationCount();
        Check(views[0].color.OnResize(192,128),"resize Camera target");
        Check(views[0].color.ReallocationCount()==allocations0+1,"one target reallocation for changed extent");
        Check(views[0].color.OnResize(192,128) && views[0].color.ReallocationCount()==allocations0+1,"same extent never reallocates");
        Check(submitView(0).decisions[0].executed && !submitView(0).decisions[0].executed,"aspect change invalidates only corresponding cascade once");
        view1.GetComponent<RenderCameraComponent>().orthographicHeight=8;
        Check(submitView(1).decisions[0].executed,"Camera edit updates authoritative view and depth");
        Check(!submitView(0).decisions[0].executed,"other Camera cache remains valid after edit");
        submitter.RetireCameraCaches();
        Check(submitView(0).decisions[0].executed,"retired target cache recreated safely");
        // Bounded matched 0/1/2 additional-view work. No readback in timed regions.
        PassTiming viewTiming;Check(viewTiming.Initialize(true,"views-timing.csv"),"multi-view delayed timing");
        std::ofstream viewFrames("views-frames.csv");viewFrames<<"frame,views,cpu_ns,draws,allocations,bytes,target_reallocations\n";
        {
            PassTiming::Activation activation(viewTiming);unsigned frameIndex{};
            for(unsigned viewCount=0;viewCount<=2;++viewCount)for(unsigned frame=0;frame<144;++frame) {
                Check(viewTiming.BeginFrame(),"view timing begin");
                Rbs::ReviewMetrics::allocations=Rbs::ReviewMetrics::bytes=0;Rbs::ReviewMetrics::counting=true;
                const auto begin=std::chrono::steady_clock::now();std::size_t draws{};
                for(unsigned i=0;i<viewCount;++i)draws+=submitView(i).submittedDrawCalls;
                draws+=submit().submittedDrawCalls;
                const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
                Rbs::ReviewMetrics::counting=false;viewTiming.EndFrame();
                viewFrames<<++frameIndex<<','<<viewCount<<','<<ns<<','<<draws<<','<<Rbs::ReviewMetrics::allocations<<','<<Rbs::ReviewMetrics::bytes<<','<<views[0].color.ReallocationCount()+views[1].color.ReallocationCount()<<'\n';
            }
        }
        viewTiming.Reset();
        const auto retired=Take(scene.RenderData().Identify(view0),"retired Camera id");
        Check(scene.DestroyEntity(view0),"delete Camera");
        {auto access=resources->Publication().BeginFrame();CameraViewport viewport;viewport.pixelSize={128,128};viewport.logicalSize={128,128};
            Check(!scene.SnapshotSceneCamera(retired,resources->ForFrame(access),viewport),"stale Camera snapshot rejected");}
        Check(glGetError()==GL_NO_ERROR,"multi-view GL clean");
        std::println("[PASS] editor-views checks={} visibility_isolation two_views authoritative_projection caches resize stale_camera",checks);
