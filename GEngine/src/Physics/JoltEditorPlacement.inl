// Included in JoltWorld's public section; uses the existing world and cached shapes.
PhysicsBodyResult<PhysicsPlacementResult> QueryEditorPlacement(
    std::span<const PhysicsPlacementPose> input,const PhysicsPlacementSettings& options) const override
{
    constexpr float tolerance=PhysicsPlacementSettings::ContactTolerance;
    static_assert(tolerance==JPH::cDefaultCollisionTolerance);
    const auto fail=[&](PhysicsBodyErrorCode code,std::string_view why){return Error(code,{},why);};
    PhysicsPlacementResult result;result.count=input.size();result.poses.assign(input.begin(),input.end());
    if(input.size()>EditorTransformBudget::Roots)return fail(PhysicsBodyErrorCode::Capacity,"Transform exceeds 4096-root staging budget; selection retained");
    m_EditorShapes.resize(input.size());m_EditorQueryShapes.resize(input.size());m_EditorQueryNative.resize(input.size());
    m_EditorQueryScales.assign(input.size(),Vec3f(1));m_EditorQueryBounds.resize(input.size());
    auto& shapes=m_EditorQueryShapes;auto& nativeShapes=m_EditorQueryNative;auto& scales=m_EditorQueryScales;
    std::bitset<BodyCapacity> edited;std::size_t points{};
    for(std::size_t i=0;i<input.size();++i) {
        const auto h=input[i].body;
        if(h.index>=m_Bodies.size() || m_Bodies[h.index].m_Handle!=h || !m_Bodies[h.index].m_Body)
            return fail(PhysicsBodyErrorCode::StaleHandle,"Jolt candidate identity changed");
        if(edited.test(h.index))return fail(PhysicsBodyErrorCode::InvalidHandle,"Duplicate candidate body");edited.set(h.index);
        const auto& r=m_Bodies[h.index];
        points+=EditorScaleAccess::PointCount(*r.m_Native->m_Shape);
        if(points>EditorTransformBudget::ColliderPoints)return fail(PhysicsBodyErrorCode::Capacity,"Candidate collider geometry exceeds 262144-point staging budget");
        if(r.m_Shape!=r.m_Native->m_Shape || r.m_ShapeRevision!=r.m_Shape->GetRevision())
            return fail(PhysicsBodyErrorCode::InvalidShape,"Jolt/native shape revision mismatch");
        shapes[i]=r.m_Body->GetShape();
        nativeShapes[i]=r.m_Shape;
        const bool round=r.m_Shape->GetShapeType()==ShapeType::Sphere || r.m_Shape->GetShapeType()==ShapeType::Capsule;
        const bool positive=glm::all(glm::greaterThan(input[i].referenceScale,Vec3f(0))) &&
            input[i].scale && glm::all(glm::greaterThan(*input[i].scale,Vec3f(0)));
        const bool roundValid=!round || (input[i].referenceScale.x==input[i].referenceScale.y && input[i].referenceScale.x==input[i].referenceScale.z &&
            input[i].scale && input[i].scale->x==input[i].scale->y && input[i].scale->x==input[i].scale->z);
        if(positive && roundValid && shapes[i]->IsValidScale(V(*input[i].scale/input[i].referenceScale))) {
            // Query supported relative scales directly, including a uniform group
            // gesture over Box/Hull objects with nonuniform authored source scales.
            // No hull reconstruction, wrapper allocation or live shape mutation.
            m_EditorShapes[i]={}; // Retire an earlier nonuniform draft; current geometry budget stays authoritative.
            scales[i]=*input[i].scale/input[i].referenceScale;
            if(!Math::IsFinite(scales[i]) || !Math::IsFinite(scales[i]*scales[i]))
                return fail(PhysicsBodyErrorCode::InvalidShape,"Candidate scale is not representable");
        } else if(input[i].scale) {
            auto& cache=m_EditorShapes[i];const auto scale=*input[i].scale;
            if(cache.handle!=h || cache.source!=r.m_Shape || cache.revision!=r.m_ShapeRevision || cache.scale!=scale || cache.reference!=input[i].referenceScale || !cache.shape) {
                auto native=PrepareEditorScale(*r.m_Shape,scale,PhysicsBackend::Jolt,input[i].referenceScale);
                if(!native)return fail(PhysicsBodyErrorCode::InvalidShape,"Scale rejected: Sphere/Capsule require uniform positive scale; Box/Hull require valid positive geometry");
                auto shape=MakeShape(**native);
                if(shape.HasError())return fail(PhysicsBodyErrorCode::InvalidShape,"Jolt scaled collider creation rejected");
                cache.handle=h;cache.source=r.m_Shape;cache.revision=r.m_ShapeRevision;cache.scale=scale;cache.reference=input[i].referenceScale;
                cache.native=std::move(*native);cache.shape=shape.Get();
            }
            shapes[i]=cache.shape;nativeShapes[i]=cache.native.get();
        } else m_EditorShapes[i]={};
    }
    struct Filter final : JPH::BodyFilter {
        const JoltWorld& owner;std::span<const PhysicsPlacementPose> edits;std::size_t source;const std::bitset<BodyCapacity>& selected;
        Filter(const JoltWorld& o,std::span<const PhysicsPlacementPose> e,std::size_t s,const std::bitset<BodyCapacity>& set):owner(o),edits(e),source(s),selected(set){}
        bool ShouldCollideLocked(const JPH::Body& body) const override {
            const auto index=body.GetObjectLayer()>>1;if(index>=owner.m_Bodies.size())return false;
            const auto& other=owner.m_Bodies[index];const auto& a=owner.m_Bodies[edits[source].body.index];
            if(selected.test(index))return false;
            return other.m_Body==&body && (a.m_Layer&other.m_Mask) && (other.m_Layer&a.m_Mask) &&
                !owner.m_Suppressed[edits[source].body.index][index];
        }
    };
    struct ExternalLayers final : JPH::ObjectLayerFilter {
        const std::bitset<BodyCapacity>& selected;
        explicit ExternalLayers(const std::bitset<BodyCapacity>& s):selected(s){}
        bool ShouldCollide(JPH::ObjectLayer layer) const override {const auto i=layer>>1;return i<BodyCapacity && !selected.test(i);}
    } externalLayers(edited);
    struct ExternalBroadLayers final : JPH::BroadPhaseLayerFilter {
        std::array<bool,2> present{};
        bool ShouldCollide(JPH::BroadPhaseLayer layer) const override {return layer.GetValue()<present.size() && present[layer.GetValue()];}
    } externalBroadLayers;
    // A full-world group has no external geometry. Its complete candidate-pair
    // sweep below is authoritative; querying the old world would only discard hits.
    bool hasExternal=false;
    for(std::size_t i=0;i<m_Bodies.size();++i)if(m_Bodies[i].m_Body && !edited.test(i)) {
        hasExternal=true;externalBroadLayers.present[m_Bodies[i].m_Body->GetObjectLayer()&1]=true;
    }
    const auto transform=[&](std::size_t i) {
        const auto& p=result.poses[i];return JPH::RMat44::sRotationTranslation(Q(p.rotation),V(p.position)).PreTranslated(shapes[i]->GetCenterOfMass()*V(scales[i]));
    };
    const auto shift=[&](const Vec3f& delta) {for(std::size_t i=0;i<result.count;++i)result.poses[i].position+=delta;result.translation+=delta;};
    const auto normalOf=[](const JPH::CollideShapeResult& hit) {return -V(hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero()));};
    const auto& query=m_Physics.GetNarrowPhaseQuery();
    Vec3f correctionAxis{};JPH::BodyID surfaceBody;
    if(options.surface) {
        const JPH::RRayCast ray(V(options.rayOrigin),V(options.rayDelta));JPH::RayCastResult hit;
        ++result.queries;
        if(!query.CastRay(ray,hit,externalBroadLayers, externalLayers,Filter(*this,input,0,edited)) || hit.mFraction<=0 || !std::isfinite(hit.mFraction))
            return fail(PhysicsBodyErrorCode::NoSurface,"No valid collider under the surface ray (edited bodies excluded)");
        { JPH::BodyLockRead lock(m_Physics.GetBodyLockInterface(),hit.mBodyID);
        if(!lock.Succeeded())return fail(PhysicsBodyErrorCode::StaleHandle,"Surface body unavailable");
        result.point=V(ray.GetPointOnRay(hit.mFraction));
        result.normal=V(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,ray.GetPointOnRay(hit.mFraction)));
        }
        if(!Math::IsFinite(result.normal) || glm::length(result.normal)<.99f)return fail(PhysicsBodyErrorCode::InvalidPose,"Invalid surface normal");
        correctionAxis=glm::normalize(result.normal);surfaceBody=hit.mBodyID;result.surface=true;
        if(options.alignNormal) {
            const auto up=input.front().rotation*Vec3f(0,1,0);
            result.rotation=glm::rotation(up,correctionAxis);
            for(std::size_t i=0;i<result.count;++i) {
                auto& p=result.poses[i];p.position=options.pivot+result.rotation*(p.position-options.pivot);
                p.rotation=glm::normalize(result.rotation*p.rotation);
            }
        }
        // Find the group's actual support plane. Native mirror shapes are the
        // exact source of the cached Jolt shapes; this is not a mesh AABB/pivot.
        float support=std::numeric_limits<float>::max();
        for(std::size_t i=0;i<result.count;++i) {
            const auto& p=result.poses[i];const auto* shape=nativeShapes[i];
            const auto localDirection=glm::inverse(p.rotation)*(-correctionAxis);
            const auto supportPoint=shape->Support(localDirection*scales[i],Vec3f(0),Quat(1,0,0,0),0);
            const auto point=p.position+p.rotation*(supportPoint*scales[i]);
            if(!Math::IsFinite(point))return fail(PhysicsBodyErrorCode::InvalidShape,"Invalid collider support");
            support=std::min(support,glm::dot(point,correctionAxis));
        }
        const float plane=glm::dot(result.point,correctionAxis)-support;
        if(!std::isfinite(plane) || std::abs(plane+options.offset)>options.maxCorrection)
            return fail(PhysicsBodyErrorCode::NoSurface,"Surface is outside the configured snap distance; move closer");
        shift(correctionAxis*(plane+options.maxCorrection));
        const Vec3f sweep=-correctionAxis*(2*options.maxCorrection);
        float fraction=1;bool found=false;JPH::BodyID firstBody;
        JPH::ShapeCastSettings settings;settings.mReturnDeepestPoint=true;
        for(std::size_t i=0;i<result.count;++i) {
            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
            const JPH::RShapeCast cast(shapes[i],V(scales[i]),transform(i),V(sweep));
            query.CastShape(cast,settings,JPH::RVec3::sZero(),collector,externalBroadLayers, externalLayers,Filter(*this,input,i,edited));++result.queries;
            if(collector.HadHit() && collector.mHit.mFraction<fraction) {
                if(collector.mHit.mFraction<=0)return fail(PhysicsBodyErrorCode::Penetration,"Surface approach starts obstructed; no safe bounded snap");
                fraction=collector.mHit.mFraction;firstBody=collector.mHit.mBodyID2;found=true;
            }
        }
        if(!found || firstBody!=surfaceBody)return fail(PhysicsBodyErrorCode::NoSurface,"Selected surface is outside the collider footprint or blocked by another collider");
        shift(sweep*fraction+correctionAxis*(options.offset+2*tolerance));
        if(glm::length(result.translation)>options.maxCorrection+4*tolerance)
            return fail(PhysicsBodyErrorCode::NoSurface,"Snap exceeds the configured correction distance");
    }
    // Collect the deepest contact without allocating result arrays or pruning
    // other bodies. Every corrected candidate is tested against the full world.
    struct Contacts final : JPH::CollideShapeCollector {
        bool invalid{},hit{};float depth{};Vec3f normal{};
        const JoltWorld* owner{};const JPH::Body* source{};std::size_t* queries{};
        void AddHit(const JPH::CollideShapeResult& h) override {
            const auto n=-V(h.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero()));
            if(!std::isfinite(h.mPenetrationDepth)||!Math::IsFinite(n)){invalid=true;return;}
            if(source && h.mPenetrationDepth>PhysicsPlacementSettings::ContactTolerance) {
                // Owner-thread query outside Step. The world collector already owns
                // the other body's read lock; no nested lock or synchronous wait.
                const auto* other=owner->m_Physics.GetBodyLockInterfaceNoLock().TryGetBody(h.mBodyID2);
                if(!other || ++*queries>EditorTransformBudget::NarrowQueries){invalid=true;return;}
                Contacts before;JPH::CollideShapeSettings settings;
                JPH::CollisionDispatch::sCollideShapeVsShape(source->GetShape(),other->GetShape(),JPH::Vec3::sReplicate(1),JPH::Vec3::sReplicate(1),
                    source->GetCenterOfMassTransform().ToMat44(),other->GetCenterOfMassTransform().ToMat44(),{}, {},settings,before);
                // Retain an existing shallow contact only if this exact pair does
                // not become deeper. Newly introduced overlaps still reject.
                if(!before.invalid && before.depth>0 && h.mPenetrationDepth<=before.depth+PhysicsPlacementSettings::ContactTolerance)return;
            }
            if(h.mPenetrationDepth>depth){depth=h.mPenetrationDepth;normal=n;hit=true;}
        }
    };
    JPH::CollideShapeSettings overlap;
    for(unsigned attempt=0;attempt<=8;++attempt) {
        Contacts worst;
        if(hasExternal)for(std::size_t i=0;i<result.count;++i) {
            Contacts current;
            if(options.preserveExistingContacts && !input[i].scale && !options.surface && !options.resolve) {
                current.owner=this;current.source=m_Bodies[input[i].body.index].m_Body;current.queries=&result.queries;
            }
            query.CollideShape(shapes[i],V(scales[i]),transform(i),overlap,
                JPH::RVec3::sZero(),current,externalBroadLayers, externalLayers,Filter(*this,input,i,edited));++result.queries;
            if(current.invalid)return fail(PhysicsBodyErrorCode::InvalidPose,"Invalid overlap query result");
            if(current.depth>worst.depth){worst.depth=current.depth;worst.normal=current.normal;worst.hit=current.hit;}
        }
        for(std::size_t i=0;i<result.count;++i) {
            const auto bounds=shapes[i]->GetWorldSpaceBounds(transform(i),V(scales[i]));
            Bounds b;b.mins=V(bounds.mMin);b.maxs=V(bounds.mMax);
            if(!Math::IsFinite(b.mins)||!Math::IsFinite(b.maxs))return fail(PhysicsBodyErrorCode::InvalidPose,"Candidate bounds overflow");
            m_EditorQueryBounds[i]=EditorPlacementBroadphase::Inflate(b,tolerance);
        }
        bool penetration=false;
        const bool complete=EditorPlacementBroadphase::Visit(m_EditorQueryBounds,m_EditorQueryOrder,[&](std::size_t i,std::size_t j) {
                const auto ai=input[i].body.index,bi=input[j].body.index;const auto& a=m_Bodies[ai];const auto& b=m_Bodies[bi];
                if(!(a.m_Layer&b.m_Mask)||!(b.m_Layer&a.m_Mask)||m_Suppressed[ai][bi])return true;
                Contacts pair;JPH::CollisionDispatch::sCollideShapeVsShape(shapes[i],shapes[j],V(scales[i]),
                    V(scales[j]),transform(i).ToMat44(),transform(j).ToMat44(),{}, {},overlap,pair);++result.queries;
                if(!pair.invalid && pair.depth>tolerance && options.preserveExistingContacts &&
                    !input[i].scale && !input[j].scale && !options.surface && !options.resolve) {
                    Contacts before;
                    JPH::CollisionDispatch::sCollideShapeVsShape(a.m_Body->GetShape(),b.m_Body->GetShape(),
                        JPH::Vec3::sReplicate(1),JPH::Vec3::sReplicate(1),a.m_Body->GetCenterOfMassTransform().ToMat44(),
                        b.m_Body->GetCenterOfMassTransform().ToMat44(),{}, {},overlap,before);++result.queries;
                    if(!before.invalid && before.depth>0 && pair.depth<=before.depth+tolerance)return true;
                }
                if(pair.invalid || pair.depth>tolerance){penetration=true;return false;}
                return result.queries<=EditorTransformBudget::NarrowQueries;
        });
        if(!complete)return fail(penetration?PhysicsBodyErrorCode::Penetration:PhysicsBodyErrorCode::QueryLimit,
            penetration?"Selected candidate colliders overlap; a rigid correction cannot separate them":"Candidate broadphase/query budget exhausted; selection retained");
        if(result.queries>EditorTransformBudget::NarrowQueries)return fail(PhysicsBodyErrorCode::QueryLimit,"Placement query budget exhausted");
        if(worst.depth<=tolerance){result.corrected=options.surface || attempt>0;return result;}
        if(!options.resolve)return fail(PhysicsBodyErrorCode::Penetration,"Candidate intersects a world collider; enable Auto Resolve or move it clear");
        if(attempt==8)return fail(PhysicsBodyErrorCode::QueryLimit,"No safe correction within eight contact iterations");
        if(glm::length(correctionAxis)<.5f)correctionAxis=worst.normal;
        const float alignment=glm::dot(correctionAxis,worst.normal);
        if(!std::isfinite(alignment)||alignment<.1f)return fail(PhysicsBodyErrorCode::Penetration,"Conflicting contact normals; no safe correction toward the selected surface");
        const Vec3f step=correctionAxis*((worst.depth+2*tolerance)/alignment);
        if(!Math::IsFinite(step)||glm::length(result.translation+step)>options.maxCorrection)
            return fail(PhysicsBodyErrorCode::Penetration,"Penetration exceeds the configured correction distance");
        shift(step);result.normal=correctionAxis;
    }
    return fail(PhysicsBodyErrorCode::QueryLimit,"Placement query did not converge");
}
