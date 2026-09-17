#include "Mesh/MeshImporter.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <thread>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

using namespace GEngine;
static int failureAfter=-1;
static int checks=0;
// Only engine nothrow array allocations are injected; the dependency DLL owns
// its allocator. Successful allocations retain normal new[]/delete[] pairing.
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    if (failureAfter==0) return nullptr;
    if (failureAfter>0) --failureAfter;
    try { return ::operator new[](size); } catch (const std::bad_alloc&) { return nullptr; }
}
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
using V=MeshImportVector;
using UV=MeshImportUV;
using Code=MeshImportErrorCode;
using Field=MeshImportField;
using Result=std::expected<MeshAsset,MeshImportError>;

static V Read(const MeshAsset& mesh, std::size_t vertex, VertexSemantic semantic)
{
    for (const auto& a:mesh.Layout().attributes)
        if (a.semantic==semantic)
        {
            V result{};
            std::memcpy(result.data(),mesh.Vertices().data()+vertex*mesh.Layout().strideBytes+a.offsetBytes,a.components*sizeof(float));
            return result;
        }
    CHECK(false); return {};
}
static std::uint32_t Index(const MeshAsset& mesh, std::size_t i)
{
    std::uint32_t result=0; std::memcpy(&result,mesh.Indices().data()+i*sizeof(result),sizeof(result)); return result;
}
static bool Near(float a, float b) { return std::abs(a-b)<1e-5f; }
static float Dot(V a,V b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static V Cross(V a,V b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
static V Sub(V a,V b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
static void FrameChecks(const MeshAsset& mesh)
{
    for (std::size_t i=0;i<mesh.VertexCount();++i)
    {
        auto n=Read(mesh,i,VertexSemantic::Normal), t=Read(mesh,i,VertexSemantic::Tangent), b=Read(mesh,i,VertexSemantic::Bitangent);
        CHECK(Near(Dot(n,n),1) && Near(Dot(t,t),1) && Near(Dot(b,b),1));
        CHECK(Near(Dot(n,t),0) && Near(Dot(n,b),0) && Near(Dot(t,b),0));
    }
    for (std::size_t i=0;i<mesh.IndexCount();i+=3)
    {
        auto a=Read(mesh,Index(mesh,i),VertexSemantic::Position), b=Read(mesh,Index(mesh,i+1),VertexSemantic::Position), c=Read(mesh,Index(mesh,i+2),VertexSemantic::Position);
        CHECK(Dot(Cross(Sub(b,a),Sub(c,a)),Read(mesh,Index(mesh,i),VertexSemantic::Normal))>0);
    }
}
static void Same(const MeshAsset& a,const MeshAsset& b)
{
    CHECK(a.Vertices().size()==b.Vertices().size() && std::equal(a.Vertices().begin(),a.Vertices().end(),b.Vertices().begin()));
    CHECK(a.Indices().size()==b.Indices().size() && std::equal(a.Indices().begin(),a.Indices().end(),b.Indices().begin()));
    CHECK(a.Submeshes().size()==b.Submeshes().size() && a.MaterialSlotCount()==b.MaterialSlotCount());
    for (std::size_t i=0;i<a.Submeshes().size();++i)
        CHECK(a.Submeshes()[i].firstElement==b.Submeshes()[i].firstElement && a.Submeshes()[i].elementCount==b.Submeshes()[i].elementCount && a.Submeshes()[i].materialSlot==b.Submeshes()[i].materialSlot);
}
static void Error(const Result& r,Code code,std::size_t part=0,std::size_t element=0,Field field=Field::None)
{ CHECK(!r); CHECK(r.error().code==code && r.error().part==part && r.error().element==element && r.error().field==field); }
static Result Memory(std::string_view text)
{ return ImportMeshMemory(std::as_bytes(std::span(text.data(),text.size())),"obj"); }

struct HeapCheckpoint
{
#ifdef _DEBUG
    _CrtMemState before{};
    HeapCheckpoint() { _CrtMemCheckpoint(&before); }
    void Verify() const
    {
        _CrtMemState after{}, difference{};
        _CrtMemCheckpoint(&after);
        CHECK(!_CrtMemDifference(&difference,&before,&after));
    }
#else
    void Verify() const {}
#endif
};

int main(int argc,char** argv)
{
    CHECK(argc==3);
    std::array<V,3> positions{{{0,0,0},{1,0,0},{0,1,0}}};
    std::array<V,3> normals{{{0,0,2},{0,0,2},{0,0,2}}};
    std::array<V,3> tangents{{{2,0,0},{2,0,0},{2,0,0}}};
    std::array<V,3> bitangents{{{0,3,0},{0,3,0},{0,3,0}}};
    std::array<UV,3> uvs{{{0,0},{1,0},{0,1}}};
    std::array<std::uint32_t,3> indices{0,1,2};
    MeshImportPart part{positions,normals,tangents,bitangents,uvs,indices};
    auto run=[&](const MeshImportOptions& options=MeshImportOptions{}) { return NormalizeImportedMesh({&part,1},3,options); };
    auto complete=run(); CHECK(complete); FrameChecks(*complete);
    CHECK(complete->IndexFormat()==MeshIndexFormat::UInt32 && complete->VertexCount()==3 && complete->MaterialSlotCount()==3);
    CHECK(Read(*complete,1,VertexSemantic::Position)==positions[1]);
    part.tangents={}; part.bitangents={}; auto generated=run(); CHECK(generated); Same(*complete,*generated);
    MeshImportOptions options; options.tangents=MissingMeshTangents::Reject;
    Error(run(options),Code::MissingAttribute,0,0,Field::Tangent);
    part.normals={}; generated=run(); CHECK(generated); Same(*complete,*generated);
    options={}; options.normals=MissingMeshNormals::Reject; Error(run(options),Code::MissingAttribute,0,0,Field::Normal);
    part.uvs={}; generated=run(); CHECK(generated); FrameChecks(*generated);
    CHECK(Read(*generated,2,VertexSemantic::TexCoord0)==V{});
    options={}; options.uvs=MissingMeshUVs::Reject; Error(run(options),Code::MissingAttribute,0,0,Field::UV);
    part={positions,normals,tangents,bitangents,uvs,indices};
    options={}; options.flipV=true; auto flippedUV=run(options); CHECK(flippedUV);
    CHECK(Read(*flippedUV,0,VertexSemantic::TexCoord0)[1]==1 && Read(*flippedUV,0,VertexSemantic::Bitangent)[1]==-1);
    part.tangents={}; part.bitangents={}; auto generatedUV=run(options); CHECK(generatedUV); Same(*flippedUV,*generatedUV);
    part={positions,normals,tangents,bitangents,uvs,indices};

    // Instance transform with reflection, nonuniform scale and translation.
    part.transform={-2,0,0,5, 0,3,0,7, 0,0,4,9, 0,0,0,1};
    auto mirror=run(); CHECK(mirror); FrameChecks(*mirror);
    CHECK((Read(*mirror,1,VertexSemantic::Position)==V{3,7,9}));
    CHECK(Index(*mirror,0)==0 && Index(*mirror,1)==2 && Index(*mirror,2)==1);
    CHECK(mirror->Bounds().minimum[0]==3 && mirror->Bounds().maximum[1]==10);
    part.transform={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    options={}; options.handedness=MeshSourceHandedness::Left;
    auto left=run(options); CHECK(left); FrameChecks(*left); CHECK(Read(*left,0,VertexSemantic::Normal)[2]==-1);
    indices={0,2,1}; options={}; options.winding=MeshSourceWinding::Clockwise;
    auto clockwise=run(options); CHECK(clockwise); Same(*clockwise,*complete);
    options.handedness=MeshSourceHandedness::Left; auto both=run(options); CHECK(both); FrameChecks(*both);
    indices={0,1,2};
    // Oblique normal under shear/nonuniform scaling: inverse transpose matters.
    positions[2]={0,1,1}; normals.fill({0,-1,1}); tangents.fill({1,0,0}); bitangents.fill({0,1,1});
    part.transform={2,1,0,0, 0,3,0,0, 0,0,4,0, 0,0,0,1};
    auto oblique=run(); CHECK(oblique); FrameChecks(*oblique);
    const auto n=Read(*oblique,0,VertexSemantic::Normal); CHECK(Near(n[1],-0.8f) && Near(n[2],0.6f));
    positions[2]={0,1,0}; normals.fill({0,0,2}); tangents.fill({2,0,0}); bitangents.fill({0,3,0});
    part.transform={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::array<MeshImportPart,2> parts{part,part}; parts[0].materialSlot=2; parts[1].materialSlot=0; parts[1].transform[3]=10;
    auto multi=NormalizeImportedMesh(parts,4); CHECK(multi); FrameChecks(*multi);
    CHECK(multi->Submeshes().size()==2 && multi->Submeshes()[0].materialSlot==2 && multi->Submeshes()[1].materialSlot==0);
    CHECK(multi->Submeshes()[1].firstElement==3 && Index(*multi,3)==3 && Index(*multi,5)==5);
    CHECK(Read(*multi,3,VertexSemantic::Position)[0]==10);
    for (int repeat=0;repeat<20;++repeat) { auto r=NormalizeImportedMesh(parts,4); CHECK(r); Same(*multi,*r); }
    std::atomic<bool> workerOK=true;
    std::thread worker([&]{ auto r=NormalizeImportedMesh(parts,4); workerOK=r.has_value(); }); worker.join(); CHECK(workerOK);

    // Deterministic error ordinals, including a late failure after a valid part.
    std::array<std::uint32_t,3> invalid{0,1,99}; parts[1].indices=invalid;
    for (int repeat=0;repeat<20;++repeat) Error(NormalizeImportedMesh(parts,4),Code::IndexOutOfRange,1,2,Field::Index);
    CHECK(multi->VertexCount()==6); // previously completed asset is unaffected
    part.indices=invalid; Error(run(),Code::IndexOutOfRange,0,2,Field::Index); part.indices=indices;
    part.indices={indices.data(),2}; Error(run(),Code::InvalidTriangleRange,0,2,Field::Index); part.indices=indices;
    part.materialSlot=3; Error(run(),Code::MaterialSlotOutOfRange,0,3,Field::Material); part.materialSlot=0;
    part.normals={normals.data(),2}; Error(run(),Code::AttributeCountMismatch,0,2,Field::Normal); part.normals=normals;
    part.bitangents={}; Error(run(),Code::AttributeCountMismatch,0,0,Field::Bitangent); part.bitangents=bitangents;
    normals[1]={0,0,0}; Error(run(),Code::InvalidDirection,0,1,Field::Normal); normals[1]={0,0,2};
    tangents[2]={0,0,0}; Error(run(),Code::InvalidDirection,0,2,Field::Tangent); tangents[2]={2,0,0};
    bitangents[0]={1,0,0}; Error(run(),Code::InvalidDirection,0,0,Field::Bitangent); bitangents[0]={0,3,0};
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    positions[1][0]=nan; Error(run(),Code::NonFiniteAttribute,0,1,Field::Position); positions[1][0]=1;
    normals[2][1]=nan; Error(run(),Code::NonFiniteAttribute,0,2,Field::Normal); normals[2][1]=0;
    uvs[1][0]=nan; Error(run(),Code::NonFiniteAttribute,0,1,Field::UV); uvs[1][0]=1;
    part.transform[0]=0; Error(run(),Code::InvalidTransform,0,0,Field::Transform); part.transform[0]=1;
    part.transform[12]=1; Error(run(),Code::InvalidTransform,0,0,Field::Transform); part.transform[12]=0;
    part.transform[3]=std::numeric_limits<double>::infinity(); Error(run(),Code::InvalidTransform,0,0,Field::Transform); part.transform[3]=0;
    positions[2]=positions[1]; Error(run(),Code::DegenerateTriangle,0,0,Field::Position); positions[2]={0,1,0};
    part.tangents={}; part.bitangents={}; uvs[1]={0,0}; Error(run(),Code::TangentGenerationFailed,0,0,Field::UV); uvs[1]={1,0};
    part.tangents=tangents; part.bitangents=bitangents;
    options={}; options.normals=static_cast<MissingMeshNormals>(99); Error(run(options),Code::InvalidOptions);
    Error(NormalizeImportedMesh({},1),Code::EmptyMesh);
    const HeapCheckpoint normalizationHeap;
    for (int allocation=0;allocation<7;++allocation)
    {
        failureAfter=allocation; auto r=run(); failureAfter=-1;
        CHECK(!r && (r.error().code==Code::AllocationFailed || (r.error().code==Code::MeshValidationFailed && r.error().meshError.code==MeshErrorCode::AllocationFailed)));
    }
    normalizationHeap.Verify();
    generated=run(); CHECK(generated); Same(*complete,*generated);
    // Shared vertices accumulate area-weighted normals; no positional welding.
    const std::array<V,4> smoothPositions{{{0,0,0},{2,0,0},{0,1,0},{0,0,2}}};
    const std::array<std::uint32_t,6> smoothIndices{0,1,2,0,3,1};
    MeshImportPart smoothPart; smoothPart.positions=smoothPositions; smoothPart.indices=smoothIndices;
    auto smooth=NormalizeImportedMesh({&smoothPart,1},1); CHECK(smooth); FrameChecks(*smooth);
    CHECK(Near(Read(*smooth,0,VertexSemantic::Normal)[1],0.89442719f));
    CHECK(Near(Read(*smooth,0,VertexSemantic::Normal)[2],0.44721360f));
    std::puts("[PASS] normalization attributes/materials/transforms/winding/errors/allocation/worker");

    constexpr std::string_view obj="o Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n";
    auto imported=Memory(obj); CHECK(imported); FrameChecks(*imported);
    CHECK(imported->VertexCount()==3 && imported->IndexCount()==3);
    constexpr std::string_view missing="v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    auto noAttributes=Memory(missing); CHECK(noAttributes); FrameChecks(*noAttributes);
    CHECK(Read(*noAttributes,0,VertexSemantic::TexCoord0)==V{});
    Error(ImportMeshFile(nullptr),Code::InvalidPath);
    Error(ImportMeshFile("phase33-file-does-not-exist.obj"),Code::ReadFailed);
    Error(Memory("not a model"),Code::ReadFailed);
    CHECK(!Memory("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\n"));
    auto file=ImportMeshFile(argv[1]); CHECK(file); FrameChecks(*file);
    CHECK(file->Submeshes().size()==2 && file->Submeshes()[0].materialSlot!=file->Submeshes()[1].materialSlot);
    auto transformed=ImportMeshFile(argv[2]); CHECK(transformed); FrameChecks(*transformed);
    CHECK(transformed->Submeshes().size()==2 && transformed->VertexCount()==6);
    CHECK(transformed->Bounds().minimum[0]==0 && transformed->Bounds().maximum[0]==11);
    CHECK(transformed->Bounds().minimum[1]==3 && transformed->Bounds().maximum[1]==6);
    CHECK(transformed->Bounds().minimum[2]==4 && transformed->Bounds().maximum[2]==4);
    for (int repeat=0;repeat<20;++repeat)
    {
        auto a=Memory(obj); auto b=ImportMeshFile(argv[1]); auto c=ImportMeshFile(argv[2]);
        CHECK(a && b && c); Same(*imported,*a); Same(*file,*b); Same(*transformed,*c);
    }
    std::thread importWorker([&]{ auto a=Memory(obj); workerOK=a.has_value(); }); importWorker.join(); CHECK(workerOK);
    const HeapCheckpoint importHeap;
    int failedAllocations=0;
    for (int allocation=0;allocation<32;++allocation)
    {
        failureAfter=allocation; auto r=Memory(obj); failureAfter=-1;
        if (r) break;
        CHECK(r.error().code==Code::AllocationFailed || (r.error().code==Code::MeshValidationFailed && r.error().meshError.code==MeshErrorCode::AllocationFailed));
        ++failedAllocations;
    }
    CHECK(failedAllocations>=10 && failedAllocations<32);
    importHeap.Verify();
    auto recovered=Memory(obj); CHECK(recovered); Same(*imported,*recovered);
    std::printf("[PASS] mesh-import-CPU checks=%d\n",checks);
}
