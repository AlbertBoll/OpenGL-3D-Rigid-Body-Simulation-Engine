#pragma once
#include "RestingContactDiagnosis.h"
#include "GeometryAuthoringChecks.h"
#include "Physics/ShapeCapsule.h"
#include "Physics/GJK.h"
#include <numbers>

namespace PreEditorValidation
{
#if defined(GENGINE_PHASE23_DEBUG_REPRO) || defined(GENGINE_PHASE23_DEBUG_VALIDATION)
    // Reuse the existing read-only Physics observer. Keep values, never borrowed
    // body/manifold pointers, across the observation boundary.
    struct Phase23DebugProbe
    {
        using Vec3 = ::GEngine::Math::Vec3f;
        std::vector<std::array<Vec3,3>> contacts;
        std::size_t bodies{}, capsules{}, bodyLines{}, steps{}, frames{}, enabledFrames{};
        std::size_t peakContacts{}, peakLines{};
        bool enabled{};
        ::GEngine::PhysicsDiagnostic::Observer observer;

        bool Begin()
        {
            if (::GEngine::PhysicsDiagnostic::observer) return false;
            observer.context=this;
            observer.boundary=[](void* context, ::GEngine::PhysicsDiagnostic::Stage stage,
                const ::GEngine::PhysicsWorld& world, ::GEngine::ManifoldCollector& collector,
                std::span<const ::GEngine::ContactIsland>,std::size_t,int) noexcept {
                if (stage!=::GEngine::PhysicsDiagnostic::Stage::End) return;
                auto& p=*static_cast<Phase23DebugProbe*>(context);
                ++p.steps; p.bodies=world.GetBodyCount(); p.bodyLines=0; p.capsules=0;
                const auto& bodies=::GEngine::PhysicsTestAccess::Bodies(world);
                for (const auto* body:bodies) {
                    const auto& shape=*body->m_Shape;
                    if (shape.GetShapeType()==::GEngine::ShapeType::Capsule) {
                        p.bodyLines+=115; ++p.capsules;
                    } else if (shape.GetShapeType()==::GEngine::ShapeType::Sphere) p.bodyLines+=63;
                    else if (shape.GetShapeType()==::GEngine::ShapeType::Convex)
                        p.bodyLines+=15+static_cast<const ::GEngine::ShapeConvex&>(shape).GetBoundaryEdges().size();
                    else p.bodyLines+=27;
                }
                p.contacts.clear();
                for(auto& manifold:collector.m_Manifolds) for(int i=0;i<manifold.GetNumContacts();++i) {
                    const auto contact=manifold.GetContact(i);
                    if (std::find(bodies.begin(),bodies.end(),contact.m_BodyA)==bodies.end() ||
                        std::find(bodies.begin(),bodies.end(),contact.m_BodyB)==bodies.end()) continue;
                    if (contact.m_BodyA->GetIdentity()!=manifold.GetBodyAIdentity() ||
                        contact.m_BodyB->GetIdentity()!=manifold.GetBodyBIdentity()) continue;
                    p.contacts.push_back({contact.m_BodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace),
                        contact.m_BodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace),contact.normal});
                }
                p.peakContacts=(std::max)(p.peakContacts,p.contacts.size());
            };
            ::GEngine::PhysicsDiagnostic::observer=&observer;
            return true;
        }

        bool Inspect(::GEngine::PhysicsSystem& system)
        {
            using namespace ::GEngine;
            constexpr std::size_t capacity=32768;
            const auto legacyRequested=bodyLines+50*contacts.size();
            if (legacyRequested>131072) return false;
            std::vector<FrameDebugLine> storage(capacity);
            DebugGeometry bounded(storage);
            const auto result=system.AppendDebugGeometry(bounded);
#ifdef GENGINE_PHASE23_DEBUG_REPRO
            std::vector<FrameDebugLine> fullStorage((std::max)(capacity,legacyRequested));
            DebugGeometry full(fullStorage);
            const auto complete=system.AppendDebugGeometry(full);
            const auto used=bounded.Lines().size();
            Log::GetCoreLogger()->info("PHASE23_DEBUG_REPRO bodies={} capsules={} contacts={} body_lines={} requested={} capacity={} used={} remaining={} failed_contact={} failed_primitive=48_line_contact_sphere",
                bodies,capsules,contacts.size(),bodyLines,legacyRequested,capacity,used,capacity-used,(used-bodyLines)/50);
            return !result && result.error()==DebugGeometryCode::Capacity && complete &&
                full.Lines().size()==legacyRequested && used>=bodyLines &&
                (used-bodyLines)%50==2 && capacity-used<48 && capsules==0;
#else
            const auto expected=bodyLines+5*contacts.size();
            peakLines=(std::max)(peakLines,expected);
            if (!result || bounded.Lines().size()!=expected) return false;
            std::size_t offset=0,checkedCapsules=0;
            for (const auto* body:PhysicsTestAccess::Bodies(system.GetPhysicsWorld())) {
                const auto& shape=*body->m_Shape;
                if (shape.GetShapeType()==ShapeType::Capsule) {
                    const auto& capsule=static_cast<const ShapeCapsule&>(shape);
                    for(std::size_t line=offset;line<offset+100;++line)
                        for(const auto point:{storage[line].start,storage[line].end}) {
                            const auto local=glm::inverse(body->m_Orientation)*(point-body->m_Position);
                            const float axial=(std::max)(0.f,std::abs(local.y)-capsule.GetHalfLength());
                            if (std::abs(local.x*local.x+local.z*local.z+axial*axial-
                                    capsule.GetRadius()*capsule.GetRadius())>2e-5f ||
                                storage[line].color!=glm::vec4(0,1,1,1) ||
                                storage[line].depth!=DebugDepth::Tested) return false;
                        }
                    const auto bounds=body->GetWorldBounds();
                    for(std::size_t line=offset+100;line<offset+112;++line)
                        for(const auto point:{storage[line].start,storage[line].end})
                            for(unsigned axis=0;axis<3;++axis)
                                if (std::abs(point[axis]-bounds.mins[axis])>2e-5f &&
                                    std::abs(point[axis]-bounds.maxs[axis])>2e-5f) return false;
                    ++checkedCapsules;offset+=115;
                } else if(shape.GetShapeType()==ShapeType::Sphere) offset+=63;
                else if(shape.GetShapeType()==ShapeType::Convex)
                    offset+=15+static_cast<const ShapeConvex&>(shape).GetBoundaryEdges().size();
                else offset+=27;
            }
            if (offset!=bodyLines || checkedCapsules!=capsules) return false;
#ifdef GENGINE_PHASE23_DEBUG_GALLERY
            if (bodies!=7 || capsules!=2) return false;
#else
            if (bodies!=185 || capsules!=0) return false;
#endif
            for (std::size_t i=0;i<contacts.size();++i) {
                const auto& c=contacts[i];const auto offset=bodyLines+5*i;
                const auto equal=[](Vec3 a,Vec3 b) { return glm::length(a-b)<2e-5f; };
                if (!equal(storage[offset].start,c[0]) || !equal(storage[offset].end,c[1]) ||
                    !equal(storage[offset+1].start,c[0]) ||
                    !equal(storage[offset+1].end,c[0]+c[2]*.4f)) return false;
                for(unsigned axis=0;axis<3;++axis) {
                    Vec3 extent(0);extent[axis]=.025f;
                    const auto& marker=storage[offset+2+axis];
                    if (!equal(marker.start,c[0]-extent) || !equal(marker.end,c[0]+extent) ||
                        marker.color!=glm::vec4(1) || marker.depth!=DebugDepth::Overlay) return false;
                }
            }
            if (!contacts.empty()) {
                std::vector<FrameDebugLine> shortStorage(bodyLines+4);
                DebugGeometry shortOutput(shortStorage);
                const auto shortResult=system.AppendDebugGeometry(shortOutput);
                if (shortResult || shortResult.error()!=DebugGeometryCode::Capacity ||
                    shortOutput.Lines().size()!=bodyLines) return false;
            }
            Log::GetCoreLogger()->info("PHASE23_DEBUG_CHECK bodies={} capsules={} contacts={} body_lines={} legacy_requested={} requested={} capacity={} spare={} contact_geometry=true atomic_capacity=true",
                bodies,capsules,contacts.size(),bodyLines,legacyRequested,expected,capacity,capacity-expected);
            return true;
#endif
        }
        void End() { ::GEngine::PhysicsDiagnostic::observer=nullptr; }
    };
    inline Phase23DebugProbe phase23Debug;
#endif

    inline bool ShapeClose(double actual, double expected)
    { return std::isfinite(actual) && std::abs(actual-expected) <= 2e-5*(std::max)(1.,std::abs(expected)); }
    inline bool ShapeClose(const ::GEngine::Math::Vec3f& a, const ::GEngine::Math::Vec3f& b)
    { return ShapeClose(a.x,b.x) && ShapeClose(a.y,b.y) && ShapeClose(a.z,b.z); }
    inline bool ShapeClose(const ::GEngine::Math::Mat3& a, const ::GEngine::Math::Mat3& b)
    { return ShapeClose(a[0],b[0]) && ShapeClose(a[1],b[1]) && ShapeClose(a[2],b[2]); }

    inline std::expected<std::size_t, ::GEngine::PlatformError>
    CheckCollisionShapes(::GEngine::_Scene& active, ::GEngine::SceneRenderResources& resources,
                         ::GEngine::Asset::MeshHandle boxMesh,
                         ::GEngine::Asset::MaterialInstanceHandle material)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Math;
        using namespace ::GEngine::Component;
        std::size_t checks = 0;
        const auto require = [&](bool condition, const char* name) {
            ++checks;
            if (!condition) Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_23_FAIL check={} {}", checks, name);
            return condition;
        };
#define SHAPE23_REQUIRE(condition, name) \
        if (!require(bool(condition), name)) return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "Phase23 collision shapes", name})
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_23_BEGIN");
        const Quat rotations[]{Quat(1,0,0,0),glm::angleAxis(Math::PiOver2,Vec3f(0,0,1)),
            glm::angleAxis(.71f,glm::normalize(Vec3f(1,2,3)))};
        const Vec3f directions[]{Vec3f(1,0,0),Vec3f(-1,0,0),Vec3f(0,1,0),Vec3f(0,-1,0),
            Vec3f(0,0,1),Vec3f(0,0,-1),glm::normalize(Vec3f(2,-3,4))};
        for (float half : {0.f,.5f,2.f})
        {
            auto capsule = ShapeCapsule::Create(1,half);
            SHAPE23_REQUIRE(capsule && capsule->IsValid(), "Capsule dimensions");
            // Independent cylinder/hemisphere mass decomposition. A hemisphere's
            // inertia about its own COM is 83/320*m*r^2 transverse, 2/5*m*r^2 axial.
            const double cylinderMass = 2*std::numbers::pi*half;
            const double hemisphereMass = 2*std::numbers::pi/3;
            const double mass = cylinderMass+2*hemisphereMass;
            const double axial = (cylinderMass/2+2*hemisphereMass*2/5)/mass;
            const double transverse = (cylinderMass*(3+4*double(half)*half)/12 +
                2*hemisphereMass*(83./320 + std::pow(double(half)+3./8,2)))/mass;
            Mat3 expected(0); expected[0][0]=expected[2][2]=float(transverse); expected[1][1]=float(axial);
            SHAPE23_REQUIRE(ShapeClose(capsule->InertiaTensor(),expected), "Cylinder plus hemisphere COM inertia");
            for (const auto& rotation : rotations)
            {
                const Vec3f position(3,-2,5), segment=rotation*Vec3f(0,half,0);
                const Bounds bounds=capsule->GetBounds(position,rotation);
                for (const auto& direction : directions)
                {
                    const auto point=capsule->Support(direction,position,rotation,.001f);
                    const double expectedDot=glm::dot(glm::dvec3(direction),glm::dvec3(position))+
                        std::abs(glm::dot(glm::dvec3(direction),glm::dvec3(segment)))+1.001;
                    SHAPE23_REQUIRE(ShapeClose(glm::dot(direction,point),expectedDot), "Analytic Capsule support");
                    SHAPE23_REQUIRE(ShapeClose(point,capsule->Support(direction*11.f,position,rotation,.001f)),
                                    "Support normalizes nonunit finite directions");
                }
                for (int axis=0;axis<3;++axis)
                {
                    Vec3f direction(0);direction[axis]=1;
                    SHAPE23_REQUIRE(ShapeClose(bounds.maxs[axis],capsule->Support(direction,position,rotation,0)[axis]) &&
                        ShapeClose(bounds.mins[axis],capsule->Support(-direction,position,rotation,0)[axis]),
                        "Rotated bounds equal support extrema");
                }
            }
            const Vec3f omega(1,2,-3), direction=glm::normalize(Vec3f(2,1,-1));
            const float fastest=capsule->FastestLinearSpeed(omega,direction);
            for (const auto& sample : directions)
            {
                const Vec3f p=capsule->Support(sample,{},Quat(1,0,0,0),0);
                SHAPE23_REQUIRE(fastest+2e-5f >= glm::dot(direction,glm::cross(omega,p)),
                                "Angular speed encloses surface velocities");
            }
            if (half==0)
            {
                auto sphere=ShapeSphere::Create(1);
                SHAPE23_REQUIRE(sphere && ShapeClose(sphere->InertiaTensor(),capsule->InertiaTensor()) &&
                    sphere->GetBounds().mins==capsule->GetBounds().mins &&
                    sphere->GetBounds().maxs==capsule->GetBounds().maxs, "Zero segment sphere limit");
            }
        }
        auto scaled=ShapeCapsule::Create(1,.5f);
        SHAPE23_REQUIRE(scaled && scaled->SetScale(Vec3f(2)), "Uniform Capsule scale");
        SHAPE23_REQUIRE(scaled->GetRadius()==2 && scaled->GetHalfLength()==1, "Scaled Capsule dimensions");
        const auto revision=scaled->GetRevision(); const auto inertia=scaled->InertiaTensor();
        const float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity();
        for (const Vec3f bad : {Vec3f(0),Vec3f(-1),Vec3f(1,2,1),Vec3f(nan),Vec3f(inf)})
        {
            auto failed=scaled->SetScale(bad);
            SHAPE23_REQUIRE(!failed && failed.error().code==PhysicsShapeErrorCode::InvalidScale &&
                scaled->GetRevision()==revision && scaled->GetRadius()==2 &&
                ShapeClose(scaled->InertiaTensor(),inertia), "Invalid Capsule scale preserves valid geometry");
        }
        SHAPE23_REQUIRE(scaled->SetScale(Vec3f(2)) && scaled->GetRevision()==revision &&
            scaled->SetScale(Vec3f(1)) && scaled->GetRadius()==1 && scaled->GetHalfLength()==.5f,
            "Repeated Capsule scale is absolute and unchanged scale is a no-op");
        for (const auto desc : {CapsuleShapeDesc{0,.5f},CapsuleShapeDesc{-1,.5f},CapsuleShapeDesc{nan,.5f},
            CapsuleShapeDesc{inf,.5f},CapsuleShapeDesc{1,-1},CapsuleShapeDesc{1,nan},CapsuleShapeDesc{1,inf},
            CapsuleShapeDesc{(std::numeric_limits<float>::max)(),1},CapsuleShapeDesc{1e-30f,0}})
            SHAPE23_REQUIRE(!CreateCollisionShape(desc), "Invalid or unrepresentable Capsule rejected");
        SHAPE23_REQUIRE(!CreateCollisionShape(CapsuleShapeDesc{},Vec3f(1,2,1)), "Factory enforces uniform Capsule policy");

        const auto corners=[](Vec3f half,Vec3f center=Vec3f(0)) {
            std::vector<Vec3f> points;
            for (unsigned i=0;i<8;++i) points.push_back(center+half*Vec3f(i&1?1.f:-1.f,i&2?1.f:-1.f,i&4?1.f:-1.f));
            return points;
        };
        for (const Vec3f scale : {Vec3f(1),Vec3f(2,3,4),Vec3f(-2,1,3)})
        {
            const BoxShapeDesc desc{Vec3f(.5f),{1,2,-3}};
            auto points=corners(desc.halfExtents,desc.center);for(auto& p:points)p*=scale;
            auto reference=ShapeBox::Create(points);
            auto semantic=CreateCollisionShape(desc,scale);
            SHAPE23_REQUIRE(reference && semantic && (*semantic)->GetBounds().mins==reference->GetBounds().mins &&
                (*semantic)->GetBounds().maxs==reference->GetBounds().maxs &&
                ShapeClose((*semantic)->InertiaTensor(),reference->InertiaTensor()), "Box dimensions/inertia preserve provider math");
        }
        auto sphere=CreateCollisionShape(SphereShapeDesc{1},Vec3f(2,9,4));
        SHAPE23_REQUIRE(sphere && (*sphere)->GetBounds().maxs==Vec3f(2), "Sphere X-axis scale policy retained");
        SHAPE23_REQUIRE(!CreateCollisionShape(BoxShapeDesc{Vec3f(0),{}}) &&
            !CreateCollisionShape(BoxShapeDesc{},Vec3f(0,1,1)), "Degenerate box rejected");
        for (const auto points : {corners(Vec3f(.5f)),Math::GenerateDiamond()})
        {
            ConvexHullShapeDesc desc{points};
            auto hull=CreateCollisionShape(desc); ShapeConvex reference(points);
            SHAPE23_REQUIRE(hull && reference.IsValid() && (*hull)->GetBounds().mins==reference.GetBounds().mins &&
                (*hull)->GetBounds().maxs==reference.GetBounds().maxs &&
                ShapeClose((*hull)->InertiaTensor(),reference.InertiaTensor()), "Convex/diamond preserves provider dimensions/inertia");
            const auto bounds=(*hull)->GetBounds(); desc.points.clear();
            SHAPE23_REQUIRE((*hull)->GetBounds().mins==bounds.mins && (*hull)->GetBounds().maxs==bounds.maxs,
                            "Hull owns its points independently of description lifetime");
        }
        for (const std::vector<Vec3f> points : {std::vector<Vec3f>{},std::vector<Vec3f>{{0,0,0},{1,0,0},{2,0,0},{3,0,0}},
             std::vector<Vec3f>{{0,0,0},{1,0,0},{0,1,0},{1,1,0}},std::vector<Vec3f>{{nan,0,0},{1,0,0},{0,1,0},{0,0,1}}})
            SHAPE23_REQUIRE(!CreateCollisionShape(ConvexHullShapeDesc{points}), "Invalid hull is a typed failure");

        for (const CollisionShapeDesc other : {CollisionShapeDesc(SphereShapeDesc{1}),
             CollisionShapeDesc(BoxShapeDesc{Vec3f(1),{}}),CollisionShapeDesc(CapsuleShapeDesc{1,.5f})})
            for (bool overlap : {false,true})
        {
            PhysicsWorld world;
            PhysicsBodyDesc a,b; a.orientation=rotations[2];
            b.position=overlap?Vec3f(1.6f,.15f,.1f):Vec3f(5,.15f,.1f);
            auto sa=CreateCollisionShape(CapsuleShapeDesc{1,.5f}),sb=CreateCollisionShape(other);
            SHAPE23_REQUIRE(sa && sb, "Contact shape construction");
            auto ha=world.CreateBody(a,*sa),hb=world.CreateBody(b,*sb);
            SHAPE23_REQUIRE(ha && hb && !*sa && !*sb, "Phase22 world adopts contact shapes exactly once");
            auto* ba=PhysicsTestAccess::Resolve(world,*ha);auto* bb=PhysicsTestAccess::Resolve(world,*hb);
            Vec3f pa,pb;GjkDiagnostics diagnostics;
            const auto status=GJK_GetContact(ba,bb,.001f,pa,pb,&diagnostics);
            Log::GetCoreLogger()->info("PHASE23_CONTACT other={} overlap={} status={} gjk={} epa={}",
                unsigned(bb->m_Shape->GetShapeType()),overlap,unsigned(status),
                unsigned(diagnostics.termination),unsigned(diagnostics.epa.termination));
            SHAPE23_REQUIRE(status==(overlap?GjkContactStatus::Contact:GjkContactStatus::Separated), "Capsule contact classification");
            if (overlap) SHAPE23_REQUIRE(Math::IsFinite(pa) && Math::IsFinite(pb) && glm::length2(pb-pa)>0,
                                        "Contact witnesses and derived normal are finite and nonzero");
            auto owner=CreateCollisionShape(CapsuleShapeDesc{});auto* retained=owner->get();
            auto invalid=a;invalid.mass=-1;
            SHAPE23_REQUIRE(!world.CreateBody(invalid,*owner) && owner->get()==retained,
                            "Invalid mass retains caller shape ownership");
            invalid=a;invalid.material.friction=-1;
            SHAPE23_REQUIRE(!world.CreateBody(invalid,*owner) && owner->get()==retained,
                            "Invalid material retains caller shape ownership");
            SHAPE23_REQUIRE(world.DestroyBody(*ha) && !world.ReadBody(*ha), "Destroyed body handle is stale");
            SHAPE23_REQUIRE(world.Close() && !world.ReadBody(*hb), "World close retires remaining owned shape");
        }

        // Independent authored fixtures, copies, renderer edits and scope cleanup.
        {
            auto scene=CreateRefPtr<_Scene>();
            auto box=scene->CreateEntity("independent-box"),hull=scene->CreateEntity("independent-hull"),
                 capsule=scene->CreateEntity("independent-capsule");
            SHAPE23_REQUIRE(box && hull && capsule, "Independent fixture entities");
            box->AddComponent<RigidBody3DComponent>();box->AddComponent<BoxFixture3DComponent>();
            hull->AddComponent<RigidBody3DComponent>();
            auto& fixture=hull->AddComponent<ConvexFixture3DComponent>();fixture.Shape.points=corners(Vec3f(.5f));
            capsule->AddComponent<RigidBody3DComponent>();capsule->AddComponent<CapsuleFixture3DComponent>();
            auto duplicate=scene->DuplicateEntity(*hull),capsuleCopy=scene->DuplicateEntity(*capsule);
            SHAPE23_REQUIRE(duplicate && capsuleCopy && capsuleCopy->HasAllComponents<CapsuleFixture3DComponent>(),
                            "Scene duplicates independent collider descriptions");
            SHAPE23_REQUIRE(duplicate->GetComponent<ConvexFixture3DComponent>().Shape.points==fixture.Shape.points &&
                duplicate->GetComponent<ConvexFixture3DComponent>().Shape.points.data()!=fixture.Shape.points.data(),
                "Convex duplicate owns its point storage");
            SHAPE23_REQUIRE(scene->OnRuntimeStart(), "Mesh-free fixtures start successfully");
            struct Stop { RefPtr<_Scene> scene;~Stop(){scene->OnRuntimeStop();} } stop{scene};
            auto* world=scene->GetPhysicsSystem()->GetPhysicsWorld();
            SHAPE23_REQUIRE(world && world->GetBodyCount()==5 && !box->HasAllComponents<MeshComponent>() &&
                !hull->HasAllComponents<MeshComponent>(), "No render Geometry prerequisite");
            auto handle=box->GetComponent<RigidBody3DComponent>().RuntimeBody;
            const auto initial=world->ReadBody(handle);SHAPE23_REQUIRE(initial, "Read independent box");
            SHAPE23_REQUIRE(resources.AssignRenderable(*box,{boxMesh,material}), "Assign an independent visual mesh");
            auto replacement=resources.PublishGeometry(GeometryTemplates::Sphere{2});
            SHAPE23_REQUIRE(replacement && resources.AssignRenderable(*box,{*replacement,material}), "Replace visual mesh");
            box->RemoveComponent<MeshRendererComponent>();
            const auto after=world->ReadBody(handle);
            SHAPE23_REQUIRE(after && after->localBounds.mins==initial->localBounds.mins &&
                after->localBounds.maxs==initial->localBounds.maxs &&
                box->GetComponent<RigidBody3DComponent>().RuntimeBody==handle,
                "Assign/replace/remove visual mesh preserves collider identity and dimensions");
            SHAPE23_REQUIRE(scene->DestroyEntity(*box) && !world->ReadBody(handle), "Scene destruction retires body and scale connection");
            const auto capsuleHandle=capsule->GetComponent<RigidBody3DComponent>().RuntimeBody;
            capsule->Transform().SetScale({2,1,1});
            const auto unchanged=world->ReadBody(capsuleHandle);
            SHAPE23_REQUIRE(unchanged && unchanged->localBounds.maxs==Vec3f(1,1.5f,1),
                            "Legacy nonuniform Transform callback leaves Capsule geometry unchanged");
            scene->OnRuntimeStop();
            capsule->Transform().SetScale(Vec3f(2));
            SHAPE23_REQUIRE(!capsule->GetComponent<RigidBody3DComponent>().RuntimeBody &&
                !scene->GetPhysicsSystem()->GetPhysicsWorld(), "Stop disconnects shape callbacks before world destruction");
        }
        {
            auto scene=CreateRefPtr<_Scene>();auto bad=scene->CreateEntity("bad-capsule");
            SHAPE23_REQUIRE(bad, "Bad fixture entity");
            bad->AddComponent<RigidBody3DComponent>();
            bad->AddComponent<CapsuleFixture3DComponent>().Shape.radius=0;
            auto failed=scene->OnRuntimeStart();
            SHAPE23_REQUIRE(!failed && failed.error().code==PhysicsShapeErrorCode::InvalidRadius &&
                !scene->GetPhysicsSystem()->GetPhysicsWorld() &&
                !bad->GetComponent<RigidBody3DComponent>().RuntimeBody, "Invalid startup rolls back the world and identity");
        }
        // A small fixed-step scene exercises the existing solver without changing
        // its algorithms or making a Phase21 settling/convergence claim.
        {
            auto scene=CreateRefPtr<_Scene>();auto floor=scene->CreateEntity("floor"),body=scene->CreateEntity("capsule-drop");
            SHAPE23_REQUIRE(floor && body, "Short integration scene");
            floor->AddComponent<RigidBody3DComponent>();
            auto& floorFixture=floor->AddComponent<BoxFixture3DComponent>();
            floorFixture.Shape.halfExtents={5,.5f,5};floorFixture.Property.m_InvMass=0;
            body->AddComponent<RigidBody3DComponent>().Type=BodyType::Dynamic;
            body->AddComponent<CapsuleFixture3DComponent>().Shape={.5f,1};body->Transform().Translation={0,4,0};
            SHAPE23_REQUIRE(scene->OnRuntimeStart(), "Short integration startup");
            struct Stop { RefPtr<_Scene> scene;~Stop(){scene->OnRuntimeStop();} } stop{scene};
            auto* system=scene->GetPhysicsSystem();auto* world=system->GetPhysicsWorld();
            auto handle=body->GetComponent<RigidBody3DComponent>().RuntimeBody;
            for(unsigned tick=0;tick<120;++tick)
            {
                system->Update(Timestep(1./120));
                SHAPE23_REQUIRE(world->ReadBody(handle), "Finite state during short Capsule integration");
            }
            const auto state=world->ReadBody(handle);
            SHAPE23_REQUIRE(state && state->position.y<4 && state->position.y>0, "Capsule falls and remains above the floor origin");
        }
        // Debug output reads the same body-owned Capsule, with atomic primitive capacity.
        for (const auto& rotation : rotations)
        {
            PhysicsSystem system;system.SetPhysicsWorld(new PhysicsWorld);
            auto* world=system.GetPhysicsWorld();PhysicsBodyDesc desc;desc.orientation=rotation;desc.position={3,2,-1};
            auto shape=CreateCollisionShape(CapsuleShapeDesc{1,.5f});SHAPE23_REQUIRE(shape, "Debug Capsule shape");
            auto handle=world->CreateBody(desc,*shape);SHAPE23_REQUIRE(handle, "Debug Capsule body");
            std::array<FrameDebugLine,115> lines;DebugGeometry debug(lines);
            SHAPE23_REQUIRE(system.AppendDebugGeometry(debug) && debug.Lines().size()==115, "Capsule outline, AABB and COM share debug renderer");
            for (std::size_t i=0;i<100;++i) for(const auto point:{lines[i].start,lines[i].end})
            {
                const auto local=glm::inverse(rotation)*(point-desc.position);
                const float axial=(std::max)(0.f,std::abs(local.y)-.5f);
                SHAPE23_REQUIRE(ShapeClose(local.x*local.x+local.z*local.z+axial*axial,1), "Debug endpoint lies on analytic Capsule surface");
            }
            std::array<FrameDebugLine,99> small;DebugGeometry insufficient(small);
            SHAPE23_REQUIRE(!system.AppendDebugGeometry(insufficient) && insufficient.Lines().empty(),
                            "Capsule primitive capacity failure is atomic");
        }
        auto* normal=active.GetPhysicsSystem()->GetPhysicsWorld();
        SHAPE23_REQUIRE(normal, "Normal RBS world exists");
        unsigned floors=0,spheres=0;
        for (auto id:active.GetAllEntitiesWith<RigidBody3DComponent>())
        {
            _Entity entity{id,&active};auto state=normal->ReadBody(entity.GetComponent<RigidBody3DComponent>().RuntimeBody);
            SHAPE23_REQUIRE(state && !entity.HasAllComponents<MeshComponent>(), "Normal RBS collider has no legacy CPU visual owner");
            if (entity.HasAllComponents<BoxFixture3DComponent>())
            {
                const Vec3f expected=glm::abs(entity.Transform().Scale);
                SHAPE23_REQUIRE(ShapeClose(state->localBounds.maxs-state->localBounds.mins,expected),
                                "Existing RBS ground/wall box dimensions preserved");++floors;
            }
            if (entity.HasAllComponents<SphereFixture3DComponent>())
            {
                SHAPE23_REQUIRE(state->localBounds.maxs==Vec3f(1) && state->localBounds.mins==Vec3f(-1),
                                "Existing lattice sphere radius preserved");++spheres;
            }
        }
        SHAPE23_REQUIRE(floors==5 && spheres==180 && normal->GetBodyCount()==185, "Canonical lattice membership preserved");
        auto bridgeMesh=resources.PublishGeometry(GeometryTemplates::Cube{2,3,4});
        SHAPE23_REQUIRE(bridgeMesh, "Existing scale bridge visual source");
        auto bridge=CheckGeometryPhysicsBridge(resources,*bridgeMesh,material);
        if (!bridge) return std::unexpected(bridge.error());
        checks+=*bridge;
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_23_PASS checks={} shape_math=true contacts=true mesh_independence=true lifetime=true legacy_scale=true",checks);
#undef SHAPE23_REQUIRE
        return checks;
    }
}
