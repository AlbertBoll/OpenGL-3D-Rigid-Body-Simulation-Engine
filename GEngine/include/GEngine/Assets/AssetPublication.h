#pragma once

#include <cstddef>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>

namespace GEngine::Asset
{
    template<class Handle, class Resource> class AssetRegistry;

    // Application/root owned until the scheduler takes over the same boundary.
    // Publication ends before extraction; read access covers extraction/submission.
    class AssetPublication final
    {
        enum class Phase { Idle, Publishing, Reading };
    public:
        class Publication final
        {
        public:
            Publication(const Publication&) = delete;
            Publication& operator=(const Publication&) = delete;
            ~Publication() { m_Owner.End(Phase::Publishing); }
        private:
            friend class AssetPublication;
            explicit Publication(AssetPublication& owner) : m_Owner(owner) { owner.Begin(Phase::Publishing); }
            AssetPublication& m_Owner;
        };

        class FrameAccess final
        {
        public:
            FrameAccess(const FrameAccess&) = delete;
            FrameAccess& operator=(const FrameAccess&) = delete;
            ~FrameAccess() { m_Owner.End(Phase::Reading); }
        private:
            friend class AssetPublication;
            explicit FrameAccess(AssetPublication& owner) : m_Owner(owner) { owner.Begin(Phase::Reading); }
            AssetPublication& m_Owner;
        };

        AssetPublication() = default;
        AssetPublication(const AssetPublication&) = delete;
        AssetPublication& operator=(const AssetPublication&) = delete;
        ~AssetPublication() { RequireDrained(); }

        [[nodiscard]] Publication BeginPublication() { return Publication(*this); }
        [[nodiscard]] FrameAccess BeginFrame() { return FrameAccess(*this); }

        // Call before context teardown. Registry/lease owners must retire first.
        void RequireDrained() const noexcept
        {
            if (std::this_thread::get_id() != m_Thread || m_Phase != Phase::Idle || m_Registries) std::terminate();
        }

    private:
        template<class Handle, class Resource> friend class AssetRegistry;
        void RequireOwner() const
        {
            if (std::this_thread::get_id() != m_Thread)
                throw std::logic_error("Asset publication requires its owner thread");
        }
        void Begin(Phase phase)
        {
            RequireOwner();
            if (m_Phase != Phase::Idle) throw std::logic_error("Asset publication and frame access cannot overlap");
            m_Phase = phase;
        }
        void End(Phase phase) noexcept
        {
            if (std::this_thread::get_id() != m_Thread || m_Phase != phase) std::terminate();
            m_Phase = Phase::Idle;
        }
        void Require(const Publication& access) const
        {
            RequireOwner();
            if (&access.m_Owner != this || m_Phase != Phase::Publishing)
                throw std::logic_error("Registry mutation requires its publication safe point");
        }
        void Require(const FrameAccess& access) const
        {
            RequireOwner();
            if (&access.m_Owner != this || m_Phase != Phase::Reading)
                throw std::logic_error("Registry resolution requires its frame access scope");
        }
        void Register()
        {
            RequireOwner();
            if (m_Phase == Phase::Reading) throw std::logic_error("Cannot create a registry during extraction/submission");
            if (m_Registries == (std::numeric_limits<std::size_t>::max)()) throw std::overflow_error("Too many asset registries");
            ++m_Registries;
        }
        void RequireRetirement() const noexcept
        {
            if (std::this_thread::get_id() != m_Thread || m_Phase == Phase::Reading) std::terminate();
        }
        void Unregister() noexcept { RequireRetirement(); --m_Registries; }

        const std::thread::id m_Thread = std::this_thread::get_id();
        Phase m_Phase = Phase::Idle;
        std::size_t m_Registries = 0;
    };
}
