#pragma once
#include "Physics/ShapeBox.h"
#include "Physics/ShapeConvex.h"
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeCapsule.h"
#include "Physics/PhysicsWorld.h"
#include "InertiaValidation.h"

namespace GEngine
{
    // Immutable actual collider reference plus the Scene scale it corresponds to.
    // This preserves startup fitting conventions without scaling them twice.
    struct EditorScaleGeometry {
        Vec3f scale{1},center{};Mat3 inertia{0};std::vector<Vec3f> points;
        float radius{},halfLength{};
    };
    struct EditorScaleAccess {
        static std::size_t PointCount(const PhysicalShape& shape) {
            if(auto* p=dynamic_cast<const ShapeBox*>(&shape))return p->m_points.size();
            if(auto* p=dynamic_cast<const ShapeConvex*>(&shape))return p->m_Points.size();
            return 0;
        }
        static std::unique_ptr<PhysicalShape> Clone(const PhysicalShape& shape) {
            if(auto* p=dynamic_cast<const ShapeBox*>(&shape))return std::make_unique<ShapeBox>(*p);
            if(auto* p=dynamic_cast<const ShapeConvex*>(&shape))return std::make_unique<ShapeConvex>(*p);
            if(auto* p=dynamic_cast<const ShapeSphere*>(&shape))return std::make_unique<ShapeSphere>(*p);
            if(auto* p=dynamic_cast<const ShapeCapsule*>(&shape))return std::make_unique<ShapeCapsule>(*p);
            return {};
        }
        static auto Prepare(const PhysicalShape& shape,const Vec3f& scale,PhysicsBackend backend,const Vec3f& reference)
            -> std::expected<std::unique_ptr<PhysicalShape>,PhysicsShapeError>
        {
            if(backend==PhysicsBackend::Native && Math::IsFinite(scale) && Math::IsFinite(reference) &&
                (!glm::all(glm::greaterThan(scale,Vec3f(0))) || !glm::all(glm::greaterThan(reference,Vec3f(0)))))
                return shape.PrepareScale(scale); // Preserve the independent legacy mirrored policy.
            auto output=Clone(shape);
            if(!output)return std::unexpected(PhysicsShapeError{PhysicsShapeErrorCode::InvalidScale,"Editor scale","Collider has no supported editor scale provider"});
            if(auto result=UpdatePrepared(shape,*output,scale,backend,reference);!result)return std::unexpected(result.error());
            return output;
        }
        // Update an owned editor-only draft from immutable source geometry. The
        // caller validates source identity/revision; never pass the live collider.
        static auto UpdatePrepared(const PhysicalShape& shape,PhysicalShape& target,const Vec3f& scale,PhysicsBackend backend,const Vec3f& reference)
            -> std::expected<void,PhysicsShapeError>
        {
            const auto fail=[](const char* why){return std::unexpected(PhysicsShapeError{PhysicsShapeErrorCode::InvalidScale,"Editor scale",why});};
            if(!Math::IsFinite(scale)||!Math::IsFinite(reference))return fail("Scale must be finite");
            const bool positive=glm::all(glm::greaterThan(scale,Vec3f(0)))&&glm::all(glm::greaterThan(reference,Vec3f(0)));
            if(!positive) {
                return fail("Editor draft scale must be positive");
            }
            const bool round=shape.GetShapeType()==ShapeType::Sphere || shape.GetShapeType()==ShapeType::Capsule;
            if(round && (backend==PhysicsBackend::Jolt || shape.GetShapeType()==ShapeType::Capsule) && (scale.x!=scale.y || scale.x!=scale.z || reference.x!=reference.y || reference.x!=reference.z))
                return fail("Sphere/Capsule editor scale requires a uniform source and target");
            auto* output=&target;
            if(output->m_EditorScaleSource && output->m_EditorCurrentScale==scale)return {};
            auto source=shape.m_EditorScaleSource?shape.m_EditorScaleSource:output->m_EditorScaleSource;
            if(!source) {
                auto base=std::make_shared<EditorScaleGeometry>();base->scale=reference;base->center=shape.GetCenterOfMass();base->inertia=shape.InertiaTensor();
                if(auto* p=dynamic_cast<const ShapeBox*>(&shape))base->points=p->m_points;
                if(auto* p=dynamic_cast<const ShapeConvex*>(&shape))base->points=p->m_Points;
                if(auto* p=dynamic_cast<const ShapeSphere*>(&shape))base->radius=p->m_Radius;
                if(auto* p=dynamic_cast<const ShapeCapsule*>(&shape)){base->radius=p->m_Radius;base->halfLength=p->m_HalfLength;}
                source=std::move(base);
            }
            output->m_EditorScaleSource=source; // Retain immutable input even if a later candidate is rejected.
            const Vec3f ratio=scale/source->scale;
            if(!Math::IsFinite(ratio)||glm::any(glm::lessThanEqual(ratio,Vec3f(0))))return fail("Scale ratio is not representable");
            const auto points=[&](std::vector<Vec3f>& target,Bounds& bounds) {
                target=source->points;bounds=Bounds{};
                for(auto& p:target){p*=ratio;if(!Math::IsFinite(p))return false;bounds.Expand(p);}return true;
            };
            if(auto* p=dynamic_cast<ShapeBox*>(output)) {
                if(!points(p->m_points,p->m_bounds))return fail("Scaled Box points overflow");
            }
            if(auto* p=dynamic_cast<ShapeConvex*>(output)) {
                if(!points(p->m_Points,p->m_Bounds))return fail("Scaled hull points overflow");
                // Fixed-mass second moment under an affine scale. Preserve the
                // original sampled mass distribution; do not resample 100^3 cells.
                const glm::dmat3 inertia(source->inertia);
                const double trace=inertia[0][0]+inertia[1][1]+inertia[2][2];
                glm::dmat3 covariance=glm::dmat3(trace*.5)-inertia;
                for(int i=0;i<3;++i)for(int j=i;j<3;++j) {
                    const double value=covariance[i][j]*double(ratio[i])*ratio[j];
                    covariance[i][j]=covariance[j][i]=value;
                }
                const double nextTrace=covariance[0][0]+covariance[1][1]+covariance[2][2];
                p->m_InertiaTensor=Mat3(glm::dmat3(nextTrace)-covariance);
            }
            if(auto* p=dynamic_cast<ShapeSphere*>(output))p->m_Radius=source->radius*ratio.x;
            if(auto* p=dynamic_cast<ShapeCapsule*>(output)) {
                p->m_Radius=source->radius*ratio.x;p->m_HalfLength=source->halfLength*ratio.x;p->m_Inertia=source->inertia*(ratio.x*ratio.x);
            }
            output->m_CenterOfMass=source->center*ratio;
            const auto bounds=output->GetBounds();const auto inertia=output->InertiaTensor();
            if(shape.GetShapeType()==ShapeType::Convex) {
                const float threshold=std::max(Math::NumericalEpsilon,std::max({bounds.WidthX(),bounds.WidthY(),bounds.WidthZ()})*Math::NumericalEpsilon);
                if(bounds.WidthX()<=threshold||bounds.WidthY()<=threshold||bounds.WidthZ()<=threshold)return fail("Scaled hull is degenerate at the established geometry tolerance");
            }
            if(!output->IsValid() || !Math::IsFinite(output->m_CenterOfMass) || !Math::IsFinite(bounds.mins) || !Math::IsFinite(bounds.maxs) ||
               !PhysicsInertia::Prepare(inertia,1.f))return fail("Scaled geometry/inertia is not representable as a positive-definite inverse pair");
            output->MarkGeometryChanged();output->m_EditorScaleSource=std::move(source);output->m_EditorCurrentScale=scale;
            return {};
        }
    };
    inline auto PrepareEditorScale(const PhysicalShape& shape,const Vec3f& scale,PhysicsBackend backend,const Vec3f& reference=Vec3f(1))
        -> std::expected<std::unique_ptr<PhysicalShape>,PhysicsShapeError>
    {return EditorScaleAccess::Prepare(shape,scale,backend,reference);}
}
