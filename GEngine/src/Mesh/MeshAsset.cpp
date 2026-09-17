#include "gepch.h"
#include "Mesh/MeshAsset.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace GEngine
{
    namespace
    {
        using Code = MeshErrorCode;
        std::unexpected<MeshError> Error(Code code, std::size_t element = 0) noexcept
        { return std::unexpected(MeshError{code, element}); }

        bool Product(std::size_t count, std::size_t width, std::size_t& bytes) noexcept
        {
            if (width && count > (std::numeric_limits<std::size_t>::max)() / width) return false;
            bytes = count * width;
            // Byte spans/allocations must also be representable as pointer distances.
            return bytes <= static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)());
        }

        std::size_t ScalarBytes(VertexScalarFormat format) noexcept
        {
            switch (format)
            {
            case VertexScalarFormat::Int8: case VertexScalarFormat::UInt8: return 1;
            case VertexScalarFormat::Int16: case VertexScalarFormat::UInt16: return 2;
            case VertexScalarFormat::Float32: case VertexScalarFormat::Int32:
            case VertexScalarFormat::UInt32: return 4;
            default: return 0;
            }
        }

        bool Supported(const VertexAttribute& a) noexcept
        {
            using S = VertexSemantic;
            using F = VertexScalarFormat;
            using I = VertexInterpretation;
            const bool floating = a.scalar == F::Float32 && a.interpretation == I::Floating;
            const bool integer = a.scalar != F::Float32 && a.interpretation == I::Integer;
            const bool unorm = (a.scalar == F::UInt8 || a.scalar == F::UInt16)
                && a.interpretation == I::Normalized;
            switch (a.semantic)
            {
            case S::Position: case S::Normal: case S::Bitangent:
                return floating && a.components == 3;
            case S::TexCoord0: return floating && a.components == 2;
            case S::Tangent: return floating && (a.components == 3 || a.components == 4);
            case S::JointIndices: return integer && a.components == 4;
            case S::JointWeights: return (floating || unorm) && a.components == 4;
            case S::EntityId: return integer && a.scalar == F::Int32 && a.components == 1;
            case S::Color0: return (floating || unorm) && (a.components == 3 || a.components == 4);
            default: return false;
            }
        }

        std::expected<std::size_t, MeshError> ValidateLayout(VertexLayout layout) noexcept
        {
            if (!layout.strideBytes) return Error(Code::InvalidStride);
            if (layout.attributes.empty() || layout.attributes.size() > MeshAttributeSlots)
                return Error(Code::InvalidAttributeCount, layout.attributes.size());
            std::array<bool, MeshAttributeSlots> semantics{}, slots{};
            std::size_t positionOffset = 0;
            for (std::size_t i = 0; i < layout.attributes.size(); ++i)
            {
                const auto& a = layout.attributes[i];
                const auto semantic = AttributeSlot(a.semantic);
                const auto scalarBytes = ScalarBytes(a.scalar);
                if (semantic >= MeshAttributeSlots || a.slot >= MeshAttributeSlots || !scalarBytes || !Supported(a))
                    return Error(Code::UnsupportedAttribute, i);
                if (semantics[semantic]) return Error(Code::DuplicateSemantic, i);
                if (slots[a.slot]) return Error(Code::DuplicateSlot, i);
                if (a.slot != semantic) return Error(Code::SlotMismatch, i);
                semantics[semantic] = slots[a.slot] = true;
                std::size_t extent = 0;
                if (!Product(a.components, scalarBytes, extent)) return Error(Code::SizeOverflow, i);
                if (a.offsetBytes > layout.strideBytes || extent > layout.strideBytes - a.offsetBytes)
                    return Error(Code::AttributeOutOfRecord, i);
                // Each prior extent has already been checked, so these sums are safe.
                for (std::size_t j = 0; j < i; ++j)
                {
                    const auto& b = layout.attributes[j];
                    const auto end = b.offsetBytes + b.components * ScalarBytes(b.scalar);
                    if (a.offsetBytes < end && b.offsetBytes < a.offsetBytes + extent)
                        return Error(Code::OverlappingAttributes, i);
                }
                if (a.semantic == VertexSemantic::Position) positionOffset = a.offsetBytes;
            }
            if (!semantics[AttributeSlot(VertexSemantic::Position)]) return Error(Code::MissingPosition);
            return positionOffset;
        }

        std::array<double, 3> Position(const MeshSourceData& source, std::size_t offset, std::size_t i) noexcept
        {
            std::array<float, 3> value;
            std::memcpy(value.data(), source.vertices.data() + i * source.layout.strideBytes + offset, sizeof(value));
            return {value[0], value[1], value[2]};
        }

        std::expected<LocalBounds, MeshError> CalculateBounds(const MeshSourceData& source, std::size_t offset) noexcept
        {
            LocalBounds bounds;
            if (!source.vertexCount) return bounds;
            for (std::size_t i = 0; i < source.vertexCount; ++i)
            {
                const auto p = Position(source, offset, i);
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    if (!std::isfinite(p[axis])) return Error(Code::NonFinitePosition, i);
                    if (!i) bounds.minimum[axis] = bounds.maximum[axis] = p[axis];
                    else
                    {
                        bounds.minimum[axis] = (std::min)(bounds.minimum[axis], p[axis]);
                        bounds.maximum[axis] = (std::max)(bounds.maximum[axis], p[axis]);
                    }
                }
            }
            bounds.empty = false;
            for (std::size_t axis = 0; axis < 3; ++axis)
                bounds.sphereCenter[axis] = (bounds.minimum[axis] + bounds.maximum[axis]) * 0.5;
            for (std::size_t i = 0; i < source.vertexCount; ++i)
            {
                const auto p = Position(source, offset, i);
                const auto radius = std::hypot(p[0] - bounds.sphereCenter[0],
                    p[1] - bounds.sphereCenter[1], p[2] - bounds.sphereCenter[2]);
                bounds.sphereRadius = (std::max)(bounds.sphereRadius, radius);
            }
            if (bounds.sphereRadius > 0)
                bounds.sphereRadius = std::nextafter(bounds.sphereRadius, std::numeric_limits<double>::infinity());
            return bounds;
        }
    }

    std::expected<MeshAsset, MeshError> MeshAsset::Create(const MeshSourceData& source) noexcept
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        const auto position = ValidateLayout(source.layout);
        if (!position) return std::unexpected(position.error());
        if (source.updateIntent != MeshUpdateIntent::Static && source.updateIntent != MeshUpdateIntent::Dynamic)
            return Error(Code::InvalidUpdateIntent);
        std::size_t vertexBytes = 0, indexBytes = 0, submeshBytes = 0;
        if (!Product(source.vertexCount, source.layout.strideBytes, vertexBytes)
            || !Product(source.submeshes.size(), sizeof(SubmeshRange), submeshBytes))
            return Error(Code::SizeOverflow);
        if (vertexBytes != source.vertices.size()) return Error(Code::VertexPayloadMismatch, source.vertices.size());
        std::size_t indexWidth = 0;
        switch (source.indexFormat)
        {
        case MeshIndexFormat::None:
            if (source.indexCount || !source.indices.empty()) return Error(Code::IndexPayloadMismatch);
            break;
        case MeshIndexFormat::UInt16: indexWidth = 2; break;
        case MeshIndexFormat::UInt32: indexWidth = 4; break;
        default: return Error(Code::InvalidIndexFormat);
        }
        if (!Product(source.indexCount, indexWidth, indexBytes)) return Error(Code::SizeOverflow);
        if (indexBytes != source.indices.size()) return Error(Code::IndexPayloadMismatch, source.indices.size());
        for (std::size_t i = 0; i < source.indexCount; ++i)
        {
            std::uint32_t value = 0;
            if (source.indexFormat == MeshIndexFormat::UInt16)
            {
                std::uint16_t small;
                std::memcpy(&small, source.indices.data() + i * indexWidth, sizeof(small));
                value = small;
            }
            else std::memcpy(&value, source.indices.data() + i * indexWidth, sizeof(value));
            if (value >= source.vertexCount) return Error(Code::IndexOutOfRange, i);
        }
        const auto drawCount = source.indexFormat == MeshIndexFormat::None ? source.vertexCount : source.indexCount;
        if (drawCount && source.submeshes.empty()) return Error(Code::MissingSubmeshes);
        for (std::size_t i = 0; i < source.submeshes.size(); ++i)
        {
            const auto& range = source.submeshes[i];
            if (range.firstElement > drawCount || range.elementCount > drawCount - range.firstElement)
                return Error(Code::SubmeshOutOfRange, i);
            if (range.materialSlot >= source.materialSlotCount) return Error(Code::MaterialSlotOutOfRange, i);
        }
        const auto bounds = CalculateBounds(source, *position);
        if (!bounds) return std::unexpected(bounds.error());

        MeshAsset result;
        // All sizes and contents are validated before allocation. Partial owners
        // release themselves on every failure without exception control flow.
        if (vertexBytes)
        {
            result.m_Vertices.reset(new (std::nothrow) std::byte[vertexBytes]);
            if (!result.m_Vertices) return Error(Code::AllocationFailed, vertexBytes);
            std::memcpy(result.m_Vertices.get(), source.vertices.data(), vertexBytes);
        }
        if (indexBytes)
        {
            result.m_Indices.reset(new (std::nothrow) std::byte[indexBytes]);
            if (!result.m_Indices) return Error(Code::AllocationFailed, indexBytes);
            std::memcpy(result.m_Indices.get(), source.indices.data(), indexBytes);
        }
        if (submeshBytes)
        {
            result.m_Submeshes.reset(new (std::nothrow) SubmeshRange[source.submeshes.size()]);
            if (!result.m_Submeshes) return Error(Code::AllocationFailed, submeshBytes);
            std::copy(source.submeshes.begin(), source.submeshes.end(), result.m_Submeshes.get());
        }
        std::copy(source.layout.attributes.begin(), source.layout.attributes.end(), result.m_Attributes.begin());
        result.m_AttributeCount = source.layout.attributes.size();
        result.m_Stride = source.layout.strideBytes;
        result.m_VertexBytes = vertexBytes;
        result.m_IndexBytes = indexBytes;
        result.m_SubmeshCount = source.submeshes.size();
        result.m_VertexCount = source.vertexCount;
        result.m_IndexCount = source.indexCount;
        result.m_IndexFormat = source.indexFormat;
        result.m_MaterialSlotCount = source.materialSlotCount;
        result.m_UpdateIntent = source.updateIntent;
        result.m_Bounds = *bounds;
        return result;
    }

    void MeshAsset::Swap(MeshAsset& other) noexcept
    {
        using std::swap;
        swap(m_Attributes, other.m_Attributes);
        swap(m_AttributeCount, other.m_AttributeCount);
        swap(m_Stride, other.m_Stride);
        swap(m_Vertices, other.m_Vertices);
        swap(m_Indices, other.m_Indices);
        swap(m_Submeshes, other.m_Submeshes);
        swap(m_VertexBytes, other.m_VertexBytes);
        swap(m_IndexBytes, other.m_IndexBytes);
        swap(m_SubmeshCount, other.m_SubmeshCount);
        swap(m_VertexCount, other.m_VertexCount);
        swap(m_IndexCount, other.m_IndexCount);
        swap(m_IndexFormat, other.m_IndexFormat);
        swap(m_MaterialSlotCount, other.m_MaterialSlotCount);
        swap(m_UpdateIntent, other.m_UpdateIntent);
        swap(m_Bounds, other.m_Bounds);
    }
    MeshAsset::MeshAsset(MeshAsset&& other) noexcept { Swap(other); }
    MeshAsset& MeshAsset::operator=(MeshAsset&& other) noexcept
    {
        if (this != &other)
        {
            MeshAsset pending(std::move(other));
            Swap(pending);
        }
        return *this;
    }
}
