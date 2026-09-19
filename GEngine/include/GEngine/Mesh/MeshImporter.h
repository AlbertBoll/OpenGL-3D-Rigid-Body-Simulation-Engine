#pragma once

#include "Mesh/MeshAsset.h"

namespace GEngine
{
    using MeshImportVector = std::array<float, 3>;
    using MeshImportUV = std::array<float, 2>;

    enum class MeshSourceHandedness : std::uint8_t { Right, Left };
    enum class MeshSourceWinding : std::uint8_t { CounterClockwise, Clockwise };
    enum class MissingMeshNormals : std::uint8_t { GenerateSmooth, Reject };
    enum class MissingMeshUVs : std::uint8_t { Zero, Reject };
    enum class MissingMeshTangents : std::uint8_t { Generate, Reject };

    struct MeshImportOptions
    {
        MeshSourceHandedness handedness = MeshSourceHandedness::Right;
        MeshSourceWinding winding = MeshSourceWinding::CounterClockwise;
        MissingMeshNormals normals = MissingMeshNormals::GenerateSmooth;
        MissingMeshUVs uvs = MissingMeshUVs::Zero;
        MissingMeshTangents tangents = MissingMeshTangents::Generate;
        bool flipV = false;
    };

    // One triangle-list instance, in caller order. Optional arrays are empty or
    // exactly positions.size(); tangents and bitangents must appear together.
    // Spans need only survive NormalizeImportedMesh. Materials are slot ordinals,
    // never backend objects. Row-major affine transform maps local to model space.
    struct MeshImportPart
    {
        std::span<const MeshImportVector> positions, normals, tangents, bitangents;
        std::span<const MeshImportUV> uvs;
        std::span<const std::uint32_t> indices;
        std::uint32_t materialSlot = 0;
        std::array<double, 16> transform{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    };

    enum class MeshImportField : std::uint8_t
    { None, Position, Normal, UV, Tangent, Bitangent, Index, Transform, Material };
    enum class MeshImportErrorCode : std::uint8_t
    {
        InvalidOptions, InvalidPath, ReadFailed, InvalidScene, UnsupportedContent,
        EmptyMesh, SizeOverflow, AllocationFailed, AttributeCountMismatch,
        MissingAttribute, NonFiniteAttribute, InvalidDirection, InvalidTransform,
        InvalidTriangleRange, IndexOutOfRange, MaterialSlotOutOfRange,
        DegenerateTriangle, NormalGenerationFailed, TangentGenerationFailed,
        MeshValidationFailed, MemoryBudgetExceeded, Cancelled
    };
    struct MeshImportError
    {
        MeshImportErrorCode code;
        std::size_t part = 0, element = 0;
        MeshImportField field = MeshImportField::None;
        MeshError meshError{}; // meaningful only for MeshValidationFailed, MemoryBudgetExceeded, Cancelled
    };

    // Bounds engine-owned heap payload, including adapter copies, normalization
    // scratch and completed MeshAsset storage. Assimp-owned internal allocations
    // are explicitly excluded by the Phase 51 owner-approved exception. Fixed
    // stack frames and caller-controlled source/descriptor storage are not payload.
    // Callback/context are borrowed only for the synchronous import call.
    struct MeshImportControl
    {
        std::size_t maxEngineBytes = SIZE_MAX;
        const void* cancellationContext{};
        bool (*stopRequested)(const void*) noexcept = nullptr;
        bool Stopped() const noexcept
        { return stopRequested && stopRequested(cancellationContext); }
    };

    // Canonical output: right-handed model space, CCW triangles, Float32 position,
    // normal, UV0, tangent and bitangent; UInt32 absolute indices; static intent.
    // Left-handed sources reflect Z after the instance transform. Negative
    // determinants and source winding both correct index order. Supplied normals
    // use inverse transpose; tangent frames are orthonormalized with handedness.
    // Missing normals are area-weighted across shared indexed vertices (no weld).
    // Missing UV policy Zero explicitly generates (0,0); missing tangents then
    // use a deterministic orthogonal basis, NOT a texture-derived tangent frame.
    // With UVs present, generated tangents require nonsingular UV triangles.
    // Nonfinite/zero directions, degenerate triangles, unused vertices without
    // generatable frames and singular transforms are errors, never silent repairs.
    // Nothing is published or uploaded, including on failure; workers may use this.
    std::expected<MeshAsset, MeshImportError> NormalizeImportedMesh(
        std::span<const MeshImportPart> parts, std::uint32_t materialSlotCount,
        const MeshImportOptions& options = {}, const MeshImportControl& control = {}) noexcept;

    // Static Assimp adapter: depth-first node/mesh order, transforms baked per
    // instance, material table ordinals retained. Bones/morphs/animations and
    // non-triangle primitives are unsupported. No scene/native types escape.
    // Assimp's format reader first decodes its format conventions; options describe
    // the resulting source coordinates. No additional implicit UV/axis conversion.
    std::expected<MeshAsset, MeshImportError> ImportMeshFile(
        const char* path, const MeshImportOptions& options = {}, const MeshImportControl& control = {});
    std::expected<MeshAsset, MeshImportError> ImportMeshMemory(
        std::span<const std::byte> bytes, const char* formatHint,
        const MeshImportOptions& options = {}, const MeshImportControl& control = {});
}
