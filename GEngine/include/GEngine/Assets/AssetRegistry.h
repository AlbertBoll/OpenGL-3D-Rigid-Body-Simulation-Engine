#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/AssetPublication.h"
#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace GEngine::Asset
{
    // Storage needing GPU completion (e.g. mapped/ring memory) supplies a fence.
    // Polling and destruction occur only at the owner's retirement safe point.
    class AssetRetirementFence
    {
    public:
        virtual ~AssetRetirementFence() = default;
        virtual bool IsComplete() const noexcept = 0;
    };

    struct AssetRegistryLimits
    {
        std::uint32_t maxSlots = (std::numeric_limits<std::uint32_t>::max)();
        std::uint64_t maxGeneration = (std::numeric_limits<std::uint64_t>::max)();
        std::uint64_t maxRevision = (std::numeric_limits<std::uint64_t>::max)();
    };

    template<class Tag, class Resource>
    class AssetRegistry<AssetHandle<Tag>, Resource> final
    {
        static_assert(std::is_nothrow_destructible_v<Resource>, "Registry resources need noexcept destruction");
        using Handle = AssetHandle<Tag>;
        struct Version
        {
            template<class... Args>
            explicit Version(std::uint64_t number, Args&&... args)
                : revision(number), fences(0), resource(std::forward<Args>(args)...) {}
            const std::uint64_t revision;
            std::vector<std::unique_ptr<AssetRetirementFence>> fences;
            Resource resource;
        };
        struct Slot
        {
            std::shared_ptr<Version> current;
            std::uint64_t generation = 1;
            std::uint32_t nextFree = Handle::NullIndex;
        };

    public:
        // The registry keeps a strong retirement reference even after destroy or
        // replacement. A worker dropping a lease can never destroy a GPU owner.
        class Lease final
        {
        public:
            Lease() = default;
            explicit operator bool() const noexcept { return bool(m_Version); }
            const Resource* Get() const noexcept { return m_Version ? &m_Version->resource : nullptr; }
            const Resource& operator*() const
            {
                if (!m_Version) throw std::logic_error("Empty asset lease");
                return m_Version->resource;
            }
            const Resource* operator->() const { return &**this; }
            Handle Identity() const noexcept { return m_Handle; }
            std::uint64_t Revision() const noexcept { return m_Version ? m_Version->revision : 0; }
        private:
            friend class AssetRegistry;
            Lease(Handle handle, std::shared_ptr<Version> version) : m_Handle(handle), m_Version(std::move(version)) {}
            Handle m_Handle{};
            std::shared_ptr<Version> m_Version;
        };

        explicit AssetRegistry(AssetPublication& publication, AssetRegistryLimits limits = {})
            : m_Publication(publication), m_Limits(limits),
              m_Identity(detail::TakeRegistryIdentity(detail::nextRegistryIdentity)), m_Slots(0), m_Retired(0)
        {
            if (!limits.maxSlots || !limits.maxGeneration || !limits.maxRevision)
                throw std::invalid_argument("Asset registry limits must be nonzero");
            m_Publication.Register();
        }
        AssetRegistry(const AssetRegistry&) = delete;
        AssetRegistry& operator=(const AssetRegistry&) = delete;
        ~AssetRegistry()
        {
            m_Publication.RequireRetirement();
            // Fail before dropping any reference if CPU/GPU users still exist.
            if (m_Mutating || !CanClose()) std::terminate();
            m_Mutating = true;
            m_Slots.clear(); m_Retired.clear();
            m_Publication.Unregister();
        }

        template<class... Args>
        [[nodiscard]] Handle Create(const AssetPublication::Publication& access, Args&&... args)
        {
            Mutation mutation(*this, access);
            if (m_Free == Handle::NullIndex && m_Slots.size() >= m_Limits.maxSlots)
                throw std::overflow_error("Asset registry slots exhausted");
            auto version = std::make_shared<Version>(1, std::forward<Args>(args)...);
            std::uint32_t index = m_Free;
            if (index == Handle::NullIndex)
            {
                index = static_cast<std::uint32_t>(m_Slots.size());
                m_Slots.emplace_back();
            }
            else m_Free = m_Slots[index].nextFree;
            auto& slot = m_Slots[index];
            slot.nextFree = Handle::NullIndex;
            slot.current = std::move(version);
            ++m_Live;
            return {index, slot.generation, m_Identity};
        }

        [[nodiscard]] Lease Acquire(const AssetPublication::FrameAccess& access, Handle handle) const
        {
            m_Publication.Require(access);
            const auto* slot = Find(handle);
            return slot ? Lease(handle, slot->current) : Lease{};
        }

        template<class... Args>
        bool Replace(const AssetPublication::Publication& access, Handle handle, Args&&... args)
        {
            Mutation mutation(*this, access);
            auto* slot = Find(handle);
            if (!slot) return false;
            if (slot->current->revision == m_Limits.maxRevision)
                throw std::overflow_error("Asset revision exhausted; destroy and create a new identity");
            auto version = std::make_shared<Version>(slot->current->revision + 1, std::forward<Args>(args)...);
            m_Retired.push_back(slot->current); // Failure leaves the published version intact.
            slot->current = std::move(version);
            return true;
        }

        bool Destroy(const AssetPublication::Publication& access, Handle handle)
        {
            Mutation mutation(*this, access);
            auto* slot = Find(handle);
            if (!slot) return false;
            m_Retired.push_back(slot->current);
            slot->current.reset();
            --m_Live;
            if (slot->generation != m_Limits.maxGeneration)
            {
                ++slot->generation;
                slot->nextFree = m_Free;
                m_Free = handle.index;
            }
            // Exhausted slots stay empty forever; an old generation never revives.
            return true;
        }

        // Register before submitting GPU work that needs explicit storage retention.
        // Multiple submissions may attach independent fences to the same version.
        void ProtectGpuUse(const AssetPublication::FrameAccess& access, const Lease& lease,
            std::unique_ptr<AssetRetirementFence>&& fence)
        {
            m_Publication.Require(access);
            if (m_Closed || !lease || lease.Identity().registry != m_Identity || !fence)
                throw std::invalid_argument("GPU retention requires a lease and fence from this registry");
            lease.m_Version->fences.push_back(std::move(fence));
        }

        std::size_t Collect(const AssetPublication::Publication& access)
        {
            Mutation mutation(*this, access);
            for (auto& slot : m_Slots) if (slot.current) PruneFences(*slot.current);
            const auto before = m_Retired.size();
            std::erase_if(m_Retired, [](auto& version) {
                PruneFences(*version);
                return version.use_count() == 1 && version->fences.empty();
            });
            return before - m_Retired.size();
        }

        // Retry at a later safe point if any CPU lease or GPU fence is still live.
        bool Close(const AssetPublication::Publication& access)
        {
            m_Publication.Require(access);
            if (m_Closed) return true;
            Mutation mutation(*this, access);
            if (!CanClose()) return false;
            m_Slots.clear(); m_Retired.clear(); m_Free = Handle::NullIndex; m_Live = 0;
            m_Closed = true;
            return true;
        }

        std::size_t Size() const { m_Publication.RequireOwner(); return m_Live; }

    private:
        class Mutation
        {
        public:
            Mutation(AssetRegistry& registry, const AssetPublication::Publication& access) : m_Registry(registry)
            {
                registry.m_Publication.Require(access);
                if (registry.m_Mutating || registry.m_Closed) throw std::logic_error("Registry is closed or mutating recursively");
                registry.m_Mutating = true;
            }
            ~Mutation() { m_Registry.m_Mutating = false; }
        private:
            AssetRegistry& m_Registry;
        };

        Slot* Find(Handle handle)
        {
            if (!handle || handle.registry != m_Identity || handle.index >= m_Slots.size()) return nullptr;
            auto& slot = m_Slots[handle.index];
            return slot.current && slot.generation == handle.generation ? &slot : nullptr;
        }
        const Slot* Find(Handle handle) const { return const_cast<AssetRegistry*>(this)->Find(handle); }
        static void PruneFences(Version& version)
        { std::erase_if(version.fences, [](const auto& fence) { return fence->IsComplete(); }); }
        static bool Unused(const std::shared_ptr<Version>& version) noexcept
        {
            return !version || (version.use_count() == 1 && std::all_of(version->fences.begin(), version->fences.end(),
                [](const auto& fence) { return fence->IsComplete(); }));
        }
        bool CanClose() const noexcept
        {
            return std::all_of(m_Slots.begin(), m_Slots.end(), [](const auto& slot) { return Unused(slot.current); })
                && std::all_of(m_Retired.begin(), m_Retired.end(), [](const auto& version) { return Unused(version); });
        }

        AssetPublication& m_Publication;
        const AssetRegistryLimits m_Limits;
        const std::uint64_t m_Identity;
        std::vector<Slot> m_Slots;
        std::vector<std::shared_ptr<Version>> m_Retired;
        std::uint32_t m_Free = Handle::NullIndex;
        std::size_t m_Live = 0;
        bool m_Mutating = false;
        bool m_Closed = false;
    };
}
