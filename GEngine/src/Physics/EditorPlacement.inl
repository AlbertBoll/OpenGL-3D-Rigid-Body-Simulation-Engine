// Included by PhysicsWorld.cpp. Separate from solver and collider-replacement certification.
PhysicsBodyResult<PhysicsPlacementResult> PhysicsWorld::QueryEditorPlacement(
    std::span<const PhysicsPlacementPose> poses,const PhysicsPlacementSettings& settings) const
{
    constexpr auto op="Editor placement";
    if(auto access=CheckAccess(op,true);!access)return std::unexpected(access.error());
    if(poses.empty() || poses.size()>EditorTransformBudget::Roots || m_RigidBodies.size()>EditorTransformBudget::Roots)
        return Failure(Code::Capacity,{},"Placement exceeds the 4096-body interactive world/staging budget; selection retained");
    if(!Math::IsFinite(settings.offset) || !Math::IsFinite(settings.maxCorrection) || settings.maxCorrection<=0 || !std::isfinite(settings.maxCorrection*4) ||
       !Math::IsFinite(settings.pivot) || (settings.surface && (!Math::IsFinite(settings.rayOrigin) ||
       !Math::IsFinite(settings.rayDelta) || !std::isfinite(glm::length(settings.rayDelta)) || glm::length(settings.rayDelta)<=0 || !Math::IsFinite(settings.rayOrigin+settings.rayDelta))))
        return Failure(Code::InvalidPose,{},"Invalid snap settings or surface ray");
    std::optional<std::vector<PhysicsPlacementPose>> normalizedStorage;
    std::optional<std::vector<RigidBody3D*>> bodyStorage;
    std::optional<std::vector<std::pair<std::uint32_t,std::size_t>>> indexStorage;
    try {normalizedStorage.emplace(poses.begin(),poses.end());bodyStorage.emplace(poses.size());indexStorage.emplace(std::size_t(0));indexStorage->reserve(poses.size());}
    catch(const std::bad_alloc&){return Failure(Code::Allocation,{},op);}
    auto& normalized=*normalizedStorage;auto& bodies=*bodyStorage;auto& indices=*indexStorage;
    for(std::size_t i=0;i<poses.size();++i) {
        auto& q=normalized[i].rotation;const auto length=glm::dot(q,q);
        if(!Math::IsFinite(q)||!std::isfinite(length)||std::abs(length-1.f)>1.e-4f)
            return Failure(Code::InvalidPose,poses[i].body,"Candidate rotation must be finite and normalized");
        // Match the existing atomic pose API, and satisfy Jolt's tighter unit-Q invariant.
        if(std::abs(length-1.f)>4*std::numeric_limits<float>::epsilon())q/=std::sqrt(length);
    }
    poses={normalized.data(),poses.size()};
    std::size_t points{};
    for(std::size_t i=0;i<poses.size();++i) {
        // The owner-thread mutation boundary was checked once above. No callbacks
        // or live mutation intervene: retain every handle check without N full-world scans.
        const auto& p=poses[i];auto body=Resolve(p.body,op,false);
        if(!body)return std::unexpected(body.error());
        if(!ValidPose(p.position,p.rotation) || std::abs(glm::dot(p.rotation,p.rotation)-1.f)>1.e-4f)
            return Failure(Code::InvalidPose,p.body,"Candidate pose must be finite and normalized");
        indices.emplace_back(p.body.index,i);
        if(!(*body)->m_Shape || !(*body)->m_Shape->IsValid())return Failure(Code::InvalidShape,p.body,op);
        const auto bounds=(*body)->m_Shape->GetBounds(p.position,p.rotation);
        if(!FiniteBounds(bounds))return Failure(Code::InvalidPose,p.body,"Candidate collider bounds are not representable");
        points+=EditorScaleAccess::PointCount(*(*body)->m_Shape);
        if(points>EditorTransformBudget::ColliderPoints)return Failure(Code::Capacity,p.body,"Candidate collider geometry exceeds 262144-point staging budget");
        bodies[i]=*body;
    }
    std::sort(indices.begin(),indices.end());
    for(std::size_t i=1;i<indices.size();++i)if(indices[i-1].first==indices[i].first)return Failure(Code::InvalidHandle,{},"Duplicate candidate body");
    const auto candidate=[&](PhysicsBodyHandle h)->const PhysicsPlacementPose* {
        auto found=std::lower_bound(indices.begin(),indices.end(),std::pair{h.index,std::size_t(0)});
        return found!=indices.end() && found->first==h.index && poses[found->second].body==h?&poses[found->second]:nullptr;
    };
    // Moving one endpoint or scaling a connected assembly would author a joint
    // violation. Full assemblies may receive a rigid group move/rotation only.
    for(std::size_t k=0;k<MaxDistanceJoints;++k)if(m_Joints && m_Joints[k].live) {
        const auto& d=m_Joints[k].row.desc;
        const auto *a=candidate(d.bodyA),*b=candidate(d.bodyB);
        if(!a && !b)continue;
        if(!a || !b || a->scale || b->scale)return Failure(Code::UnsupportedFeature,{},"Select the complete joint assembly; connected scale is unsupported");
        const auto* oldA=ResolveJointBody(d.bodyA);const auto* oldB=ResolveJointBody(d.bodyB);
        if(!oldA || !oldB)return Failure(Code::StaleHandle,{},op);
        const auto qa=a->rotation*glm::inverse(oldA->m_Orientation),qb=b->rotation*glm::inverse(oldB->m_Orientation);
        const auto ta=a->position-qa*oldA->m_Position,tb=b->position-qb*oldB->m_Position;
        if(std::abs(glm::dot(qa,qb))<1.f-1.e-5f || glm::length(ta-tb)>1.e-4f)
            return Failure(Code::UnsupportedFeature,{},"Connected bodies require one rigid group transform");
    }
    if(m_Backend) {
        try{return m_Backend->QueryEditorPlacement(poses,settings);}
        catch(const std::bad_alloc&){return Failure(Code::Allocation,{},"Placement query allocation failed");}
    }
    if(settings.surface || settings.resolve || settings.alignNormal)
        return Failure(Code::UnsupportedFeature,{},"Native surface cast/contact correction is unavailable; use Grid Snap and verified clear poses");
    try {
        m_EditorPlacementShapes.resize(poses.size());
        std::vector<const PhysicalShape*> shapes(poses.size());
        PhysicsPlacementResult result;result.count=poses.size();result.poses.assign(poses.begin(),poses.end());
        for(std::size_t i=0;i<poses.size();++i) {
            shapes[i]=bodies[i]->m_Shape;
            if(poses[i].scale) {
                auto& draft=m_EditorPlacementShapes[i];
                if(!draft.shape || draft.handle!=poses[i].body || draft.source!=shapes[i] || draft.revision!=shapes[i]->GetRevision() || draft.reference!=poses[i].referenceScale) {
                    draft.shape=EditorScaleAccess::Clone(*shapes[i]);draft.handle=poses[i].body;draft.source=shapes[i];draft.revision=shapes[i]->GetRevision();draft.reference=poses[i].referenceScale;
                }
                if(!draft.shape || !EditorScaleAccess::UpdatePrepared(*shapes[i],*draft.shape,*poses[i].scale,PhysicsBackend::Native,poses[i].referenceScale)) {
                    draft.shape.reset();return Failure(Code::InvalidShape,poses[i].body,"Native candidate scale rejected");
                }
                shapes[i]=draft.shape.get();
            } else m_EditorPlacementShapes[i]={};
            RigidBody3D probe=*bodies[i];probe.m_Shape=const_cast<PhysicalShape*>(shapes[i]);
            probe.m_Position=poses[i].position;probe.m_Orientation=poses[i].rotation;probe.m_DerivedData={};
            if(!shapes[i]->IsValid() || !FiniteGeometry(probe))
                return Failure(Code::InvalidShape,poses[i].body,"Candidate scale/pose has invalid derived mass or inertia");
        }
        // Conservative separation certificates from actual support geometry.
        // An inconclusive axis search is rejection, never a claimed contact normal.
        const auto separated=[&](const PhysicalShape& a,const Vec3f& pa,const Quat& qa,
                                 const PhysicalShape& b,const Vec3f& pb,const Quat& qb,
                                 double minimum=-double(PhysicsPlacementSettings::ContactTolerance),double* upper=nullptr) {
            if(a.GetShapeType()==ShapeType::Sphere && b.GetShapeType()==ShapeType::Sphere) {
                // Exact float-authored sphere geometry, evaluated in double. Avoid
                // subtracting large rounded float support points at resting contact.
                const double radii=double(static_cast<const ShapeSphere&>(a).GetRadius())+double(static_cast<const ShapeSphere&>(b).GetRadius());
                const double distance=glm::length(glm::dvec3(pb)-glm::dvec3(pa));
                const double gap=distance-radii,error=32*std::numeric_limits<double>::epsilon()*(1.+distance+radii);
                if(upper)*upper=std::max(*upper,gap+error);
                return std::isfinite(gap) && gap-error>=minimum;
            }
            const auto axis=[&](Vec3f n) {
                const auto length=glm::length(n);if(!std::isfinite(length) || length<1.e-6f)return false;n/=length;
                const auto gap=[&](const Vec3f& v) {
                    const auto x=a.Support(v,pa,qa,0),y=b.Support(-v,pb,qb,0);
                    if(!Math::IsFinite(x)||!Math::IsFinite(y))return false;
                    const double value=glm::dot(glm::dvec3(y)-glm::dvec3(x),glm::dvec3(v));
                    const double error=8*std::numeric_limits<float>::epsilon()*(1.+glm::dot(glm::abs(glm::dvec3(x))+glm::abs(glm::dvec3(y)),glm::abs(glm::dvec3(v))));
                    if(upper)*upper=std::max(*upper,value+error);
                    return value-error>=minimum;
                };
                return gap(n)||gap(-n);
            };
            if(axis(pb-pa))return true;
            for(int i=0;i<3;++i) {
                Vec3f unit(0);unit[i]=1;if(axis(unit)||axis(qa*unit)||axis(qb*unit))return true;
                for(int j=0;j<3;++j){Vec3f other(0);other[j]=1;if(axis(glm::cross(qa*unit,qb*other)))return true;}
            }
            if(a.GetShapeType()==ShapeType::Box && b.GetShapeType()==ShapeType::Box)return false; // Complete Box SAT axes.
            // Axis list is only a cheap first pass. Obtain a geometry-derived
            // direction from the existing bounded GJK distance query, then verify
            // it with the same support-plane certificate. No solver thresholds change.
            RigidBody3D probeA,probeB;probeA.m_Shape=const_cast<PhysicalShape*>(&a);probeB.m_Shape=const_cast<PhysicalShape*>(&b);
            probeA.m_Position=pa;probeA.m_Orientation=qa;probeB.m_Position=pb;probeB.m_Orientation=qb;
            Vec3f witnessA{},witnessB{};GjkDiagnostics diagnostics;
            GJK_ClosestPoints(&probeA,&probeB,witnessA,witnessB,&diagnostics);
            if(Math::IsFinite(witnessA)&&Math::IsFinite(witnessB) && axis(witnessB-witnessA))return true;
            return false;
        };
        struct Candidate {RigidBody3D* body;const PhysicalShape* shape;Vec3f position;Quat rotation;int edited=-1;};
        std::vector<Candidate> candidates;std::vector<Bounds> bounds;std::vector<std::size_t> order;
        candidates.reserve(m_RigidBodies.size());bounds.reserve(m_RigidBodies.size());
        std::vector<std::pair<RigidBody3D*,std::size_t>> membership;
        for(std::size_t i=0;i<bodies.size();++i)membership.emplace_back(bodies[i],i);
        std::sort(membership.begin(),membership.end(),[](auto a,auto b){return std::less<RigidBody3D*>{}(a.first,b.first);});
        for(auto* body:m_RigidBodies) {
            Candidate c{body,body->m_Shape,body->m_Position,body->m_Orientation};
            auto found=std::lower_bound(membership.begin(),membership.end(),body,[](auto a,auto b){return std::less<RigidBody3D*>{}(a.first,b);});
            if(found!=membership.end() && found->first==body) {
                const auto i=found->second;c.shape=shapes[i];c.position=poses[i].position;c.rotation=poses[i].rotation;c.edited=int(i);
            }
            if(!c.shape)return Failure(Code::InvalidShape,{},"Native collider unavailable");
            const auto box=c.shape->GetBounds(c.position,c.rotation);
            if(!FiniteBounds(box))return Failure(Code::InvalidPose,{},"Native collider bounds invalid");
            bounds.push_back(EditorPlacementBroadphase::Inflate(box,PhysicsPlacementSettings::ContactTolerance));candidates.push_back(c);
        }
        bool penetration=false;
        const bool complete=EditorPlacementBroadphase::Visit(bounds,order,[&](std::size_t x,std::size_t y) {
            if(candidates[x].edited<0 && candidates[y].edited<0)return true;
            if(candidates[x].edited<0)std::swap(x,y);
            const auto i=std::size_t(candidates[x].edited);const auto& target=candidates[y];auto* other=target.body;
            if(!(bodies[i]->m_CollisionLayer&other->m_CollisionMask) || !(other->m_CollisionLayer&bodies[i]->m_CollisionMask) ||
                JointSuppressesPair(bodies[i]->GetIdentity(),other->GetIdentity()))return true;
            const auto* shape=target.shape;const auto p=target.position;const auto q=target.rotation;
            if(++result.queries>EditorTransformBudget::NarrowQueries)return false;
            bool clear=separated(*shapes[i],poses[i].position,poses[i].rotation,*shape,p,q);
            if(!clear && settings.preserveExistingContacts && shapes[i]==bodies[i]->m_Shape && shape==other->m_Shape) {
                const auto& a=*bodies[i];const auto& b=*other;
                const auto same=[](const PhysicalShape& shape,const Vec3f& p,const Quat& q,const RigidBody3D& old) {
                    return p==old.m_Position && (q==old.m_Orientation || shape.GetShapeType()==ShapeType::Sphere);
                };
                clear=same(*shapes[i],poses[i].position,poses[i].rotation,a)&&same(*shape,p,q,b);
                if(!clear && ((shapes[i]->GetShapeType()==ShapeType::Box && shape->GetShapeType()==ShapeType::Box) ||
                    (shapes[i]->GetShapeType()==ShapeType::Sphere && shape->GetShapeType()==ShapeType::Sphere))) {
                    // Complete SAT proves this pair already overlaps. Compare the
                    // conservative old depth with the new support certificate; new
                    // pairs/deeper penetration are never admitted by this exception.
                    double oldUpper=-std::numeric_limits<double>::infinity();
                    const bool oldClear=separated(*a.m_Shape,a.m_Position,a.m_Orientation,*b.m_Shape,b.m_Position,b.m_Orientation,
                        -double(PhysicsPlacementSettings::ContactTolerance),&oldUpper);
                    if(!oldClear && oldUpper<0)clear=separated(*shapes[i],poses[i].position,poses[i].rotation,*shape,p,q,
                        oldUpper-double(PhysicsPlacementSettings::ContactTolerance));
                }
            }
            if(!clear){penetration=true;return false;}return true;
        });
        if(!complete)return Failure(penetration?Code::Penetration:Code::QueryLimit,{},penetration?
            "Native candidate overlaps or has unproved clearance. Move it clear; Native automatic contact correction is unavailable. Previous gestures retained.":
            "Native candidate broadphase/query budget exhausted; selection retained");
        return result;
    } catch(const std::bad_alloc&) {return Failure(Code::Allocation,{},op);}
}
