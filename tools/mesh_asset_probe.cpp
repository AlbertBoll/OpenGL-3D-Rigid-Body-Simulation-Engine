#include "Mesh/MeshAsset.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <print>
#include <thread>
#include <utility>

#if defined(GLAD_GL_H_) || defined(GL_VERSION_1_0) || defined(SDL_MAJOR_VERSION)
#error Native backend leaked into the mesh consumer
#endif

// Inject failure at each production nothrow array allocation, without changing
// MeshAsset or allocating bookkeeping. Deletion observes transactional cleanup.
namespace
{
    int failAllocation = -1;
    void* tracked[32]{};
    int liveArrays = 0;
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{
    if (failAllocation == 0) return nullptr;
    if (failAllocation > 0) --failAllocation;
    void* p = ::operator new(bytes, std::nothrow);
    if (p)
    {
        for (auto& slot : tracked) if (!slot) { slot = p; ++liveArrays; return p; }
        std::abort();
    }
    return p;
}
void operator delete[](void* p) noexcept
{
    if (p) for (auto& slot : tracked) if (slot == p) { slot = nullptr; --liveArrays; break; }
    ::operator delete(p);
}
void operator delete[](void* p, std::size_t) noexcept { ::operator delete[](p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete[](p); }

namespace
{
    using namespace GEngine;
    using S = VertexSemantic;
    using F = VertexScalarFormat;
    using I = VertexInterpretation;
    using E = MeshErrorCode;
    int checks = 0;
    void Check(bool condition, const char* expression, int line)
    {
        ++checks;
        if (!condition) { std::println("[FAIL] line {}: {}", line, expression); std::exit(1); }
    }
#define CHECK(...) Check(static_cast<bool>(__VA_ARGS__), #__VA_ARGS__, __LINE__)

    struct P { float position[3]; };
    struct PN { float position[3], normal[3]; };
    struct PNUV { float position[3], normal[3], uv[2]; };
#pragma warning(push)
#pragma warning(disable: 4324) // Padding is the subject of this aligned-record fixture.
    struct alignas(32) Padded
    {
        std::uint8_t prefix;
        alignas(16) float position[3];
        alignas(16) float normal[3];
        float uv[2];
    };
#pragma warning(pop)
    struct NonTrivial { ~NonTrivial() {} float position[3]; };
    struct NonStandard { virtual ~NonStandard() = default; float position[3]; };
    static_assert(InterleavedVertexRecord<Padded> && !InterleavedVertexRecord<NonTrivial>);
    static_assert(!InterleavedVertexRecord<NonStandard> && !InterleavedVertexRecord<volatile P>);
    static_assert(!std::is_copy_constructible_v<MeshAsset> && !std::is_copy_assignable_v<MeshAsset>);
    static_assert(std::is_nothrow_move_constructible_v<MeshAsset> && std::is_nothrow_move_assignable_v<MeshAsset>);
    static_assert(std::is_nothrow_destructible_v<MeshAsset>);
    static_assert(offsetof(Padded, position) > 0 && sizeof(Padded) > sizeof(PNUV));

    VertexAttribute Attribute(S semantic, std::size_t offset, std::uint8_t components,
        F scalar = F::Float32, I interpretation = I::Floating)
    { return {semantic, AttributeSlot(semantic), scalar, components, interpretation, offset}; }
    const std::array pAttributes{Attribute(S::Position, offsetof(P, position), 3)};
    const std::array pnAttributes{Attribute(S::Position, offsetof(PN, position), 3),
        Attribute(S::Normal, offsetof(PN, normal), 3)};
    const std::array pnuvAttributes{Attribute(S::Position, offsetof(PNUV, position), 3),
        Attribute(S::Normal, offsetof(PNUV, normal), 3), Attribute(S::TexCoord0, offsetof(PNUV, uv), 2)};
    const std::array paddedAttributes{Attribute(S::TexCoord0, offsetof(Padded, uv), 2),
        Attribute(S::Position, offsetof(Padded, position), 3), Attribute(S::Normal, offsetof(Padded, normal), 3)};

    template<InterleavedVertexRecord T, std::size_t N>
    MeshSourceData Source(const std::array<T, N>& vertices, std::span<const VertexAttribute> attributes,
        std::span<const SubmeshRange> submeshes)
    {
        auto source = MeshSourceData::FromVertices<T>(vertices, attributes);
        source.submeshes = submeshes;
        return source;
    }
    template<class T>
    T Read(const MeshAsset& mesh, std::size_t vertex, S semantic, std::size_t component = 0)
    {
        for (const auto& a : mesh.Layout().attributes)
            if (a.semantic == semantic)
            {
                T value;
                std::memcpy(&value, mesh.Vertices().data() + vertex * mesh.Layout().strideBytes
                    + a.offsetBytes + component * sizeof(T), sizeof(T));
                return value;
            }
        std::abort();
    }
    void Reject(const MeshSourceData& source, E code, std::size_t element = 0)
    {
        const auto result = MeshAsset::Create(source);
        CHECK(!result);
        CHECK(result.error().code == code);
        CHECK(result.error().element == element);
    }

    void LayoutsAndBounds()
    {
        const std::array ranges{SubmeshRange{0, 1, 1}, SubmeshRange{1, 1, 0}, SubmeshRange{2, 0, 1}};
        const std::array positions{P{{-2, 1, 4}}, P{{6, -3, 8}}};
        auto source = Source(positions, pAttributes, ranges);
        source.materialSlotCount = 2;
        source.updateIntent = MeshUpdateIntent::Dynamic;
        auto mesh = MeshAsset::Create(source);
        CHECK(mesh && mesh->VertexCount() == 2 && mesh->IndexCount() == 0);
        CHECK(mesh->Layout().strideBytes == sizeof(P) && mesh->Layout().attributes.size() == 1);
        CHECK(mesh->IndexFormat() == MeshIndexFormat::None && mesh->Indices().empty());
        CHECK(mesh->UpdateIntent() == MeshUpdateIntent::Dynamic && mesh->MaterialSlotCount() == 2);
        CHECK(mesh->Submeshes().size() == 3 && mesh->Submeshes()[0].materialSlot == 1);
        CHECK(mesh->Submeshes()[1].firstElement == 1 && mesh->Submeshes()[2].elementCount == 0);
        const auto& b = mesh->Bounds();
        CHECK(!b.empty && b.minimum == std::array<double, 3>{-2, -3, 4});
        CHECK(b.maximum == std::array<double, 3>{6, 1, 8});
        CHECK(b.sphereCenter == std::array<double, 3>{2, -1, 6});
        CHECK(b.sphereRadius >= std::sqrt(24.0) && b.sphereRadius < std::sqrt(24.0) + 1e-12);
        for (std::size_t i = 0; i < 2; ++i) for (std::size_t c = 0; c < 3; ++c)
            CHECK(Read<float>(*mesh, i, S::Position, c) == positions[i].position[c]);

        const std::array oneRange{SubmeshRange{0, 2, 0}};
        const std::array normals{PN{{1, 2, 3}, {4, 5, 6}}, PN{{7, 8, 9}, {10, 11, 12}}};
        auto pn = MeshAsset::Create(Source(normals, pnAttributes, oneRange));
        CHECK(pn && pn->Layout().strideBytes == sizeof(PN) && pn->Layout().attributes.size() == 2);
        const std::array uv{PNUV{{1, 2, 3}, {4, 5, 6}, {0.1f, 0.2f}},
            PNUV{{7, 8, 9}, {10, 11, 12}, {0.7f, 0.8f}}};
        auto pnuv = MeshAsset::Create(Source(uv, pnuvAttributes, oneRange));
        CHECK(pnuv && pnuv->Layout().strideBytes == sizeof(PNUV));
        std::array<Padded, 2> padded{};
        for (std::size_t i = 0; i < 2; ++i)
        {
            for (std::size_t c = 0; c < 3; ++c)
            { padded[i].position[c] = uv[i].position[c]; padded[i].normal[c] = uv[i].normal[c]; }
            for (std::size_t c = 0; c < 2; ++c) padded[i].uv[c] = uv[i].uv[c];
        }
        auto aligned = MeshAsset::Create(Source(padded, paddedAttributes, oneRange));
        CHECK(aligned && aligned->Layout().strideBytes == sizeof(Padded));
        CHECK(aligned->Layout().attributes[0].slot == 1 && aligned->Layout().attributes[1].slot == 0);
        for (std::size_t i = 0; i < 2; ++i)
        {
            for (std::size_t c = 0; c < 3; ++c)
            {
                CHECK(Read<float>(*pn, i, S::Position, c) == normals[i].position[c]);
                CHECK(Read<float>(*pn, i, S::Normal, c) == normals[i].normal[c]);
                CHECK(Read<float>(*pnuv, i, S::Position, c) == uv[i].position[c]);
                CHECK(Read<float>(*pnuv, i, S::Normal, c) == uv[i].normal[c]);
                CHECK(Read<float>(*aligned, i, S::Position, c) == padded[i].position[c]);
                CHECK(Read<float>(*aligned, i, S::Normal, c) == padded[i].normal[c]);
            }
            for (std::size_t c = 0; c < 2; ++c)
            {
                CHECK(Read<float>(*pnuv, i, S::TexCoord0, c) == uv[i].uv[c]);
                CHECK(Read<float>(*aligned, i, S::TexCoord0, c) == padded[i].uv[c]);
            }
        }
        std::array extreme{P{{-(std::numeric_limits<float>::max)(), 0, 0}},
            P{{(std::numeric_limits<float>::max)(), 0, 0}}};
        auto wide = MeshAsset::Create(Source(extreme, pAttributes, oneRange));
        CHECK(wide && std::isfinite(wide->Bounds().sphereRadius));
        CHECK(wide->Bounds().sphereRadius >= (std::numeric_limits<float>::max)());
        extreme[1] = extreme[0];
        auto point = MeshAsset::Create(Source(extreme, pAttributes, oneRange));
        CHECK(point && point->Bounds().sphereRadius == 0);
        extreme[1].position[0] = std::numeric_limits<float>::infinity();
        Reject(Source(extreme, pAttributes, oneRange), E::NonFinitePosition, 1);
        extreme[1].position[0] = std::numeric_limits<float>::quiet_NaN();
        Reject(Source(extreme, pAttributes, oneRange), E::NonFinitePosition, 1);
    }

    void AdditionalAttributes()
    {
        struct Full
        { float p[3], uv[2], n[3], tangent[4], bitangent[3]; std::int32_t joints[4]; float weights[4]; std::int32_t id; std::uint8_t color[4]; };
        const std::array attrs{Attribute(S::Position, offsetof(Full, p), 3), Attribute(S::TexCoord0, offsetof(Full, uv), 2),
            Attribute(S::Normal, offsetof(Full, n), 3), Attribute(S::Tangent, offsetof(Full, tangent), 4),
            Attribute(S::Bitangent, offsetof(Full, bitangent), 3), Attribute(S::JointIndices, offsetof(Full, joints), 4, F::Int32, I::Integer),
            Attribute(S::JointWeights, offsetof(Full, weights), 4), Attribute(S::EntityId, offsetof(Full, id), 1, F::Int32, I::Integer),
            Attribute(S::Color0, offsetof(Full, color), 4, F::UInt8, I::Normalized)};
        const std::array data{Full{{1, 2, 3}, {4, 5}, {6, 7, 8}, {9, 10, 11, -1}, {12, 13, 14},
            {-1, 2, 3, 4}, {0.1f, 0.2f, 0.3f, 0.4f}, -42, {0, 64, 128, 255}}};
        const std::array ranges{SubmeshRange{0, 1, 0}};
        auto mesh = MeshAsset::Create(Source(data, attrs, ranges));
        CHECK(mesh && mesh->Layout().attributes.size() == MeshAttributeSlots);
        CHECK(Read<std::int32_t>(*mesh, 0, S::JointIndices) == -1);
        CHECK(Read<std::int32_t>(*mesh, 0, S::EntityId) == -42);
        CHECK(Read<std::uint8_t>(*mesh, 0, S::Color0, 3) == 255);
        CHECK(Read<float>(*mesh, 0, S::Tangent, 3) == -1);
        CHECK(Read<float>(*mesh, 0, S::JointWeights, 3) == 0.4f);
        // Smaller integer storage and normalized weights remain explicit metadata.
        for (auto scalar : {F::Int8, F::UInt8, F::Int16, F::UInt16, F::UInt32})
        {
            auto changed = attrs; changed[5].scalar = scalar;
            CHECK(MeshAsset::Create(Source(data, changed, ranges)));
        }
        for (auto scalar : {F::UInt8, F::UInt16})
        {
            auto changed = attrs;
            changed[6].scalar = scalar; changed[6].interpretation = I::Normalized;
            CHECK(MeshAsset::Create(Source(data, changed, ranges)));
        }
    }

    void InvalidDescriptions()
    {
        const std::array data{PN{{1, 2, 3}, {4, 5, 6}}, PN{{7, 8, 9}, {10, 11, 12}}};
        std::array ranges{SubmeshRange{0, 2, 0}};
        auto attrs = pnAttributes;
        const auto original = Source(data, attrs, ranges);
        auto s = original; s.layout.strideBytes = 0; Reject(s, E::InvalidStride);
        s = original; s.layout.attributes = {}; Reject(s, E::InvalidAttributeCount);
        std::array<VertexAttribute, 10> many{};
        s = original; s.layout.attributes = many; Reject(s, E::InvalidAttributeCount, 10);
        s = original; s.layout.strideBytes = sizeof(PN) - 1; Reject(s, E::AttributeOutOfRecord, 1);
        attrs[1].offsetBytes = (std::numeric_limits<std::size_t>::max)(); Reject(original, E::AttributeOutOfRecord, 1);
        attrs = pnAttributes; attrs[1].semantic = S::Position; Reject(original, E::DuplicateSemantic, 1);
        attrs = pnAttributes; attrs[1].slot = 0; Reject(original, E::DuplicateSlot, 1);
        attrs = pnAttributes; attrs[1].slot = 3; Reject(original, E::SlotMismatch, 1);
        attrs = pnAttributes; attrs[1].offsetBytes = 4; Reject(original, E::OverlappingAttributes, 1);
        attrs = pnAttributes; attrs[0].semantic = S::Tangent; attrs[0].slot = 3; Reject(original, E::MissingPosition);
        attrs = pnAttributes; attrs[1].components = 0; Reject(original, E::UnsupportedAttribute, 1);
        attrs = pnAttributes; attrs[1].components = 255; Reject(original, E::UnsupportedAttribute, 1);
        attrs = pnAttributes; attrs[1].interpretation = I::Integer; Reject(original, E::UnsupportedAttribute, 1);
        attrs = pnAttributes; attrs[1].scalar = static_cast<F>(255); Reject(original, E::UnsupportedAttribute, 1);
        attrs = pnAttributes; attrs[1].semantic = static_cast<S>(255); Reject(original, E::UnsupportedAttribute, 1);
        attrs = pnAttributes; attrs[1].slot = 255; Reject(original, E::UnsupportedAttribute, 1);
        attrs = pnAttributes;
        s = original; s.vertexCount = 1; Reject(s, E::VertexPayloadMismatch, sizeof(data));
        s = original; s.vertices = s.vertices.first(s.vertices.size() - 1); Reject(s, E::VertexPayloadMismatch, sizeof(data) - 1);
        s = original; s.vertexCount = (std::numeric_limits<std::size_t>::max)(); Reject(s, E::SizeOverflow);
        s = original; s.layout.strideBytes = (std::numeric_limits<std::size_t>::max)(); Reject(s, E::SizeOverflow);
        s = original; s.submeshes = {}; Reject(s, E::MissingSubmeshes);
        ranges[0].firstElement = (std::numeric_limits<std::size_t>::max)(); Reject(original, E::SubmeshOutOfRange);
        ranges[0] = {1, (std::numeric_limits<std::size_t>::max)(), 0}; Reject(original, E::SubmeshOutOfRange);
        ranges[0] = {0, 3, 0}; Reject(original, E::SubmeshOutOfRange);
        ranges[0] = {0, 2, 1}; Reject(original, E::MaterialSlotOutOfRange);
        ranges[0] = {0, 2, 0};
        s = original; s.materialSlotCount = 0; Reject(s, E::MaterialSlotOutOfRange);
        s = original; s.updateIntent = static_cast<MeshUpdateIntent>(255); Reject(s, E::InvalidUpdateIntent);
        s = original; s.indexFormat = static_cast<MeshIndexFormat>(255); Reject(s, E::InvalidIndexFormat);
        s = original; s.indexCount = 1; Reject(s, E::IndexPayloadMismatch);
        s = original; s.indices = s.vertices.first(2); Reject(s, E::IndexPayloadMismatch);
        s = original; s.indexFormat = MeshIndexFormat::UInt32; s.indexCount = (std::numeric_limits<std::size_t>::max)();
        Reject(s, E::SizeOverflow);
    }

    void IndicesOwnershipAndFailures()
    {
        auto owned = []
        {
            std::array data{P{{1, 2, 3}}, P{{4, 5, 6}}, P{{-100, 20, 30}}};
            auto attrs = pAttributes;
            std::array ranges{SubmeshRange{1, 2, 1}, SubmeshRange{0, 1, 0}};
            const std::array<std::uint16_t, 3> indices{1, 0, 1};
            auto s = Source(data, attrs, ranges);
            s.indexFormat = MeshIndexFormat::UInt16; s.indices = std::as_bytes(std::span(indices)); s.indexCount = 3;
            s.materialSlotCount = 2;
            auto result = MeshAsset::Create(s);
            CHECK(result && result->IndexCount() == 3 && result->IndexFormat() == MeshIndexFormat::UInt16);
            CHECK(std::memcmp(result->Indices().data(), indices.data(), sizeof(indices)) == 0);
            data[0].position[0] = 99; attrs[0].offsetBytes = 999; ranges[0].materialSlot = 99;
            return result;
        }();
        CHECK(owned && Read<float>(*owned, 0, S::Position) == 1);
        CHECK(owned->Layout().attributes[0].offsetBytes == 0 && owned->Submeshes()[0].materialSlot == 1);
        CHECK(owned->Bounds().minimum[0] == -100); // Unreferenced vertices also contribute.
        const std::array data{P{{1, 2, 3}}, P{{4, 5, 6}}};
        const std::array ranges{SubmeshRange{0, 3, 0}};
        std::array<std::uint32_t, 3> indices{1, 0, 1};
        auto s = Source(data, pAttributes, ranges);
        s.indexFormat = MeshIndexFormat::UInt32; s.indices = std::as_bytes(std::span(indices)); s.indexCount = 3;
        auto mesh = MeshAsset::Create(s);
        CHECK(mesh && mesh->IndexFormat() == MeshIndexFormat::UInt32);
        CHECK(std::memcmp(mesh->Indices().data(), indices.data(), sizeof(indices)) == 0);
        s.indexCount = 2; Reject(s, E::IndexPayloadMismatch, sizeof(indices)); s.indexCount = 3;
        indices[2] = 2; Reject(s, E::IndexOutOfRange, 2); indices[2] = 1;
        const std::array<std::uint16_t, 3> shortIndices{0, 1, 65535};
        auto shortSource = s; shortSource.indexFormat = MeshIndexFormat::UInt16;
        shortSource.indices = std::as_bytes(std::span(shortIndices)); Reject(shortSource, E::IndexOutOfRange, 2);
        // Byte views need not be aligned to the scalar type.
        std::array<std::byte, sizeof(indices) + 1> unaligned{};
        std::memcpy(unaligned.data() + 1, indices.data(), sizeof(indices));
        s.indices = std::span<const std::byte>(unaligned).subspan(1);
        CHECK(MeshAsset::Create(s));
        const int before = liveArrays;
        for (int failure = 0; failure < 3; ++failure)
        {
            failAllocation = failure;
            const auto failed = MeshAsset::Create(s);
            failAllocation = -1;
            CHECK(!failed && failed.error().code == E::AllocationFailed);
            CHECK(liveArrays == before);
        }
        CHECK(MeshAsset::Create(s)); // Failure did not mutate source or future construction.
        MeshAsset moved(std::move(*owned));
        CHECK(owned->Vertices().empty() && owned->Submeshes().empty() && owned->Bounds().empty);
        CHECK(owned->VertexCount() == 0 && owned->Layout().strideBytes == 0 && owned->Indices().empty());
        *mesh = std::move(moved); // Releases an occupied destination.
        CHECK(moved.VertexCount() == 0 && mesh->VertexCount() == 3 && mesh->IndexFormat() == MeshIndexFormat::UInt16);
        auto* same = &*mesh; *mesh = std::move(*same);
        CHECK(mesh->VertexCount() == 3 && Read<float>(*mesh, 0, S::Position) == 1);
    }

    void EmptyAndWorker()
    {
        const std::array<P, 0> empty{};
        auto s = Source(empty, pAttributes, {}); s.materialSlotCount = 0;
        auto result = MeshAsset::Create(s);
        CHECK(result && result->Bounds().empty && result->Bounds().sphereRadius == 0);
        CHECK(result->Vertices().empty() && result->Submeshes().empty() && result->Layout().strideBytes == sizeof(P));
        for (auto format : {MeshIndexFormat::UInt16, MeshIndexFormat::UInt32})
        { s.indexFormat = format; CHECK(MeshAsset::Create(s)); }
        std::thread worker([]
        {
            const std::array data{P{{-1, 2, 3}}};
            const std::array ranges{SubmeshRange{0, 1, 0}};
            auto result = MeshAsset::Create(Source(data, pAttributes, ranges));
            CHECK(result && !result->Bounds().empty && result->Bounds().sphereRadius == 0);
        });
        worker.join();
    }
}

int main()
{
    LayoutsAndBounds(); CHECK(liveArrays == 0);
    AdditionalAttributes(); CHECK(liveArrays == 0);
    InvalidDescriptions(); CHECK(liveArrays == 0);
    IndicesOwnershipAndFailures(); CHECK(liveArrays == 0);
    EmptyAndWorker(); CHECK(liveArrays == 0);
    std::println("[PASS] mesh-asset-CPU checks={} live-arrays={}", checks, liveArrays);
}
