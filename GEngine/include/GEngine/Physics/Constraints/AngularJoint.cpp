#include "gepch.h"
#include "AngularJoint.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GEngine
{
    namespace {
        constexpr float AngularPi=3.14159265358979323846f;
        bool Unit(const Quat& q) noexcept {
            return std::isfinite(q.w)&&std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&
                std::abs(glm::dot(q,q)-1.f)<=8*std::numeric_limits<float>::epsilon();
        }
        float Asin(float x) noexcept { return std::asin(std::clamp(x,-1.f,1.f)); }
        Quat Relative(const AngularJointDesc& d,const Quat& a,const Quat& b) noexcept {
            return glm::normalize(glm::conjugate(a*d.angular.localFrameA)*(b*d.angular.localFrameB));
        }
    }
    bool AngularJoint::IsKind(JointKind k) noexcept {
        return k==JointKind::Hinge || k==JointKind::ConstantVelocity || k==JointKind::LimitedConstantVelocity;
    }
    bool AngularJoint::Valid(const AngularJointDesc& d) noexcept {
        const auto& p=d.angular;
        if(!IsKind(d.kind)||!Math::IsFinite(d.localAnchorA)||!Math::IsFinite(d.localAnchorB)||
            !Unit(p.localFrameA)||!Unit(p.localFrameB)||!std::isfinite(p.lower)||!std::isfinite(p.upper)||
            !std::isfinite(p.halfAngleU)||!std::isfinite(p.halfAngleV))return false;
        if(d.kind==JointKind::Hinge && p.limitsEnabled)
            return p.lower>=-AngularPi && p.lower<=0 && p.upper>=0 && p.upper<=AngularPi && p.lower<p.upper;
        if(d.kind==JointKind::ConstantVelocity)return !p.limitsEnabled;
        if(d.kind==JointKind::LimitedConstantVelocity)
            return p.halfAngleU>0 && p.halfAngleU<AngularPi*.5f && p.halfAngleV>0 && p.halfAngleV<AngularPi*.5f;
        return true;
    }
    JointAngularState AngularJoint::Observe(const AngularJointDesc& d,const Quat& a,const Quat& b) noexcept {
        JointAngularState s;
        auto q=Relative(d,a,b);
        // Unique principal representative, including the exact half-turn tie.
        if(q.w<0 || (q.w==0 && (q.x<0 || (q.x==0 && (q.y<0 || (q.y==0 && q.z<0)))))) q=-q;
        s.relativeRotation=q;
        s.signedAngle=std::remainder(2*std::atan2(q.x,q.w),2*AngularPi);
        s.axisError=2*Asin(std::sqrt(q.y*q.y+q.z*q.z));
        s.cvError=2*Asin(std::abs(q.x));
        s.angleU=2*Asin(q.y);s.angleV=2*Asin(q.z);
        if(d.kind==JointKind::Hinge && d.angular.limitsEnabled) {
            s.limitU=s.signedAngle<d.angular.lower?-1:s.signedAngle>d.angular.upper?1:0;
            s.violationU=std::max({0.f,d.angular.lower-s.signedAngle,s.signedAngle-d.angular.upper});
        } else if(d.kind==JointKind::LimitedConstantVelocity) {
            s.limitU=s.angleU < -d.angular.halfAngleU?-1:s.angleU>d.angular.halfAngleU?1:0;
            s.limitV=s.angleV < -d.angular.halfAngleV?-1:s.angleV>d.angular.halfAngleV?1:0;
            s.violationU=std::max(0.f,std::abs(s.angleU)-d.angular.halfAngleU);
            s.violationV=std::max(0.f,std::abs(s.angleV)-d.angular.halfAngleV);
        }
        return s;
    }
    void AngularJointRow::Reset() noexcept {
        for(auto& r:rows)r={};
        response={};a=b=nullptr;count=0;previousDt=0;
        previousRelative=Quat(1,0,0,0);impulse=angularImpulse=Vec3f(0);
        previousLimitU=previousLimitV=0;active=impulseAvailable=false;
    }
    void AngularJointRow::Apply(std::size_t i,double value) {
        const auto& r=rows[i];const float lambda=static_cast<float>(value);
        if(a->GetInverseMass()>0) {
            a->m_LinearVelocity-=r.linear*(lambda*a->GetInverseMass());
            a->m_AngularVelocity+=inverseA*r.angularA*lambda;
        }
        if(b->GetInverseMass()>0) {
            b->m_LinearVelocity+=r.linear*(lambda*b->GetInverseMass());
            b->m_AngularVelocity+=inverseB*r.angularB*lambda;
        }
    }
    bool AngularJointRow::Prepare(RigidBody3D& bodyA,RigidBody3D& bodyB,float dt) {
        if(!(dt>0)||!std::isfinite(dt)||!AngularJoint::Valid(desc))return false;
        const bool reset=previousDt!=dt;
        a=&bodyA;b=&bodyB;previousDt=dt;impulseAvailable=false;
        active=(a->GetInverseMass()>0&&!a->IsSleeping())||(b->GetInverseMass()>0&&!b->IsSleeping());
        inverseA=a->GetInverseInertiaTensorWorldSpace();inverseB=b->GetInverseInertiaTensorWorldSpace();
        const auto pa=a->BodySpaceToWorldSpace(desc.localAnchorA),pb=b->BodySpaceToWorldSpace(desc.localAnchorB);
        const auto ra=pa-a->GetCenterOfMassWorldSpace(),rb=pb-b->GetCenterOfMassWorldSpace();
        auto q=Relative(desc,a->m_Orientation,b->m_Orientation);
        if(desc.kind==JointKind::LimitedConstantVelocity || (desc.kind==JointKind::Hinge&&desc.angular.limitsEnabled)) {
            if(q.w<0)q=-q;
        } else if(glm::dot(q,previousRelative)<0)q=-q;
        previousRelative=q;
        const Quat frame=glm::normalize(a->m_Orientation*desc.angular.localFrameA);
        const Vec3f gx(q.w,q.z,-q.y),gy(-q.z,q.w,q.x),gz(q.y,-q.x,q.w);
        if(desc.kind!=JointKind::Hinge && glm::dot(gx,gx)<=1.e-8f)return false;
        if(desc.kind==JointKind::Hinge && q.w*q.w+q.x*q.x<=1.e-8f)return false;
        const auto observed=AngularJoint::Observe(desc,a->m_Orientation,b->m_Orientation);
        std::array<double,6> old{};for(std::size_t i=0;i<6;++i)old[i]=reset?0:rows[i].lambda;
        rows={};count=3;
        for(std::size_t i=0;i<3;++i) {
            auto& row=rows[i];row.linear[i]=1;row.angularA=-glm::cross(ra,row.linear);
            row.angularB=glm::cross(rb,row.linear);row.bias=.2*double(pb[i]-pa[i])/dt;row.lambda=old[i];
        }
        auto add=[&](const Vec3f& normal,double residual,bool unilateral,double cached) {
            auto& row=rows[count++];row.angularB=frame*normal;row.angularA=-row.angularB;
            row.bias=(unilateral && residual<0?1.:.2)*residual/dt;row.unilateral=unilateral;row.lambda=cached;
        };
        const Vec3f relativeVelocity=glm::conjugate(frame)*(b->GetAngularVelocity()-a->GetAngularVelocity());
        int limitU=observed.limitU,limitV=observed.limitV;
        if(desc.kind==JointKind::Hinge) {
            add(gy,2*q.y,false,old[3]);add(gz,2*q.z,false,old[4]);
            if(desc.angular.limitsEnabled) {
                const float divisor=q.w*q.w+q.x*q.x;
                if(divisor<1.e-8f)return false;
                const Vec3f gradient=(q.w*gx+q.x*Vec3f(q.x,q.y,q.z))/divisor;
                const float future=observed.signedAngle+dt*glm::dot(gradient,relativeVelocity);
                if(!limitU)limitU=future<desc.angular.lower?-1:future>desc.angular.upper?1:0;
                if(limitU) {
                    const float gap=limitU<0?desc.angular.lower-observed.signedAngle:observed.signedAngle-desc.angular.upper;
                    add(gradient*float(limitU),gap,true,limitU==previousLimitU?old[5]:0);
                }
            }
        } else {
            add(gx,2*q.x,false,old[3]);
            // Fixed U/V row slots prevent a U exit from importing V's warm cache.
            if(desc.kind==JointKind::LimitedConstantVelocity) {
                const float boundU=std::sin(desc.angular.halfAngleU*.5f),boundV=std::sin(desc.angular.halfAngleV*.5f);
                const float futureU=q.y+.5f*dt*glm::dot(gy,relativeVelocity),futureV=q.z+.5f*dt*glm::dot(gz,relativeVelocity);
                if(!limitU)limitU=futureU < -boundU?-1:futureU>boundU?1:0;
                if(!limitV)limitV=futureV < -boundV?-1:futureV>boundV?1:0;
                if(limitU)add(gy*float(limitU),2*(limitU*q.y-boundU),true,
                    limitU==previousLimitU?old[4]:0);
                else { rows[count].unilateral=true; ++count; }
                if(limitV)add(gz*float(limitV),2*(limitV*q.z-boundV),true,
                    limitV==previousLimitV?old[5]:0);
                else { rows[count].unilateral=true; ++count; }
            }
        }
        previousLimitU=limitU;previousLimitV=limitV;
        response={};
        for(std::size_t i=0;i<count;++i)for(std::size_t j=0;j<count;++j) {
            response[i][j]=double(a->GetInverseMass()+b->GetInverseMass())*glm::dot(rows[i].linear,rows[j].linear)+
                glm::dot(rows[i].angularA,inverseA*rows[j].angularA)+glm::dot(rows[i].angularB,inverseB*rows[j].angularB);
            if(!std::isfinite(response[i][j]))return false;
        }
        if(active)for(std::size_t i=0;i<count;++i)Apply(i,rows[i].lambda);
        return a->HasFiniteState()&&b->HasFiniteState();
    }
    bool AngularJointRow::RefreshLimitedRows() {
        if(desc.kind!=JointKind::LimitedConstantVelocity &&
            !(desc.kind==JointKind::Hinge && desc.angular.limitsEnabled))return true;

        // Contact impulses are applied after Prepare and before each joint solve.
        // Remove this joint's retained row impulses only from the forecast, then
        // project the current external velocity through its bilateral block.
        Vec3f coldVA=a->GetLinearVelocity(),coldVB=b->GetLinearVelocity();
        Vec3f coldWA=a->GetAngularVelocity(),coldWB=b->GetAngularVelocity();
        for(std::size_t i=0;i<count;++i) {
            const auto& row=rows[i];const float lambda=static_cast<float>(row.lambda);
            if(a->GetInverseMass()>0) {
                coldVA+=row.linear*(lambda*a->GetInverseMass());
                coldWA-=inverseA*row.angularA*lambda;
            }
            if(b->GetInverseMass()>0) {
                coldVB-=row.linear*(lambda*b->GetInverseMass());
                coldWB-=inverseB*row.angularB*lambda;
            }
        }
        if(!Math::IsFinite(coldVA)||!Math::IsFinite(coldVB)||
            !Math::IsFinite(coldWA)||!Math::IsFinite(coldWB))return false;

        const std::size_t bilateral=desc.kind==JointKind::Hinge?5:4;
        double matrix[6][7]{};std::array<double,6> projectedLambda{};
        for(std::size_t i=0;i<bilateral;++i) {
            const auto& row=rows[i];
            for(std::size_t j=0;j<bilateral;++j)matrix[i][j]=response[i][j];
            matrix[i][bilateral]=-(glm::dot(row.linear,coldVB-coldVA)+
                glm::dot(row.angularA,coldWA)+glm::dot(row.angularB,coldWB)+row.bias);
        }
        for(std::size_t k=0;k<bilateral;++k) {
            std::size_t pivot=k;
            for(std::size_t i=k+1;i<bilateral;++i)
                if(std::abs(matrix[i][k])>std::abs(matrix[pivot][k]))pivot=i;
            if(!std::isfinite(matrix[pivot][k]) ||
                !(std::abs(matrix[pivot][k])>std::numeric_limits<double>::min()))return false;
            if(pivot!=k)for(std::size_t j=k;j<=bilateral;++j)std::swap(matrix[k][j],matrix[pivot][j]);
            for(std::size_t i=k+1;i<bilateral;++i) {
                const double factor=matrix[i][k]/matrix[k][k];
                for(std::size_t j=k;j<=bilateral;++j)matrix[i][j]-=factor*matrix[k][j];
            }
        }
        for(std::size_t i=bilateral;i-->0;) {
            double rhs=matrix[i][bilateral];
            for(std::size_t j=i+1;j<bilateral;++j)rhs-=matrix[i][j]*projectedLambda[j];
            projectedLambda[i]=rhs/matrix[i][i];
            if(!std::isfinite(projectedLambda[i]))return false;
        }
        for(std::size_t i=0;i<bilateral;++i) {
            const auto& row=rows[i];const float lambda=static_cast<float>(projectedLambda[i]);
            if(a->GetInverseMass()>0)coldWA+=inverseA*row.angularA*lambda;
            if(b->GetInverseMass()>0)coldWB+=inverseB*row.angularB*lambda;
        }
        if(!Math::IsFinite(coldWA)||!Math::IsFinite(coldWB))return false;

        auto q=Relative(desc,a->m_Orientation,b->m_Orientation);
        if(q.w<0)q=-q;
        const Quat frame=glm::normalize(a->m_Orientation*desc.angular.localFrameA);
        const Vec3f localRate=glm::conjugate(frame)*(coldWB-coldWA);
        if(!Math::IsFinite(localRate))return false;
        const auto observed=AngularJoint::Observe(desc,a->m_Orientation,b->m_Orientation);
        int nextU=observed.limitU,nextV=observed.limitV;
        const Vec3f gx(q.w,q.z,-q.y),gy(-q.z,q.w,q.x),gz(q.y,-q.x,q.w);
        if(desc.kind==JointKind::Hinge) {
            const float divisor=q.w*q.w+q.x*q.x;
            if(!(divisor>1.e-8f))return false;
            const Vec3f gradient=(q.w*gx+q.x*Vec3f(q.x,q.y,q.z))/divisor;
            const float future=observed.signedAngle+previousDt*glm::dot(gradient,localRate);
            if(!std::isfinite(future))return false;
            if(!nextU)nextU=future<desc.angular.lower?-1:future>desc.angular.upper?1:0;
            if(nextU!=previousLimitU) {
                if(count>5)Apply(5,-rows[5].lambda);
                rows[5]={};count=5;
                if(nextU) {
                    const float gap=nextU<0?desc.angular.lower-observed.signedAngle:
                        observed.signedAngle-desc.angular.upper;
                    auto& row=rows[5];row.angularB=frame*(gradient*float(nextU));
                    row.angularA=-row.angularB;row.bias=(gap<0?1.:.2)*double(gap)/previousDt;
                    row.unilateral=true;count=6;
                }
            }
        } else {
            const float boundU=std::sin(desc.angular.halfAngleU*.5f);
            const float boundV=std::sin(desc.angular.halfAngleV*.5f);
            const float futureU=q.y+.5f*previousDt*glm::dot(gy,localRate);
            const float futureV=q.z+.5f*previousDt*glm::dot(gz,localRate);
            if(!std::isfinite(futureU)||!std::isfinite(futureV))return false;
            if(!nextU)nextU=futureU < -boundU?-1:futureU>boundU?1:0;
            if(!nextV)nextV=futureV < -boundV?-1:futureV>boundV?1:0;
            if(nextU!=previousLimitU) {
                Apply(4,-rows[4].lambda);rows[4]={};rows[4].unilateral=true;
                if(nextU) {
                    auto& row=rows[4];row.angularB=frame*(gy*float(nextU));
                    row.angularA=-row.angularB;
                    const double gap=2*(nextU*q.y-boundU);
                    row.bias=(gap<0?1.:.2)*gap/previousDt;
                }
            }
            if(nextV!=previousLimitV) {
                Apply(5,-rows[5].lambda);rows[5]={};rows[5].unilateral=true;
                if(nextV) {
                    auto& row=rows[5];row.angularB=frame*(gz*float(nextV));
                    row.angularA=-row.angularB;
                    const double gap=2*(nextV*q.z-boundV);
                    row.bias=(gap<0?1.:.2)*gap/previousDt;
                }
            }
        }
        if(nextU!=previousLimitU || nextV!=previousLimitV) {
            response={};
            for(std::size_t i=0;i<count;++i)for(std::size_t j=0;j<count;++j) {
                response[i][j]=double(a->GetInverseMass()+b->GetInverseMass())*
                    glm::dot(rows[i].linear,rows[j].linear)+
                    glm::dot(rows[i].angularA,inverseA*rows[j].angularA)+
                    glm::dot(rows[i].angularB,inverseB*rows[j].angularB);
                if(!std::isfinite(response[i][j]))return false;
            }
        }
        previousLimitU=nextU;previousLimitV=nextV;
        return a->HasFiniteState()&&b->HasFiniteState();
    }
    bool AngularJointRow::Solve() {
        if(!active)return true;
        if(!RefreshLimitedRows())return false;
        std::array<double,6> base{},solution{};
        std::array<std::size_t,2> unilateral{};std::size_t unilateralCount=0;
        for(std::size_t i=0;i<count;++i) {
            const auto& r=rows[i];
            base[i]=glm::dot(r.linear,b->GetLinearVelocity()-a->GetLinearVelocity())+
                glm::dot(r.angularA,a->GetAngularVelocity())+glm::dot(r.angularB,b->GetAngularVelocity())+r.bias;
            for(std::size_t j=0;j<count;++j)base[i]-=response[i][j]*rows[j].lambda;
            if(r.unilateral && response[i][i]>0)unilateral[unilateralCount++]=i;
        }
        bool found=false;
        // Enumerate the at most four independent unilateral active sets. This is
        // the local complementarity solve, not another Physics/contact iteration.
        for(unsigned mask=0;mask<(1u<<unilateralCount)&&!found;++mask) {
            std::array<std::size_t,6> map{};std::size_t n=0;
            for(std::size_t i=0;i<count;++i)if(!rows[i].unilateral)map[n++]=i;
            for(std::size_t u=0;u<unilateralCount;++u)if(mask&(1u<<u))map[n++]=unilateral[u];
            double matrix[6][7]{};
            for(std::size_t i=0;i<n;++i) {
                for(std::size_t j=0;j<n;++j)matrix[i][j]=response[map[i]][map[j]];
                matrix[i][n]=-base[map[i]];
            }
            bool singular=false;
            for(std::size_t k=0;k<n;++k) {
                std::size_t pivot=k;for(std::size_t i=k+1;i<n;++i)if(std::abs(matrix[i][k])>std::abs(matrix[pivot][k]))pivot=i;
                if(!(std::abs(matrix[pivot][k])>std::numeric_limits<double>::min())){singular=true;break;}
                if(pivot!=k)for(std::size_t j=k;j<=n;++j)std::swap(matrix[k][j],matrix[pivot][j]);
                for(std::size_t i=k+1;i<n;++i) {
                    const double factor=matrix[i][k]/matrix[k][k];
                    for(std::size_t j=k;j<=n;++j)matrix[i][j]-=factor*matrix[k][j];
                }
            }
            if(singular)continue;
            solution={};
            for(std::size_t i=n;i-->0;) {
                double rhs=matrix[i][n];for(std::size_t j=i+1;j<n;++j)rhs-=matrix[i][j]*solution[map[j]];
                solution[map[i]]=rhs/matrix[i][i];
            }
            found=true;
            for(std::size_t i=0;i<count;++i)if(!std::isfinite(solution[i]))found=false;
            for(std::size_t u=0;u<unilateralCount;++u) {
                const auto i=unilateral[u];double residual=base[i];
                for(std::size_t j=0;j<count;++j)residual+=response[i][j]*solution[j];
                // Only machine roundoff is tolerated here, not physical error.
                const double roundoff=64*std::numeric_limits<double>::epsilon()*(1+std::abs(base[i]));
                if(mask&(1u<<u)) {if(solution[i]>roundoff)found=false;}
                else if(residual>roundoff)found=false;
            }
        }
        if(!found)return false;
        for(std::size_t i=0;i<count;++i){Apply(i,solution[i]-rows[i].lambda);rows[i].lambda=solution[i];}
        impulse=Vec3f(float(rows[0].lambda),float(rows[1].lambda),float(rows[2].lambda));
        angularImpulse=desc.kind==JointKind::Hinge?Vec3f(count>5?float(rows[5].lambda):0,float(rows[3].lambda),float(rows[4].lambda)):
            Vec3f(float(rows[3].lambda),count>4?float(rows[4].lambda):0,count>5?float(rows[5].lambda):0);
        impulseAvailable=true;
        return a->HasFiniteState()&&b->HasFiniteState();
    }
}
