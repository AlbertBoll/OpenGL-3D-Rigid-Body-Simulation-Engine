        auto& authored=lamp.GetComponent<RenderLightComponent>();
        authored.intensity=.12f;authored.castShadows=false;authored.spotShadow.resolution=512;
        lamp.Transform().Translation={-2,2,3};
        lamp.Transform().QuatRotation=glm::rotation(glm::vec3(0,0,-1),glm::normalize(-lamp.Transform().Translation));
        std::array<_Entity,4> casters;
        for(int i=0;i<4;++i) {
            casters[i]=SceneOperationChecked([&]{return scene.CreateEntity("spot caster");});
            casters[i].AddComponent<MeshRendererComponent>(MeshRendererComponent{box,opaque});
            casters[i].Transform().Scale=glm::vec3(.45f);
            casters[i].Transform().Translation={-.8f+.55f*i,0,1.1f};
        }
        auto submit=[&]{auto access=resources->Publication().BeginFrame();auto frame=Take(Extract(scene,*resources,access,camera),"spot extraction");return Take(submitter.Submit(frame,desc,picks),"spot submission");};
        auto offStats=submit();auto off=read();save("spot-off",off);
        Check(!offStats.decisions[3].requested && !offStats.decisions[3].executed,"Cast Shadows OFF skips Spot pass");
        authored.castShadows=true;auto onStats=submit();auto on=read();save("spot-on",on);
        unsigned darkened{};for(std::size_t i=0;i<on.size();i+=4)if(int(off[i])-int(on[i])>3)++darkened;
        Check(onStats.decisions[3].executed && onStats.shadowVisibility[2].layerCount==1 && darkened>100,"real 2D Spot shadow darkens visible receivers");
        Check(!submit().decisions[3].executed && read()==on,"unchanged light/casters reuse exact shadow content");
        desc.instancingEnabled=false;desc.shadowCullingEnabled=false;submitter.InvalidatePassContents();submit();
        Check(read()==on,"Spot serial broadcast agrees with instanced culled image");
        desc.instancingEnabled=true;desc.shadowCullingEnabled=true;
        casters[0].Transform().Translation.y=.6f;Check(submit().decisions[3].executed && read()!=on,"moving caster updates shadow");
        auto moved=read();lamp.Transform().Translation.x=-1.5f;Check(submit().decisions[3].executed && read()!=moved,"moving light updates shadow");
        auto lightMoved=read();lamp.Transform().QuatRotation=glm::rotation(glm::vec3(0,0,-1),glm::normalize(glm::vec3(.2f,-.3f,-1)));
        Check(submit().decisions[3].executed && read()!=lightMoved,"rotating Spot updates projection and contribution");
        authored.outerConeRadians=.8f;Check(submit().decisions[3].executed,"outer cone invalidates depth projection");
        authored.range=8;Check(submit().decisions[3].executed,"range invalidates shadow clip projection");
        authored.spotShadow.resolution=256;Check(submit().decisions[3].executed,"resolution reallocation invalidates depth");
        authored.spotShadow.bias=.002f;Check(!submit().decisions[3].executed,"receiver bias changes without unnecessary depth pass");
        for(int radius=0;radius<=2;++radius){authored.spotShadow.pcfRadius=radius;submit();save("pcf-"+std::to_string(radius),read());}
        auto beforeInvalid=read();auto invalid=authored.spotShadow;invalid.resolution=0;
        Check(!submitter.PrepareSpotShadowTarget(invalid) && read()==beforeInvalid,"invalid resolution retains valid target/image");
        invalid=authored.spotShadow;invalid.bias=std::numeric_limits<float>::quiet_NaN();Check(!submitter.PrepareSpotShadowTarget(invalid),"nonfinite bias rejected");
        invalid=authored.spotShadow;invalid.pcfRadius=3;Check(!submitter.PrepareSpotShadowTarget(invalid),"unsupported PCF rejected");
        // Actual allocation failure injection only at the driver fixture boundary.
        {
            SpotFailure::real=glad_glTexImage2D;glad_glTexImage2D=SpotFailure::Image;
            invalid=authored.spotShadow;invalid.resolution=1024;
            Check(!submitter.PrepareSpotShadowTarget(invalid),"failed depth allocation rejected");glad_glTexImage2D=SpotFailure::real;
            while(glGetError()!=GL_NO_ERROR){}
            Check(!submit().decisions[3].executed && read()==beforeInvalid,"failed allocation preserves cached previous target");
        }
        auto mesh=casters[1].GetComponent<MeshRendererComponent>();mesh.material=masked;Check(scene.RenderData().Replace(*scene.RenderData().Identify(casters[1]),mesh),"masked material edit");Check(submit().decisions[3].executed,"masked coverage invalidates depth");save("masked-caster",read());
        desc.pickingEnabled=true;auto picked=submit();Check(picked.decisions[2].executed && picked.pickDraws==5,"picking retained with Spot shadows");desc.pickingEnabled=false;
        auto sun=SceneOperationChecked([&]{return scene.CreateEntity("sun regression");});RenderLightComponent sunLight;
        sunLight.kind=RenderLightKind::Directional;sunLight.intensity=.2f;sunLight.castShadows=true;sun.AddComponent<RenderLightComponent>(sunLight);
        sun.Transform().QuatRotation=glm::rotation(glm::vec3(0,0,-1),glm::normalize(glm::vec3(-.3f,-.4f,-1)));
        auto bulb=SceneOperationChecked([&]{return scene.CreateEntity("point regression");});RenderLightComponent bulbLight;
        bulbLight.kind=RenderLightKind::Point;bulbLight.intensity=.05f;bulbLight.range=10;bulbLight.castShadows=true;bulb.AddComponent<RenderLightComponent>(bulbLight);bulb.Transform().Translation={2,2,3};
        authored.intensity=0;auto other=submit();auto otherImage=read();auto otherDepth=ShadowDepth(pointDepth,cascadeDepth);
        Check(other.decisions[0].executed && other.decisions[1].executed && !other.decisions[3].requested,"Directional/Point retain their independent passes");
        sun.Transform().Translation={99,48,-70};submit();Check(read()==otherImage && ShadowDepth(pointDepth,cascadeDepth)==otherDepth,"Directional position does not affect illumination/shadows");
        authored.intensity=.12f;authored.castShadows=false;submit();auto mixedOff=read();authored.castShadows=true;submit();auto mixedOn=read();
        Check(ShadowDepth(pointDepth,cascadeDepth)==otherDepth,"Spot on/off does not alter Directional/Point depth targets");
        authored.intensity=0;submit();Check(read()==otherImage,"Spot disabled leaves other contributions exact");
        authored.intensity=.12f;save("mixed-spot-on",mixedOn);save("mixed-spot-off",mixedOff);
        Check(mixedOn!=mixedOff,"only corresponding Spot contribution is shadowed in mixed lighting");
        // RBS preview regression: use the same adapter with actual retained snapshots.
        const auto savedCamera=camera;const auto savedSplits=desc.cascadeSplits;
        for(auto projection:{CameraProjection::Orthographic,CameraProjection::Perspective}) {
            CameraSnapshotDesc preview;preview.position={0,0,8};preview.projection=projection;
            preview.orthographicHeight=6;preview.nearPlane=.1f;preview.farPlane=40;
            preview.viewport.pixelSize={size,size};
            const auto adapted=Rbs::Editor::CameraCascadeSplits(std::array<float,5>{20,40,100,320,1000},double(.1f),1000,preview.nearPlane,preview.farPlane);
            Check(adapted,"preview cascade binding");desc.cascadeSplits=*adapted;
            camera=FrameCamera::FromSnapshot(savedCamera.entity,Take(CameraSnapshot::Create(preview),"preview snapshot"));
            const auto previewStats=submit();
            Check(previewStats.colorDraws==5 && !previewStats.decisions[3].executed,"edited camera submits without invalidating camera-independent Spot depth");
        }
        camera=savedCamera;desc.cascadeSplits=savedSplits;
        Check(submit().colorDraws==5,"return to Editor camera after preview");
        // Delayed query timing: no image/query readback, glFinish or present in this loop.
        PassTiming timing;Check(timing.Initialize(true,"spot-timing.csv"),"timing init");
        std::ofstream frames("spot-frames.csv");frames<<"frame,case,cpu_ns,draws,spot_executed,spot_casters,texture_allocations,cpu_allocations,cpu_bytes\n";
        ShadowCalls::Observe allocations;unsigned frameNumber{};
        {
        PassTiming::Activation activation(timing);
        for(int scenario=0;scenario<3;++scenario) {
            authored.castShadows=scenario!=0;submit();
            for(int frame=0;frame<144;++frame) {
                Check(timing.BeginFrame(),"delayed timing frame");
                if(scenario==2)casters[0].Transform().Translation.x=-.8f+.1f*std::sin(float(frame)*.1f);
                Rbs::ReviewMetrics::allocations=Rbs::ReviewMetrics::bytes=0;Rbs::ReviewMetrics::counting=true;
                const auto start=std::chrono::steady_clock::now();auto stats=submit();
                Rbs::ReviewMetrics::counting=false;
                const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                timing.EndFrame();
                frames<<++frameNumber<<','<<scenario<<','<<ns<<','<<stats.submittedDrawCalls<<','<<stats.decisions[3].executed<<','<<stats.shadowVisibility[2].total.submittedCasters<<','<<ShadowCalls::allocations<<','<<Rbs::ReviewMetrics::allocations<<','<<Rbs::ReviewMetrics::bytes<<'\n';
                Check(ShadowCalls::allocations==0,"no per-frame shadow texture allocation");
            }
        }
        }
        timing.Reset();Check(frames.good() && glGetError()==GL_NO_ERROR,"timing output and GL clean");
        std::println("[PASS] camera-light-spot checks={} darkened_pixels={} (normal owner teardown follows)",checks,darkened);
