#pragma once
#include "Scene/RenderState.h"
#include "Renderer/RenderVisibility.h"
#include <algorithm>
#include <bit>
#include <new>
#include <thread>
#include <vector>
#include <atomic>
#include <concepts>
#include <expected>
#include <limits>
#include <type_traits>
#include <utility>

namespace GEngine::RenderCpu
{
    inline constexpr std::size_t PageSize = 128;
    struct Counts
    {
        std::size_t scanned{}, semanticRows{}, pagesCloned{}, bounds{}, world{};
        std::size_t meshAcquire{}, materialAcquire{}, prepare{}, ownerGroups{};
        std::size_t drawWrites{}, drawBytesCopied{}, resourceRows{};
        std::size_t visibilityHits{}, visibilityPartial{}, visibilityFull{}, visibilityTests{};
        std::size_t listWrites{}, listAllocations{}, listBytesCopied{};
        std::size_t worldCpuBytes{}, sceneCpuBytes{}, semanticBytes{};
    };
#ifdef GENGINE_RENDER_WORLD_DIAGNOSTICS
    inline thread_local Counts counts;
#define RW_COUNT(field, amount) (::GEngine::RenderCpu::counts.field += (amount))
#else
#define RW_COUNT(field, amount) ((void)0)
#endif
    // RenderWorld-private storage only. No standard container/control-block allocation
    // occurs in these operations. Construction/growth are explicit typed results.
    enum class StorageFailure { Allocation, Capacity };
#ifdef GENGINE_RENDER_WORLD_DIAGNOSTICS
    inline std::atomic<std::size_t> liveCpuBlocks{};
#endif
    struct CpuObject
    {
        mutable std::atomic<std::size_t> references{1};
        CpuObject() noexcept {
#ifdef GENGINE_RENDER_WORLD_DIAGNOSTICS
            ++liveCpuBlocks;
#endif
        }
        ~CpuObject() noexcept {
#ifdef GENGINE_RENDER_WORLD_DIAGNOSTICS
            --liveCpuBlocks;
#endif
        }
        CpuObject(const CpuObject&) = delete;
        CpuObject& operator=(const CpuObject&) = delete;
    };
    template<class T> class CpuOwner
    {
        template<class U> friend class CpuOwner;
        T* m_Value{};
        struct AdoptTag {};
        explicit CpuOwner(T* value,AdoptTag) noexcept:m_Value(value){}
        void Retain() noexcept {
            if(m_Value) {
                const auto old=m_Value->references.fetch_add(1,std::memory_order_relaxed);
                Asset::AssetDetail::RequireInvariant(old&&old!=SIZE_MAX);
            }
        }
    public:
        CpuOwner() noexcept = default;
        CpuOwner(std::nullptr_t) noexcept {}
        CpuOwner(const CpuOwner& value) noexcept:m_Value(value.m_Value){Retain();}
        template<class U> requires std::convertible_to<U*,T*>
        CpuOwner(const CpuOwner<U>& value) noexcept:m_Value(value.m_Value){Retain();}
        CpuOwner(CpuOwner&& value) noexcept:m_Value(std::exchange(value.m_Value,nullptr)){}
        template<class U> requires std::convertible_to<U*,T*>
        CpuOwner(CpuOwner<U>&& value) noexcept:m_Value(std::exchange(value.m_Value,nullptr)){}
        ~CpuOwner() noexcept{reset();}
        CpuOwner& operator=(CpuOwner value) noexcept{swap(value);return *this;}
        void swap(CpuOwner& other) noexcept{std::swap(m_Value,other.m_Value);}
        void reset() noexcept {
            T* value=std::exchange(m_Value,nullptr);
            if(value&&value->references.fetch_sub(1,std::memory_order_acq_rel)==1)delete value;
        }
        T* get() const noexcept{return m_Value;}
        T* operator->() const noexcept{return m_Value;}
        T& operator*() const noexcept{return *m_Value;}
        explicit operator bool() const noexcept{return m_Value!=nullptr;}
        // Private bridge to the opaque, move-only visibility result. Detach/Adopt
        // transfer one owned reference; they never create a borrowed lifetime.
        T* Detach() noexcept{return std::exchange(m_Value,nullptr);}
        static CpuOwner Adopt(T* value) noexcept{return CpuOwner(value,AdoptTag{});}
        [[nodiscard]] static std::expected<CpuOwner,StorageFailure> Create() noexcept
            requires (!std::is_const_v<T> && std::derived_from<T,CpuObject>
                && std::is_nothrow_default_constructible_v<T> && std::is_nothrow_destructible_v<T>)
        {
            auto* value=new(std::nothrow)T;
            if(!value)return std::unexpected(StorageFailure::Allocation);
            return Adopt(value);
        }
    };
    template<class T> requires (std::is_nothrow_default_constructible_v<T>
        &&std::is_nothrow_copy_assignable_v<T>&&std::is_nothrow_destructible_v<T>)
    class CpuArray
    {
        std::unique_ptr<T[]> m_Data;
        std::size_t m_Size{},m_Capacity{};
    public:
        CpuArray() noexcept=default;
        CpuArray(const CpuArray&)=delete;
        CpuArray& operator=(const CpuArray&)=delete;
        CpuArray(CpuArray&& other) noexcept
            :m_Data(std::move(other.m_Data)),m_Size(std::exchange(other.m_Size,0)),m_Capacity(std::exchange(other.m_Capacity,0)){}
        CpuArray& operator=(CpuArray&& other) noexcept {
            if(this!=&other){m_Data=std::move(other.m_Data);m_Size=std::exchange(other.m_Size,0);m_Capacity=std::exchange(other.m_Capacity,0);}return *this;
        }
        T* data() noexcept{return m_Data.get();}
        const T* data() const noexcept{return m_Data.get();}
        T* begin() noexcept{return data();}const T* begin()const noexcept{return data();}
        T* end() noexcept{return m_Size?data()+m_Size:data();}
        const T* end()const noexcept{return m_Size?data()+m_Size:data();}
        std::size_t size()const noexcept{return m_Size;}
        std::size_t capacity()const noexcept{return m_Capacity;}
        bool empty()const noexcept{return !m_Size;}
        T& operator[](std::size_t i)noexcept{return m_Data[i];}
        const T& operator[](std::size_t i)const noexcept{return m_Data[i];}
        [[nodiscard]] std::expected<void,StorageFailure> Reserve(std::size_t capacity) noexcept {
            if(capacity<=m_Capacity)return {};
            if(capacity>static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)())/sizeof(T))
                return std::unexpected(StorageFailure::Capacity);
            std::unique_ptr<T[]> values(new(std::nothrow)T[capacity]{});
            if(!values)return std::unexpected(StorageFailure::Allocation);
            if(m_Size)std::copy_n(m_Data.get(),m_Size,values.get());
            m_Data=std::move(values);m_Capacity=capacity;return {};
        }
        [[nodiscard]] std::expected<void,StorageFailure> Resize(std::size_t size) noexcept {
            if(auto result=Reserve(size);!result)return result;
            for(std::size_t i=size;i<m_Size;++i)m_Data[i]=T{};
            m_Size=size;return {};
        }
        [[nodiscard]] std::expected<void,StorageFailure> Assign(std::span<const T> values) noexcept {
            if(auto result=Resize(values.size());!result)return result;
            if(m_Size)std::copy_n(values.data(),m_Size,m_Data.get());return {};
        }
        [[nodiscard]] std::expected<void,StorageFailure> PrepareAppend() noexcept {
            if(m_Size==m_Capacity) {
                const auto maximum=static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)())/sizeof(T);
                if(m_Size==maximum)return std::unexpected(StorageFailure::Capacity);
                const auto growth=m_Capacity>maximum-m_Capacity/2?maximum:m_Capacity+m_Capacity/2;
                if(auto result=Reserve((std::max)(m_Size+1,growth));!result)return result;
            }
            return {};
        }
        [[nodiscard]] std::expected<void,StorageFailure> Append(const T& value) noexcept {
            if(auto result=PrepareAppend();!result)return result;
            m_Data[m_Size++]=value;return {};
        }
        // No allocation: only used after successful checked sizing, for deduplication.
        void Shrink(std::size_t size)noexcept {
            Asset::AssetDetail::RequireInvariant(size<=m_Size);
            for(std::size_t i=size;i<m_Size;++i)m_Data[i]=T{};m_Size=size;
        }
    };
    using VersionKey = std::array<std::uint64_t,4>;
    using VersionList = std::vector<VersionKey>;
    struct SemanticPage { std::array<RenderSemanticRecord,PageSize> rows; };
    struct SceneSnapshot
    {
        std::vector<std::shared_ptr<const SemanticPage>> pages;
        std::size_t size{};
        const RenderSemanticRecord& At(std::size_t i) const noexcept { return pages[i/PageSize]->rows[i%PageSize]; }
    };
    // Only the per-call envelope owns these resources; Domain never references it.
    struct MeshGroup { FrameMeshReference mesh; std::optional<Asset::RegistryError> error; };
    struct MaterialGroup
    {
        FrameMaterialReference material;
        std::optional<MaterialBindingError> error;
    };
    struct CallBindings { std::vector<MeshGroup> meshes; std::vector<MaterialGroup> materials; };

    struct DrawRows : CpuObject
    {
        DrawRows() noexcept = default;
        std::unique_ptr<DrawItem[]> values;
        std::size_t size{};
    };
    struct VisibilityInput
    {
        EntityRenderId entity;
        WorldBounds bounds;
        AlphaMode alpha = AlphaMode::Opaque;
        std::uint32_t layers{};
        bool empty{}, casts{}, pickable{};
        std::uint64_t semanticEpoch{};
        std::uint64_t visibilityEpoch{}, geometryEpoch{};
    };
    struct DrawInputs : CpuObject
    {
        DrawInputs() noexcept = default;
        std::unique_ptr<VisibilityInput[]> values;
        std::size_t size{};
    };
    struct DrawSnapshot : CpuObject
    {
        DrawSnapshot() noexcept = default;
        CpuOwner<const DrawRows> rows;
        CpuOwner<const DrawInputs> inputs;
        std::uint64_t orderEpoch{}, visibilityEpoch{}, predecessorVisibilityEpoch{};
        CpuArray<std::size_t> changed;
        RenderTargetRevision target;
    };
    struct IndexArray : CpuObject { IndexArray() noexcept = default; std::unique_ptr<std::size_t[]> values; std::size_t count{},capacity{}; };
    struct Contribution
    {
        std::uint8_t lists{};
        BoundsVisibility geometry = BoundsVisibility::Conservative;
        bool emptyDraws{}, layerRejected{}, tested{}, visible{}, culled{}, conservative{};
    };
    struct VisibilitySnapshot : CpuObject
    {
        VisibilitySnapshot() noexcept = default;
        FrameCamera camera;
        std::size_t cameraIndex{};
        RenderTargetRevision target;
        std::uint64_t orderEpoch{}, visibilityEpoch{};
        CpuArray<std::size_t> conservativeDraws;
        CpuArray<std::uint64_t> inputEpochs, geometryEpochs;
        CpuArray<Contribution> contributions;
        std::array<CpuOwner<const IndexArray>,6> lists;
        VisibilityStats stats;
    };
    struct Domain
    {
        const std::thread::id owner = std::this_thread::get_id();
        CpuOwner<const DrawSnapshot> draws;
        CpuOwner<const VisibilitySnapshot> visibility;
        std::uint64_t publicationEpoch{}, nextEpoch{};
    };
    struct DrawStorage
    {
        // Exactly one active row-owner representation.
        std::variant<std::unique_ptr<DrawItem[]>,CpuOwner<const DrawSnapshot>> rows;
        std::weak_ptr<Domain> domain;
        CpuOwner<const VisibilitySnapshot> seed;
        std::thread::id owner;
        std::uint64_t publicationEpoch{};
        // Builder-private staging, cleared before publication.
        CpuOwner<DrawRows> changedRows;
        CpuOwner<DrawInputs> changedInputs;
        CpuOwner<DrawSnapshot> pending;
        std::uint64_t nextEpoch{};
        std::size_t capacity{};
        bool fullRebuild{};
        const DrawItem* Data() const noexcept
        {
            if(const auto* direct=std::get_if<std::unique_ptr<DrawItem[]>>(&rows))return direct->get();
            const auto& snapshot=std::get<CpuOwner<const DrawSnapshot>>(rows);
            return snapshot&&snapshot->rows?snapshot->rows->values.get():nullptr;
        }
    };
    inline bool SameFloat(float a,float b) noexcept { return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b); }
    inline bool SameMatrix(const glm::mat4& a,const glm::mat4& b) noexcept
    { for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!SameFloat(a[c][r],b[c][r]))return false;return true; }
    inline bool SameBounds(const WorldBounds&a,const WorldBounds&b) noexcept
    {
        if(a.status!=b.status)return false;
        for(int i=0;i<3;++i)
            if(std::bit_cast<std::uint64_t>(a.minimum[i])!=std::bit_cast<std::uint64_t>(b.minimum[i])
                ||std::bit_cast<std::uint64_t>(a.maximum[i])!=std::bit_cast<std::uint64_t>(b.maximum[i])
                ||std::bit_cast<std::uint64_t>(a.sphereCenter[i])!=std::bit_cast<std::uint64_t>(b.sphereCenter[i]))return false;
        return std::bit_cast<std::uint64_t>(a.sphereRadius)==std::bit_cast<std::uint64_t>(b.sphereRadius);
    }
    inline std::uint64_t Advance(std::uint64_t& v) noexcept
    { Asset::AssetDetail::RequireInvariant(v!=UINT64_MAX);return ++v; }

    // Only scene extraction calls this trusted bridge. Generic builders have no
    // public certificate/backing adoption API.
    struct FrameAccess
    {
        static std::expected<RenderFrameBuilder,FrameError> Create(FrameCapacity,std::uint64_t,
            const std::shared_ptr<Domain>&,const RenderTargetRevision&,bool fullTransformChange);
        static std::expected<void,FrameError> AddSceneDraw(RenderFrameBuilder&,const EntityRenderState&,std::size_t);
        static const DrawStorage* Storage(const RenderFrame&) noexcept;
    };
}
