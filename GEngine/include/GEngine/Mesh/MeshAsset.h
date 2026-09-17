#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <type_traits>

namespace GEngine
{
    // Slots are stable regardless of attribute order or absent optional fields.
    // They describe the new CPU mesh path, not legacy Geometry's insertion order.
    enum class VertexSemantic : std::uint8_t
    {
        Position, TexCoord0, Normal, Tangent, Bitangent, JointIndices,
        JointWeights, EntityId, Color0
    };
    inline constexpr std::size_t MeshAttributeSlots = 9;
    constexpr std::uint8_t AttributeSlot(VertexSemantic semantic) noexcept
    { return static_cast<std::uint8_t>(semantic); }

    enum class VertexScalarFormat : std::uint8_t
    { Float32, Int8, UInt8, Int16, UInt16, Int32, UInt32 };
    enum class VertexInterpretation : std::uint8_t { Floating, Normalized, Integer };
    enum class MeshIndexFormat : std::uint8_t { None, UInt16, UInt32 };
    enum class MeshUpdateIntent : std::uint8_t { Static, Dynamic };

    struct VertexAttribute
    {
        VertexSemantic semantic = VertexSemantic::Position;
        std::uint8_t slot = AttributeSlot(VertexSemantic::Position);
        VertexScalarFormat scalar = VertexScalarFormat::Float32;
        std::uint8_t components = 3;
        VertexInterpretation interpretation = VertexInterpretation::Floating;
        std::size_t offsetBytes = 0;
    };

    template<class T>
    concept InterleavedVertexRecord = std::is_standard_layout_v<T>
        && std::is_trivially_copyable_v<T> && !std::is_volatile_v<T>;

    struct VertexLayout
    {
        std::size_t strideBytes = 0;
        std::span<const VertexAttribute> attributes;

        // Use offsetof(T, member) for each attribute; padding belongs to the record.
        template<InterleavedVertexRecord T>
        static VertexLayout For(std::span<const VertexAttribute> attributes) noexcept
        { return {sizeof(T), attributes}; }
    };

    // Ranges address indices when indexed, otherwise vertices. Indices are absolute
    // vertex ordinals (no base-vertex adjustment). Overlap/gaps and empty ranges are
    // legal; source order and material slots are preserved without regrouping.
    struct SubmeshRange
    {
        std::size_t firstElement = 0;
        std::size_t elementCount = 0;
        std::uint32_t materialSlot = 0;
    };

    struct LocalBounds
    {
        bool empty = true;
        // Double precision avoids overflow for finite Float32 positions. The sphere
        // is centered on the AABB midpoint and conservatively covers every vertex.
        std::array<double, 3> minimum{};
        std::array<double, 3> maximum{};
        std::array<double, 3> sphereCenter{};
        double sphereRadius = 0;
    };

    enum class MeshErrorCode : std::uint8_t
    {
        InvalidStride, InvalidAttributeCount, UnsupportedAttribute,
        DuplicateSemantic, DuplicateSlot, SlotMismatch, AttributeOutOfRecord,
        OverlappingAttributes, MissingPosition, SizeOverflow, VertexPayloadMismatch,
        InvalidIndexFormat, IndexPayloadMismatch, IndexOutOfRange,
        MissingSubmeshes, SubmeshOutOfRange, MaterialSlotOutOfRange,
        InvalidUpdateIntent, NonFinitePosition, AllocationFailed
    };
    struct MeshError
    {
        MeshErrorCode code;
        // Offending attribute/index/submesh/vertex ordinal when applicable;
        // requested count/size for size or allocation failures.
        std::size_t element = 0;
    };

    struct MeshSourceData
    {
        VertexLayout layout;
        // Exactly vertexCount records from byte zero, in host byte order. Spans
        // must reference valid storage for the duration of Create; it copies them.
        std::span<const std::byte> vertices;
        std::size_t vertexCount = 0;
        MeshIndexFormat indexFormat = MeshIndexFormat::None;
        std::span<const std::byte> indices;
        std::size_t indexCount = 0;
        std::span<const SubmeshRange> submeshes;
        std::uint32_t materialSlotCount = 1;
        MeshUpdateIntent updateIntent = MeshUpdateIntent::Static;

        template<InterleavedVertexRecord T>
        static MeshSourceData FromVertices(std::span<const T> records,
            std::span<const VertexAttribute> attributes) noexcept
        {
            MeshSourceData source;
            source.layout = VertexLayout::For<T>(attributes);
            source.vertices = std::as_bytes(records);
            source.vertexCount = records.size();
            return source;
        }
    };

    // CPU only, move-only and immutable after construction. Dynamic is upload
    // intent, not permission to mutate these bytes. No resource identity is minted.
    // Views borrow this object and expire on its move, assignment or destruction.
    class MeshAsset final
    {
    public:
        MeshAsset(const MeshAsset&) = delete;
        MeshAsset& operator=(const MeshAsset&) = delete;
        MeshAsset(MeshAsset&& other) noexcept;
        MeshAsset& operator=(MeshAsset&& other) noexcept;
        ~MeshAsset() = default;

        // An empty source still needs a valid position layout; it has empty bounds.
        // Nonempty draw data requires explicit submeshes. No implicit material/range
        // is invented. Moved-from objects expose empty views and zero stride.
        static std::expected<MeshAsset, MeshError> Create(const MeshSourceData& source) noexcept;

        VertexLayout Layout() const noexcept
        { return {m_Stride, {m_Attributes.data(), m_AttributeCount}}; }
        std::span<const std::byte> Vertices() const noexcept
        { return {m_Vertices.get(), m_VertexBytes}; }
        std::span<const std::byte> Indices() const noexcept
        { return {m_Indices.get(), m_IndexBytes}; }
        std::span<const SubmeshRange> Submeshes() const noexcept
        { return {m_Submeshes.get(), m_SubmeshCount}; }
        std::size_t VertexCount() const noexcept { return m_VertexCount; }
        std::size_t IndexCount() const noexcept { return m_IndexCount; }
        MeshIndexFormat IndexFormat() const noexcept { return m_IndexFormat; }
        std::uint32_t MaterialSlotCount() const noexcept { return m_MaterialSlotCount; }
        MeshUpdateIntent UpdateIntent() const noexcept { return m_UpdateIntent; }
        const LocalBounds& Bounds() const noexcept { return m_Bounds; }

    private:
        MeshAsset() = default;
        void Swap(MeshAsset& other) noexcept;
        std::array<VertexAttribute, MeshAttributeSlots> m_Attributes{};
        std::size_t m_AttributeCount = 0, m_Stride = 0;
        std::unique_ptr<std::byte[]> m_Vertices, m_Indices;
        std::unique_ptr<SubmeshRange[]> m_Submeshes;
        std::size_t m_VertexBytes = 0, m_IndexBytes = 0, m_SubmeshCount = 0;
        std::size_t m_VertexCount = 0, m_IndexCount = 0;
        MeshIndexFormat m_IndexFormat = MeshIndexFormat::None;
        std::uint32_t m_MaterialSlotCount = 0;
        MeshUpdateIntent m_UpdateIntent = MeshUpdateIntent::Static;
        LocalBounds m_Bounds;
    };
}
