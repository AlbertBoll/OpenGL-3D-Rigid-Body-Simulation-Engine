#include "gepch.h"
#include "Mesh/MeshImporter.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace GEngine
{
    namespace
    {
        using Code = MeshImportErrorCode;
        using Field = MeshImportField;
        using V = std::array<double, 3>;
        struct Vertex { MeshImportVector position, normal; MeshImportUV uv; MeshImportVector tangent, bitangent; };
        struct Frame { V normal{}, tangent{}, bitangent{}; };
        auto Error(Code code, std::size_t part = 0, std::size_t element = 0, Field field = Field::None) noexcept
        { return std::unexpected(MeshImportError{code, part, element, field}); }
        V ToDouble(const MeshImportVector& v) noexcept { return {v[0],v[1],v[2]}; }
        V Sub(V a, V b) noexcept { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
        V Scale(V a, double s) noexcept { return {a[0]*s,a[1]*s,a[2]*s}; }
        double Dot(V a, V b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
        V Cross(V a, V b) noexcept { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
        void Add(V& a, V b) noexcept { for (int i=0;i<3;++i) a[i]+=b[i]; }
        bool Unit(V& v) noexcept
        {
            const double length = std::hypot(v[0],v[1],v[2]);
            if (!std::isfinite(length) || length == 0) return false;
            v = Scale(v,1/length);
            return true;
        }
        bool Store(V v, MeshImportVector& out) noexcept
        {
            for (int i=0;i<3;++i)
            {
                if (!std::isfinite(v[i]) || std::abs(v[i]) > (std::numeric_limits<float>::max)()) return false;
                out[i]=static_cast<float>(v[i]);
            }
            return true;
        }
        bool Fits(std::size_t count, std::size_t width) noexcept
        { return count <= static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)()) / width; }
        V Transform(const std::array<double,16>& m, V v, bool position) noexcept
        {
            V out{};
            for (int i=0;i<3;++i) out[i]=m[i*4]*v[0]+m[i*4+1]*v[1]+m[i*4+2]*v[2]+(position?m[i*4+3]:0);
            return out;
        }
    }

    std::expected<MeshAsset, MeshImportError> NormalizeImportedMesh(
        std::span<const MeshImportPart> parts, std::uint32_t materialSlotCount,
        const MeshImportOptions& options) noexcept
    {
        if (options.handedness > MeshSourceHandedness::Left || options.winding > MeshSourceWinding::Clockwise
            || options.normals > MissingMeshNormals::Reject || options.uvs > MissingMeshUVs::Reject
            || options.tangents > MissingMeshTangents::Reject) return Error(Code::InvalidOptions);
        if (parts.empty()) return Error(Code::EmptyMesh);
        std::size_t vertexCount=0, indexCount=0;
        for (std::size_t p=0;p<parts.size();++p)
        {
            const auto& part=parts[p];
            const auto count=part.positions.size();
            if (!count || part.indices.empty()) return Error(Code::EmptyMesh,p);
            if (part.indices.size()%3) return Error(Code::InvalidTriangleRange,p,part.indices.size(),Field::Index);
            if (part.materialSlot >= materialSlotCount) return Error(Code::MaterialSlotOutOfRange,p,part.materialSlot,Field::Material);
            if (count > UINT32_MAX-vertexCount || part.indices.size() > SIZE_MAX-indexCount) return Error(Code::SizeOverflow,p);
            vertexCount+=count; indexCount+=part.indices.size();
        }
        if (!Fits(vertexCount,sizeof(Vertex)) || !Fits(vertexCount,sizeof(Frame))
            || !Fits(indexCount,sizeof(std::uint32_t)) || !Fits(parts.size(),sizeof(SubmeshRange))) return Error(Code::SizeOverflow);
        std::unique_ptr<Vertex[]> vertices(new(std::nothrow) Vertex[vertexCount]{});
        std::unique_ptr<Frame[]> frames(new(std::nothrow) Frame[vertexCount]{});
        std::unique_ptr<std::uint32_t[]> indices(new(std::nothrow) std::uint32_t[indexCount]);
        std::unique_ptr<SubmeshRange[]> ranges(new(std::nothrow) SubmeshRange[parts.size()]);
        if (!vertices || !frames || !indices || !ranges) return Error(Code::AllocationFailed);
        std::size_t base=0, first=0;
        for (std::size_t p=0;p<parts.size();++p)
        {
            const auto& part=parts[p];
            const auto count=part.positions.size();
            const bool normals=!part.normals.empty(), uvs=!part.uvs.empty(), tangents=!part.tangents.empty();
            const std::array<std::pair<std::size_t,Field>,4> sizes{{{part.normals.size(),Field::Normal},
                {part.uvs.size(),Field::UV},{part.tangents.size(),Field::Tangent},{part.bitangents.size(),Field::Bitangent}}};
            for (auto [size,field]:sizes)
                if (size && size!=count) return Error(Code::AttributeCountMismatch,p,size,field);
            if (tangents!=!part.bitangents.empty()) return Error(Code::AttributeCountMismatch,p,0,Field::Bitangent);
            if (!normals && options.normals==MissingMeshNormals::Reject) return Error(Code::MissingAttribute,p,0,Field::Normal);
            if (!uvs && options.uvs==MissingMeshUVs::Reject) return Error(Code::MissingAttribute,p,0,Field::UV);
            if (!tangents && options.tangents==MissingMeshTangents::Reject) return Error(Code::MissingAttribute,p,0,Field::Tangent);
            auto m=part.transform;
            if (!std::all_of(m.begin(),m.end(),[](double x){return std::isfinite(x);})
                || m[12]!=0 || m[13]!=0 || m[14]!=0 || m[15]!=1) return Error(Code::InvalidTransform,p,0,Field::Transform);
            if (options.handedness==MeshSourceHandedness::Left) for (int j=8;j<12;++j) m[j]=-m[j];
            const V row0{m[0],m[1],m[2]}, row1{m[4],m[5],m[6]}, row2{m[8],m[9],m[10]};
            const V c0=Cross(row1,row2), c1=Cross(row2,row0), c2=Cross(row0,row1);
            const double determinant=Dot(row0,c0);
            if (!std::isfinite(determinant) || determinant==0) return Error(Code::InvalidTransform,p,0,Field::Transform);
            const bool reverse=(determinant<0)!=(options.winding==MeshSourceWinding::Clockwise);
            for (std::size_t v=0;v<count;++v)
            {
                auto& out=vertices[base+v]; auto& frame=frames[base+v];
                if (!Store(Transform(m,ToDouble(part.positions[v]),true),out.position)) return Error(Code::NonFiniteAttribute,p,v,Field::Position);
                if (normals)
                {
                    const V n=ToDouble(part.normals[v]);
                    if (!std::all_of(n.begin(),n.end(),[](double x){return std::isfinite(x);})) return Error(Code::NonFiniteAttribute,p,v,Field::Normal);
                    frame.normal=Scale(V{Dot(c0,n),Dot(c1,n),Dot(c2,n)},1/determinant);
                    if (!Unit(frame.normal)) return Error(Code::InvalidDirection,p,v,Field::Normal);
                }
                if (uvs)
                {
                    out.uv=part.uvs[v];
                    if (options.flipV) out.uv[1]=1-out.uv[1];
                    if (!std::isfinite(out.uv[0]) || !std::isfinite(out.uv[1])) return Error(Code::NonFiniteAttribute,p,v,Field::UV);
                }
                if (tangents)
                {
                    frame.tangent=Transform(m,ToDouble(part.tangents[v]),false);
                    frame.bitangent=Transform(m,ToDouble(part.bitangents[v]),false);
                    if (options.flipV) frame.bitangent=Scale(frame.bitangent,-1);
                    if (!Unit(frame.tangent)) return Error(Code::InvalidDirection,p,v,Field::Tangent);
                    if (!Unit(frame.bitangent)) return Error(Code::InvalidDirection,p,v,Field::Bitangent);
                }
            }
            for (std::size_t i=0;i<part.indices.size();i+=3)
            {
                std::array<std::uint32_t,3> triangle{part.indices[i],part.indices[i+1],part.indices[i+2]};
                for (int j=0;j<3;++j) if (triangle[j]>=count) return Error(Code::IndexOutOfRange,p,i+j,Field::Index);
                if (reverse) std::swap(triangle[1],triangle[2]);
                for (int j=0;j<3;++j) indices[first+i+j]=static_cast<std::uint32_t>(base)+triangle[j];
                const auto& a=vertices[base+triangle[0]]; const auto& b=vertices[base+triangle[1]]; const auto& c=vertices[base+triangle[2]];
                const V e1=Sub(ToDouble(b.position),ToDouble(a.position)), e2=Sub(ToDouble(c.position),ToDouble(a.position));
                const V cross=Cross(e1,e2);
                V direction=cross;
                if (!Unit(direction)) return Error(Code::DegenerateTriangle,p,i/3,Field::Position);
                if (!normals) for (auto v:triangle) Add(frames[base+v].normal,cross);
                if (!tangents && uvs)
                {
                    const double u1=static_cast<double>(b.uv[0])-a.uv[0], v1=static_cast<double>(b.uv[1])-a.uv[1];
                    const double u2=static_cast<double>(c.uv[0])-a.uv[0], v2=static_cast<double>(c.uv[1])-a.uv[1];
                    const double d=u1*v2-u2*v1;
                    if (!std::isfinite(d) || d==0) return Error(Code::TangentGenerationFailed,p,i/3,Field::UV);
                    const V t=Scale(Sub(Scale(e1,v2),Scale(e2,v1)),1/d), bt=Scale(Sub(Scale(e2,u1),Scale(e1,u2)),1/d);
                    for (auto v:triangle) { Add(frames[base+v].tangent,t); Add(frames[base+v].bitangent,bt); }
                }
            }
            for (std::size_t v=0;v<count;++v)
            {
                auto& f=frames[base+v]; auto& out=vertices[base+v];
                if (!Unit(f.normal)) return Error(Code::NormalGenerationFailed,p,v,Field::Normal);
                if (!tangents && !uvs)
                {
                    // Least-aligned axis, with stable X/Y/Z tie breaking.
                    std::size_t axis=0;
                    for (std::size_t j=1;j<3;++j) if (std::abs(f.normal[j])<std::abs(f.normal[axis])) axis=j;
                    V basis{}; basis[axis]=1;
                    f.tangent=Cross(basis,f.normal); f.bitangent=Cross(f.normal,f.tangent);
                }
                f.tangent=Sub(f.tangent,Scale(f.normal,Dot(f.normal,f.tangent)));
                if (!Unit(f.tangent) || !Unit(f.bitangent)) return Error(Code::TangentGenerationFailed,p,v,Field::Tangent);
                const double sign=Dot(Cross(f.normal,f.tangent),f.bitangent);
                if (!std::isfinite(sign) || std::abs(sign)<1e-6) return Error(Code::InvalidDirection,p,v,Field::Bitangent);
                f.bitangent=Scale(Cross(f.normal,f.tangent),sign<0?-1:1);
                Store(f.normal,out.normal); Store(f.tangent,out.tangent); Store(f.bitangent,out.bitangent);
            }
            ranges[p]={first,part.indices.size(),part.materialSlot};
            base+=count; first+=part.indices.size();
        }
        const std::array<VertexAttribute,5> attributes{{
            {VertexSemantic::Position,0,VertexScalarFormat::Float32,3,VertexInterpretation::Floating,offsetof(Vertex,position)},
            {VertexSemantic::Normal,2,VertexScalarFormat::Float32,3,VertexInterpretation::Floating,offsetof(Vertex,normal)},
            {VertexSemantic::TexCoord0,1,VertexScalarFormat::Float32,2,VertexInterpretation::Floating,offsetof(Vertex,uv)},
            {VertexSemantic::Tangent,3,VertexScalarFormat::Float32,3,VertexInterpretation::Floating,offsetof(Vertex,tangent)},
            {VertexSemantic::Bitangent,4,VertexScalarFormat::Float32,3,VertexInterpretation::Floating,offsetof(Vertex,bitangent)}}};
        auto source=MeshSourceData::FromVertices<Vertex>({vertices.get(),vertexCount},attributes);
        source.indexFormat=MeshIndexFormat::UInt32; source.indices=std::as_bytes(std::span(indices.get(),indexCount));
        source.indexCount=indexCount; source.submeshes={ranges.get(),parts.size()}; source.materialSlotCount=materialSlotCount;
        auto asset=MeshAsset::Create(source);
        if (!asset) return std::unexpected(MeshImportError{Code::MeshValidationFailed,0,0,Field::None,asset.error()});
        return std::move(*asset);
    }
}
