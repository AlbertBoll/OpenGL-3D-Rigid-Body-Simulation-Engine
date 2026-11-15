#pragma once
#include "Math/Math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>

// Private, fixed-size numerical preparation. No SDK types, owners, allocation,
// physical tolerances or changes to the caller's floating-point environment.
namespace GEngine::PhysicsInertia
{
    using Math::Mat3;
    using Math::Quat;
    using Math::Vec3f;
    inline constexpr double DoubleTolerance=64.*std::numeric_limits<double>::epsilon();
    inline constexpr double FloatTolerance=64.*std::numeric_limits<float>::epsilon();
    inline constexpr double JacobiOffDiagonalTolerance=32.*std::numeric_limits<double>::epsilon();
    inline constexpr unsigned MaxJacobiSweeps=32;

    enum class Error { NonFinite, Asymmetric, NotPositiveDefinite, DoubleResidual,
        InvalidMass, Unrepresentable, FloatResidual, PrincipalConvergence, PrincipalRepresentation };
    inline const char* ErrorMessage(Error error) noexcept
    {
        switch(error) {
        case Error::NonFinite:return "Inertia contains nonfinite coefficients";
        case Error::Asymmetric:return "Authored inertia must be exactly symmetric";
        case Error::NotPositiveDefinite:return "Inertia must be strictly positive definite";
        case Error::DoubleResidual:return "Double inertia factorization/inverse residual failed";
        case Error::InvalidMass:return "Effective inverse mass must be finite and nonnegative";
        case Error::Unrepresentable:return "Inertia or its mass-scaled inverse is not representable";
        case Error::FloatResidual:return "Published float inertia pair failed its inverse residual";
        case Error::PrincipalConvergence:return "Principal inertia preparation did not converge within 32 sweeps";
        case Error::PrincipalRepresentation:return "Principal inertia/rotation cannot reproduce the authored tensor";
        }
        return "Invalid inertia preparation";
    }

    struct PreparedTensor
    {
        Mat3 unit{0},unitInverse{0},forward{0},inverse{0};
        float inverseMass{};
        bool preservedFloat{};
    };
    struct PreparedPrincipal
    {
        float inverseMass{};
        Vec3f inverseDiagonal{};
        Quat rotation{1,0,0,0};
    };
    static_assert(sizeof(PreparedTensor)==152);
    static_assert(sizeof(PreparedPrincipal)==32);
#if defined(GENGINE_CHAIN_WAKE_CHECKS)
    // Isolated checks observe cache reuse; ordinary builds have no counter work.
    inline thread_local std::uint64_t PrepareCalls{},PrincipalCalls{};
#endif

    namespace Detail
    {
        using Wide=glm::dmat3;
        inline bool Finite(const Wide& value) noexcept
        { for(int c=0;c<3;++c)for(int r=0;r<3;++r)if(!std::isfinite(value[c][r]))return false;return true; }
        inline bool Symmetric(const Wide& value) noexcept
        { return value[0][1]==value[1][0] && value[0][2]==value[2][0] && value[1][2]==value[2][1]; }
        inline double Norm(const Wide& value) noexcept
        {
            double result=0;
            for(int r=0;r<3;++r) {double sum=0;for(int c=0;c<3;++c)sum+=std::abs(value[c][r]);result=(std::max)(result,sum);}
            return result;
        }
        inline double Scale(const Wide& value) noexcept
        {
            double result=0;for(int c=0;c<3;++c)for(int r=0;r<3;++r)result=(std::max)(result,std::abs(value[c][r]));return result;
        }
        inline bool Relative(const Wide& actual,const Wide& expected,double tolerance) noexcept
        {
            if(!Finite(actual)||!Finite(expected))return false;
            const double norm=Norm(expected),error=Norm(actual-expected);
            return std::isfinite(error) && (norm>0 ? error<=tolerance*norm : error==0);
        }
        inline bool Products(const Wide& a,const Wide& b,double tolerance,bool backward) noexcept
        {
            if(!Finite(a)||!Finite(b))return false;
            const double denominator=backward?(std::max)(1.,Norm(a)*Norm(b)):1.;
            if(!std::isfinite(denominator))return false;
            const auto left=Wide(1)-a*b,right=Wide(1)-b*a;
            return Finite(left) && Finite(right) && Norm(left)/denominator<=tolerance && Norm(right)/denominator<=tolerance;
        }
        // lower[col][row] stores the usual lower triangular L, B=L*transpose(L).
        inline bool Factor(const Wide& normalized,Wide& lower) noexcept
        {
            lower=Wide(0);
            for(int row=0;row<3;++row)for(int col=0;col<=row;++col) {
                double value=normalized[col][row];
                for(int k=0;k<col;++k)value-=lower[k][row]*lower[k][col];
                if(row==col) {
                    if(!std::isfinite(value)||value<=0)return false;
                    lower[col][row]=std::sqrt(value);
                } else lower[col][row]=value/lower[col][col];
                if(!std::isfinite(lower[col][row]))return false;
            }
            return Relative(lower*glm::transpose(lower),normalized,DoubleTolerance);
        }
        inline bool Positive(const Wide& value) noexcept
        {
            if(!Finite(value)||!Symmetric(value))return false;
            const double scale=Scale(value);if(!std::isfinite(scale)||scale<=0)return false;
            Wide lower;return Factor(value/scale,lower);
        }
        inline std::expected<Wide,Error> Invert(const Mat3& input) noexcept
        {
            const Wide value(input);
            if(!Finite(value))return std::unexpected(Error::NonFinite);
            if(!Symmetric(value))return std::unexpected(Error::Asymmetric);
            const double scale=Scale(value);
            if(!std::isfinite(scale)||scale<=0)return std::unexpected(Error::NotPositiveDefinite);
            Wide lower;
            if(!Factor(value/scale,lower))return std::unexpected(Error::NotPositiveDefinite);
            Wide inverseLower(0);
            for(int col=0;col<3;++col)for(int row=0;row<3;++row) {
                double v=row==col?1.:0.;
                for(int k=0;k<row;++k)v-=lower[k][row]*inverseLower[col][k];
                inverseLower[col][row]=v/lower[row][row];
            }
            Wide inverse(0);
            // Evaluate each symmetric coefficient once. No arbitrary input is
            // symmetrized; symmetry follows from transpose(invL)*invL.
            for(int row=0;row<3;++row)for(int col=row;col<3;++col) {
                double value=0;for(int k=0;k<3;++k)value+=inverseLower[row][k]*inverseLower[col][k];
                inverse[row][col]=inverse[col][row]=value/scale;
            }
            if(!Finite(inverse) || !Products(value,inverse,DoubleTolerance,true))return std::unexpected(Error::DoubleResidual);
            return inverse;
        }
        inline Wide Diagonal(const glm::dvec3& value) noexcept
        { Wide out(0);for(int i=0;i<3;++i)out[i][i]=value[i];return out; }
        inline Mat3 Reconstruct(const Mat3& rotation,const Vec3f& diagonal) noexcept
        {
            Mat3 out(0);
            for(int row=0;row<3;++row)for(int col=row;col<3;++col) {
                double value=0;for(int k=0;k<3;++k)value+=double(rotation[k][row])*double(diagonal[k])*double(rotation[k][col]);
                out[row][col]=out[col][row]=static_cast<float>(value);
            }
            return out;
        }
    }

    inline bool InverseResidualWithin(const Mat3& forward,const Mat3& inverse,double tolerance=FloatTolerance) noexcept
    { return Detail::Products(Detail::Wide(forward),Detail::Wide(inverse),tolerance,false); }
    inline bool MatricesAgree(const Mat3& actual,const Mat3& expected,double tolerance=FloatTolerance) noexcept
    { return Detail::Relative(Detail::Wide(actual),Detail::Wide(expected),tolerance); }
    inline bool ValidatePair(const Mat3& forward,const Mat3& inverse) noexcept
    {
        return Detail::Positive(Detail::Wide(forward)) && Detail::Positive(Detail::Wide(inverse)) && InverseResidualWithin(forward,inverse);
    }

    inline std::expected<PreparedTensor,Error> Prepare(const Mat3& unit,float inverseMass) noexcept
    {
#if defined(GENGINE_CHAIN_WAKE_CHECKS)
        ++PrepareCalls;
#endif
        if(!std::isfinite(inverseMass)||inverseMass<0)return std::unexpected(Error::InvalidMass);
        auto wideInverse=Detail::Invert(unit);if(!wideInverse)return std::unexpected(wideInverse.error());
        PreparedTensor result;result.unit=unit;result.inverseMass=inverseMass==0?0.f:inverseMass;
        // Preserve the prior calculation only when it really produced a usable
        // result. The old absolute determinant gate is never an admission rule.
        const float oldDeterminant=glm::determinant(unit);
        bool oldValid=false;Mat3 oldInverse(0);
        if(std::isfinite(oldDeterminant) && std::abs(oldDeterminant)>Math::NumericalEpsilon) {
            oldInverse=glm::inverse(unit);oldValid=ValidatePair(unit,oldInverse);
        }
        if(oldValid)result.unitInverse=oldInverse;
        else {
            result.unitInverse=Mat3(*wideInverse);
            if(!ValidatePair(unit,result.unitInverse))return std::unexpected(Error::FloatResidual);
        }
        if(inverseMass==0) {result.preservedFloat=oldValid;return result;}
        const Detail::Wide physical=Detail::Wide(unit)/double(inverseMass);
        const Detail::Wide inverse=*wideInverse*double(inverseMass);
        if(!Detail::Finite(physical)||!Detail::Finite(inverse))return std::unexpected(Error::Unrepresentable);
        result.forward=Mat3(physical);
        if(oldValid) {
            const Mat3 oldWeighted=oldInverse*inverseMass;
            if(ValidatePair(result.forward,oldWeighted)) {result.inverse=oldWeighted;result.preservedFloat=true;return result;}
        }
        result.inverse=Mat3(inverse);
        if(!ValidatePair(result.forward,result.inverse))return std::unexpected(Error::FloatResidual);
        return result;
    }

    inline std::expected<PreparedPrincipal,Error> PreparePrincipal(const PreparedTensor& tensor) noexcept
    {
#if defined(GENGINE_CHAIN_WAKE_CHECKS)
        ++PrincipalCalls;
#endif
        if(!ValidatePair(tensor.unit,tensor.unitInverse) || !std::isfinite(tensor.inverseMass) || tensor.inverseMass<0)
            return std::unexpected(Error::PrincipalRepresentation);
        PreparedPrincipal result;result.inverseMass=tensor.inverseMass;
        if(tensor.inverseMass==0)return result;
        if(!ValidatePair(tensor.forward,tensor.inverse))return std::unexpected(Error::PrincipalRepresentation);
        const Detail::Wide original(tensor.unit);const double scale=Detail::Scale(original);
        const Detail::Wide normalized=original/scale;
        Detail::Wide diagonal=normalized,basis(1);
        const double norm=Detail::Norm(normalized);
        const auto converged=[&]() {
            double off=0;
            for(int row=0;row<3;++row) {double sum=0;for(int col=0;col<3;++col)if(row!=col)sum+=std::abs(diagonal[col][row]);off=(std::max)(off,sum);}
            return std::isfinite(off) && off<=JacobiOffDiagonalTolerance*norm;
        };
        const auto diagonalExactly=[&]() {return diagonal[0][1]==0 && diagonal[0][2]==0 && diagonal[1][2]==0;};
        // A tiny off-diagonal may matter to the inverse of a small principal
        // mode even after the scale-relative convergence test is met. Continue
        // bounded cleanup instead of discarding that coupling prematurely. The
        // frozen residual/convergence criteria still govern acceptance below.
        for(unsigned sweep=0;sweep<MaxJacobiSweeps && !diagonalExactly();++sweep) {
            for(const auto pair:std::array<std::array<int,2>,3>{{{0,1},{0,2},{1,2}}}) {
                const int p=pair[0],q=pair[1];const double off=diagonal[q][p];if(off==0)continue;
                const double tau=(diagonal[q][q]-diagonal[p][p])/(2.*off);
                const double tangent=std::copysign(1.,tau)/(std::abs(tau)+std::hypot(1.,tau));
                const double cosine=1./std::sqrt(1.+tangent*tangent),sine=tangent*cosine;
                if(!std::isfinite(tangent)||!std::isfinite(cosine)||!std::isfinite(sine))return std::unexpected(Error::PrincipalConvergence);
                diagonal[p][p]-=tangent*off;diagonal[q][q]+=tangent*off;diagonal[p][q]=diagonal[q][p]=0;
                for(int k=0;k<3;++k)if(k!=p && k!=q) {
                    const double a=diagonal[p][k],b=diagonal[q][k];
                    diagonal[p][k]=diagonal[k][p]=cosine*a-sine*b;
                    diagonal[q][k]=diagonal[k][q]=sine*a+cosine*b;
                }
                const auto a=basis[p],b=basis[q];basis[p]=cosine*a-sine*b;basis[q]=sine*a+cosine*b;
            }
        }
        if(!converged())return std::unexpected(Error::PrincipalConvergence);
        glm::dvec3 moments(diagonal[0][0],diagonal[1][1],diagonal[2][2]);
        for(int i=0;i<3;++i)if(!std::isfinite(moments[i])||moments[i]<=0)return std::unexpected(Error::NotPositiveDefinite);
        // Fixed Jacobi axis order is deterministic. Do not permute an already
        // diagonal tensor into an unnecessary float quaternion: that could turn
        // harmless axis roundoff into a large error for a small principal mode.
        const double determinant=glm::determinant(basis);
        if(!std::isfinite(determinant)||determinant==0)return std::unexpected(Error::PrincipalRepresentation);
        if(determinant<0)basis[2]=-basis[2];
        if(!Detail::Relative(basis*Detail::Diagonal(moments)*glm::transpose(basis),normalized,DoubleTolerance) ||
            !Detail::Relative(glm::transpose(basis)*basis,Detail::Wide(1),DoubleTolerance) ||
            !Detail::Relative(basis*glm::transpose(basis),Detail::Wide(1),DoubleTolerance))
            return std::unexpected(Error::PrincipalRepresentation);
        auto wideRotation=glm::quat_cast(basis);
        const double length=glm::length(wideRotation);
        if(!std::isfinite(length)||length<=0)return std::unexpected(Error::PrincipalRepresentation);
        wideRotation/=length;
        result.rotation=glm::normalize(Quat(wideRotation));
        // Quaternion sign is deterministic; physical comparison uses tensors.
        const auto& q=result.rotation;
        if(q.w<0 || (q.w==0 && (q.x<0 || (q.x==0 && (q.y<0 || (q.y==0 && q.z<0))))))result.rotation=-q;
        if(!Math::IsFinite(result.rotation))return std::unexpected(Error::PrincipalRepresentation);
        Vec3f forwardDiagonal;
        for(int i=0;i<3;++i) {
            const double physical=moments[i]*scale/double(tensor.inverseMass);
            const double inverse=double(tensor.inverseMass)/(moments[i]*scale);
            forwardDiagonal[i]=static_cast<float>(physical);result.inverseDiagonal[i]=static_cast<float>(inverse);
            if(!std::isfinite(forwardDiagonal[i])||forwardDiagonal[i]<=0 ||
                !std::isfinite(result.inverseDiagonal[i])||result.inverseDiagonal[i]<=0)
                return std::unexpected(Error::Unrepresentable);
        }
        const Mat3 rotation=glm::mat3_cast(result.rotation);
        const Detail::Wide wide(rotation);
        if(!Detail::Relative(glm::transpose(wide)*wide,Detail::Wide(1),FloatTolerance) || glm::determinant(wide)<=0)
            return std::unexpected(Error::PrincipalRepresentation);
        const Mat3 reconstructedInverse=Detail::Reconstruct(rotation,result.inverseDiagonal);
        // Reconstruct the actual published principal values, including their
        // reciprocal rounding, rather than validating only double eigenvalues.
        for(int i=0;i<3;++i)forwardDiagonal[i]=static_cast<float>(1./double(result.inverseDiagonal[i]));
        const Mat3 reconstructedForward=Detail::Reconstruct(rotation,forwardDiagonal);
        if(!ValidatePair(reconstructedForward,reconstructedInverse) || !InverseResidualWithin(tensor.forward,reconstructedInverse) ||
            !MatricesAgree(reconstructedForward,tensor.forward) || !MatricesAgree(reconstructedInverse,tensor.inverse))
            return std::unexpected(Error::PrincipalRepresentation);
        // SDK float reconstruction may have asymmetric last-bit products. The
        // adapter additionally checks that actual reconstruction's agreement and
        // two-sided residual; exact symmetry is required for authored input only.
        return result;
    }
}
