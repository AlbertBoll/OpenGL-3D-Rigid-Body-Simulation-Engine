#include "gepch.h"
#include "Mesh/GpuMesh.h"
#include "Mesh/VertexBuffer.h"
#include "Mesh/IndexBuffer.h"
#include "Mesh/Mesh.h"
#include "Core/GLContextThread.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace GEngine
{
    namespace
    {
        using Code = GpuMeshErrorCode;
        std::unexpected<GpuMeshError> Error(Code code, const char* message, std::size_t element = 0)
        { return std::unexpected(GpuMeshError{code, message, element}); }
        std::unexpected<GpuMeshError> RegistryFailure(Asset::RegistryError code)
        { return std::unexpected(GpuMeshError{Code::Registry, "Mesh registry operation failed", 0, code}); }
        GLenum Scalar(VertexScalarFormat format)
        {
            switch (format)
            {
            case VertexScalarFormat::Float32: return GL_FLOAT;
            case VertexScalarFormat::Int8: return GL_BYTE;
            case VertexScalarFormat::UInt8: return GL_UNSIGNED_BYTE;
            case VertexScalarFormat::Int16: return GL_SHORT;
            case VertexScalarFormat::UInt16: return GL_UNSIGNED_SHORT;
            case VertexScalarFormat::Int32: return GL_INT;
            case VertexScalarFormat::UInt32: return GL_UNSIGNED_INT;
            }
            return 0;
        }
        bool SameLayout(VertexLayout a, VertexLayout b)
        {
            if (a.strideBytes != b.strideBytes || a.attributes.size() != b.attributes.size()) return false;
            for (std::size_t i = 0; i < a.attributes.size(); ++i)
            {
                const auto& x = a.attributes[i]; const auto& y = b.attributes[i];
                if (x.semantic != y.semantic || x.slot != y.slot || x.scalar != y.scalar
                    || x.components != y.components || x.interpretation != y.interpretation || x.offsetBytes != y.offsetBytes)
                    return false;
            }
            return true;
        }
        bool DriverFailed(const char* operation)
        {
            const auto error = glGetError();
            if (error == GL_NO_ERROR) return false;
            if (Log::GetCoreLogger()) Log::GetCoreLogger()->error("GpuMesh {}: driver error {}", operation, error);
            return true;
        }
        struct Bindings
        {
            GLint vao = 0, array = 0;
            Bindings() { glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao); glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array); }
            ~Bindings() { glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, array); }
        };
    }

    struct GpuMesh::Storage
    {
        // These are the Phase 22 owners. The new path bypasses legacy allocating /
        // exception-based upload APIs; their existing destructors retire each name.
        Buffer::VertexBuffer vertex;
        Buffer::IndexBuffer index;
        Mesh vao{nullptr}; // Retires before its buffers (reverse member order).
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        std::array<VertexAttribute, MeshAttributeSlots> attributes{};
        std::size_t attributeCount = 0, stride = 0, vertices = 0, indices = 0;
        std::unique_ptr<SubmeshRange[]> submeshes;
        std::size_t submeshCount = 0;
        std::uint32_t materialSlots = 0;
        MeshIndexFormat indexFormat = MeshIndexFormat::None;
        MeshUpdateIntent intent = MeshUpdateIntent::Static;
        LocalBounds bounds;
        void RequireContext() const
        {
            GLContextThread::RequireCurrent("GpuMesh operation/retirement");
            Asset::AssetDetail::RequireInvariant(context == SDL_GL_GetCurrentContext());
        }
        ~Storage() { RequireContext(); }
    };

    GpuMesh::GpuMesh() noexcept = default;
    GpuMesh::~GpuMesh() = default;
    GpuMesh::GpuMesh(GpuMesh&&) noexcept = default;
    GpuMesh& GpuMesh::operator=(GpuMesh&& other) noexcept
    { if (this != &other) m_Storage = std::move(other.m_Storage); return *this; }
    GpuMesh::operator bool() const noexcept { return bool(m_Storage); }
    VertexLayout GpuMesh::Layout() const noexcept
    { return m_Storage ? VertexLayout{m_Storage->stride, {m_Storage->attributes.data(), m_Storage->attributeCount}} : VertexLayout{}; }
    std::span<const SubmeshRange> GpuMesh::Submeshes() const noexcept
    { return m_Storage ? std::span<const SubmeshRange>{m_Storage->submeshes.get(), m_Storage->submeshCount} : std::span<const SubmeshRange>{}; }
    std::size_t GpuMesh::VertexCount() const noexcept { return m_Storage ? m_Storage->vertices : 0; }
    std::size_t GpuMesh::IndexCount() const noexcept { return m_Storage ? m_Storage->indices : 0; }
    MeshIndexFormat GpuMesh::IndexFormat() const noexcept { return m_Storage ? m_Storage->indexFormat : MeshIndexFormat::None; }
    MeshUpdateIntent GpuMesh::UpdateIntent() const noexcept { return m_Storage ? m_Storage->intent : MeshUpdateIntent::Static; }
    std::uint32_t GpuMesh::MaterialSlotCount() const noexcept { return m_Storage ? m_Storage->materialSlots : 0; }
    LocalBounds GpuMesh::Bounds() const noexcept { return m_Storage ? m_Storage->bounds : LocalBounds{}; }

    std::expected<GpuMesh, GpuMeshError> GpuMesh::Create(const MeshAsset& source)
    {
        GLContextThread::RequireCurrent("GpuMesh creation");
        const auto layout = source.Layout();
        if (!layout.strideBytes || layout.attributes.empty()) return Error(Code::InvalidAsset, "Moved-from mesh source");
        if (DriverFailed("before creation")) return Error(Code::Driver, "Pre-existing driver error; no mesh created");
        GLint maxAttributes = 0, maxStride = 0;
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttributes);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIB_STRIDE, &maxStride);
        const auto maxCount = static_cast<std::size_t>((std::numeric_limits<GLsizei>::max)());
        const auto maxBytes = static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)());
        if (maxStride <= 0 || layout.strideBytes > static_cast<std::size_t>(maxStride)
            || layout.strideBytes > maxCount || source.VertexCount() > maxCount || source.IndexCount() > maxCount
            || source.VertexCount() > maxBytes / layout.strideBytes
            || source.Vertices().size() != source.VertexCount() * layout.strideBytes || source.Indices().size() > maxBytes)
            return Error(Code::DeviceLimit, "Mesh record/count/storage exceeds device or API limits");
        for (const auto& a : layout.attributes)
            if (a.slot >= maxAttributes || !Scalar(a.scalar))
                return Error(Code::DeviceLimit, "Unsupported attribute format or slot", a.slot);

        GpuMesh result;
        result.m_Storage.reset(new (std::nothrow) Storage);
        if (!result.m_Storage) return Error(Code::Allocation, "Mesh owner allocation failed");
        auto& s = *result.m_Storage;
        if (!source.Submeshes().empty())
        {
            s.submeshes.reset(new (std::nothrow) SubmeshRange[source.Submeshes().size()]);
            if (!s.submeshes) return Error(Code::Allocation, "Submesh metadata allocation failed");
            std::copy(source.Submeshes().begin(), source.Submeshes().end(), s.submeshes.get());
        }
        s.submeshCount = source.Submeshes().size(); s.materialSlots = source.MaterialSlotCount();
        std::copy(layout.attributes.begin(), layout.attributes.end(), s.attributes.begin());
        s.attributeCount = layout.attributes.size(); s.stride = layout.strideBytes;
        s.vertices = source.VertexCount(); s.indices = source.IndexCount();
        s.indexFormat = source.IndexFormat(); s.intent = source.UpdateIntent();
        s.bounds = source.Bounds();
        Bindings previous;
        glGenVertexArrays(1, &s.vao.m_VertexArrayRef);
        if (!s.vao.m_VertexArrayRef) return Error(Code::Allocation, "Driver did not allocate a vertex array");
        glBindVertexArray(s.vao.m_VertexArrayRef);
        glGenBuffers(1, &s.vertex.m_VertexBufferRef);
        if (!s.vertex.m_VertexBufferRef) return Error(Code::Allocation, "Driver did not allocate a vertex buffer");
        s.vertex.m_CapacityBytes = source.Vertices().size();
        glBindBuffer(GL_ARRAY_BUFFER, s.vertex.m_VertexBufferRef);
        const auto usage = s.intent == MeshUpdateIntent::Dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(source.Vertices().size()), source.Vertices().data(), usage);
        GLint64 bytes = -1;
        glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bytes);
        if (DriverFailed("vertex upload") || bytes != static_cast<GLint64>(source.Vertices().size()))
            return Error(Code::Driver, "Vertex upload/storage verification failed");
        for (const auto& a : layout.attributes)
        {
            glEnableVertexAttribArray(a.slot);
            if (a.interpretation == VertexInterpretation::Integer)
                glVertexAttribIPointer(a.slot, a.components, Scalar(a.scalar), static_cast<GLsizei>(s.stride), reinterpret_cast<const void*>(a.offsetBytes));
            else glVertexAttribPointer(a.slot, a.components, Scalar(a.scalar), a.interpretation == VertexInterpretation::Normalized,
                static_cast<GLsizei>(s.stride), reinterpret_cast<const void*>(a.offsetBytes));
            const std::pair<GLenum, GLint> expected[]{
                {GL_VERTEX_ATTRIB_ARRAY_ENABLED, GL_TRUE}, {GL_VERTEX_ATTRIB_ARRAY_SIZE, a.components},
                {GL_VERTEX_ATTRIB_ARRAY_TYPE, static_cast<GLint>(Scalar(a.scalar))},
                {GL_VERTEX_ATTRIB_ARRAY_STRIDE, static_cast<GLint>(s.stride)},
                {GL_VERTEX_ATTRIB_ARRAY_INTEGER, a.interpretation == VertexInterpretation::Integer},
                {GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, a.interpretation == VertexInterpretation::Normalized},
                {GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, static_cast<GLint>(s.vertex.m_VertexBufferRef)}};
            for (const auto& [key, value] : expected)
            {
                GLint actual = -1; glGetVertexAttribiv(a.slot, key, &actual);
                if (actual != value) return Error(Code::Driver, "Vertex attribute binding verification failed", a.slot);
            }
            void* pointer = nullptr; glGetVertexAttribPointerv(a.slot, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
            if (reinterpret_cast<std::uintptr_t>(pointer) != a.offsetBytes)
                return Error(Code::Driver, "Vertex attribute offset verification failed", a.slot);
        }
        if (s.indexFormat != MeshIndexFormat::None)
        {
            glGenBuffers(1, &s.index.m_IndexBufferRef);
            if (!s.index.m_IndexBufferRef) return Error(Code::Allocation, "Driver did not allocate an index buffer");
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.index.m_IndexBufferRef);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(source.Indices().size()), source.Indices().data(), GL_STATIC_DRAW);
            bytes = -1; glGetBufferParameteri64v(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &bytes);
            if (DriverFailed("index upload") || bytes != static_cast<GLint64>(source.Indices().size()))
                return Error(Code::Driver, "Index upload/storage verification failed");
        }
        if (DriverFailed("mesh creation")) return Error(Code::Driver, "Driver rejected mesh creation");
        return result;
    }

    std::expected<void, GpuMeshError> GpuMesh::UpdateVertices(const VertexRecordUpdate& update)
    {
        if (!m_Storage) return Error(Code::InvalidMesh, "Empty mesh owner");
        auto& s = *m_Storage; s.RequireContext();
        if (s.intent != MeshUpdateIntent::Dynamic) return Error(Code::Immutable, "Static vertex content cannot be updated");
        if (!SameLayout(update.layout, Layout())) return Error(Code::IncompatibleLayout, "Update must preserve the exact vertex layout");
        if (update.firstVertex > s.vertices || update.vertexCount > s.vertices - update.firstVertex)
            return Error(Code::InvalidRange, "Update exceeds vertex capacity");
        // Creation proved capacity*stride representable; bounded ranges inherit it.
        const auto bytes = update.vertexCount * s.stride, offset = update.firstVertex * s.stride;
        if (update.records.size() != bytes) return Error(Code::PayloadMismatch, "Update must contain complete vertex records");
        std::size_t positionOffset = 0;
        auto bounds = s.bounds;
        for (const auto& a : Layout().attributes) if (a.semantic == VertexSemantic::Position) positionOffset = a.offsetBytes;
        for (std::size_t i = 0; i < update.vertexCount; ++i)
        {
            float position[3]; std::memcpy(position, update.records.data() + i * s.stride + positionOffset, sizeof(position));
            for (float value : position) if (!std::isfinite(value)) return Error(Code::NonFinitePosition, "Nonfinite updated position", i);
            // Retain a conservative union across partial updates without retaining
            // the heavy CPU vertex payload. Recreation restores tight mesh bounds.
            for (int axis = 0; axis != 3; ++axis)
            {
                bounds.minimum[axis] = (std::min)(bounds.minimum[axis], double(position[axis]));
                bounds.maximum[axis] = (std::max)(bounds.maximum[axis], double(position[axis]));
            }
        }
        if (!bytes) return {};
        if (DriverFailed("before vertex update")) return Error(Code::Driver, "Pre-existing driver error; no mesh update issued");
        Bindings previous;
        glBindBuffer(GL_ARRAY_BUFFER, s.vertex.m_VertexBufferRef);
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), update.records.data());
        std::array<double, 3> extent;
        const double infinity = std::numeric_limits<double>::infinity();
        for (int axis = 0; axis != 3; ++axis)
        {
            bounds.sphereCenter[axis] = (bounds.minimum[axis] + bounds.maximum[axis]) * .5;
            extent[axis] = std::nextafter((std::max)(bounds.maximum[axis] - bounds.sphereCenter[axis],
                bounds.sphereCenter[axis] - bounds.minimum[axis]), infinity);
        }
        bounds.sphereRadius = std::nextafter(std::hypot(extent[0], extent[1], extent[2]), infinity);
        // An issued update may have modified storage even if the driver reports
        // failure. The union is safe for both old and attempted content.
        s.bounds = bounds;
        if (DriverFailed("vertex update")) return Error(Code::Driver, "Driver rejected vertex update");
        return {};
    }

    std::expected<void, GpuMeshError> GpuMesh::DrawSubmesh(std::size_t submesh, MeshPrimitive primitive) const
    {
        if (!m_Storage) return Error(Code::InvalidMesh, "Empty mesh owner");
        const auto& s = *m_Storage; s.RequireContext();
        if (submesh >= s.submeshCount) return Error(Code::InvalidRange, "Submesh index out of range", submesh);
        GLenum mode = 0;
        switch (primitive)
        {
        case MeshPrimitive::Points: mode = GL_POINTS; break;
        case MeshPrimitive::Lines: mode = GL_LINES; break;
        case MeshPrimitive::Triangles: mode = GL_TRIANGLES; break;
        default: return Error(Code::InvalidPrimitive, "Unsupported primitive topology");
        }
        const auto& range = s.submeshes[submesh];
        if (!range.elementCount) return {};
        if (DriverFailed("before draw")) return Error(Code::Driver, "Pre-existing driver error; no draw issued");
        Bindings previous; glBindVertexArray(s.vao.m_VertexArrayRef);
        if (s.indexFormat == MeshIndexFormat::None)
            glDrawArrays(mode, static_cast<GLint>(range.firstElement), static_cast<GLsizei>(range.elementCount));
        else
        {
            const bool shortIndex = s.indexFormat == MeshIndexFormat::UInt16;
            glDrawElements(mode, static_cast<GLsizei>(range.elementCount), shortIndex ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                reinterpret_cast<const void*>(range.firstElement * (shortIndex ? 2 : 4)));
        }
        if (DriverFailed("draw")) return Error(Code::Driver, "Driver rejected submesh draw", submesh);
        return {};
    }

    std::expected<Asset::MeshHandle, GpuMeshError> PublishMesh(MeshRegistry& registry,
        const Asset::AssetPublication::Publication& access, const MeshAsset& source)
    {
        auto mesh = GpuMesh::Create(source);
        if (!mesh) return std::unexpected(mesh.error());
        auto handle = registry.Create(access, std::move(*mesh));
        if (!handle) return RegistryFailure(handle.error());
        return *handle;
    }
    std::expected<void, GpuMeshError> UpdateMesh(MeshRegistry& registry,
        const Asset::AssetPublication::Publication& access, Asset::MeshHandle handle, const VertexRecordUpdate& update)
    {
        auto result = registry.Update<GpuMeshError>(access, handle, [&](GpuMesh& mesh) { return mesh.UpdateVertices(update); });
        if (!result) return RegistryFailure(result.error());
        return *result;
    }
}
