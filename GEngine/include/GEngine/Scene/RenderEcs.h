#pragma once

#include "Component/RenderComponents.h"
#include <entt/entt.hpp>
#include <algorithm>
#include <expected>
#include <functional>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace GEngine
{
    enum class RenderEcsError
    {
        InvalidEntity, StaleId, ForeignId, IdentityExhausted, ExtractionActive,
        MissingComponent, DuplicateComponent, InvalidPick, PickCapacity, ExtractionRequired
    };

    // Retain this CPU table with the corresponding picking image/readback. The actual
    // attachment is signed R32I: -1 means no hit, nonnegative pixels index this table.
    // Never truncate an EntityRenderId or reinterpret a pixel as an EnTT entity.
    class EntityPickTable
    {
    public:
        static constexpr std::int32_t InvalidPixel = -1;
        explicit EntityPickTable(std::uint32_t capacity = INT32_MAX)
            : m_Capacity((std::min)(capacity, std::uint32_t{INT32_MAX})) {}

        std::expected<std::int32_t, RenderEcsError> Encode(EntityRenderId id)
        {
            if (!id) return std::unexpected(RenderEcsError::StaleId);
            for (std::size_t i = 0; i < m_Ids.size(); ++i)
                if (m_Ids[i] == id) return static_cast<std::int32_t>(i);
            if (m_Ids.size() >= m_Capacity) return std::unexpected(RenderEcsError::PickCapacity);
            m_Ids.push_back(id);
            return static_cast<std::int32_t>(m_Ids.size() - 1);
        }

        std::expected<EntityRenderId, RenderEcsError> Decode(std::int32_t pixel) const
        {
            if (pixel < 0 || static_cast<std::size_t>(pixel) >= m_Ids.size())
                return std::unexpected(RenderEcsError::InvalidPick);
            return m_Ids[static_cast<std::size_t>(pixel)];
        }

    private:
        std::uint32_t m_Capacity;
        std::vector<EntityRenderId> m_Ids;
    };

    // Owner-thread facade over the scene's existing registry, not a second ECS.
    // Publish assets, finish authoring, then hold ExtractionScope for serial reads.
    // Finish the scope before structural edits or component replacement. Raw registry
    // references/legacy mutable component borrows must not cross this boundary.
    class RenderEcs
    {
        struct Identity { EntityRenderId id; };

    public:
        explicit RenderEcs(entt::registry& registry) : m_Registry(registry)
        {
            Asset::AssetDetail::RequireInvariant(!registry.ctx().contains<RenderEcs*>());
            registry.ctx().emplace<RenderEcs*>(this);
        }
        RenderEcs(const RenderEcs&) = delete;
        RenderEcs& operator=(const RenderEcs&) = delete;
        ~RenderEcs()
        {
            RequireMutable();
            m_Registry.clear<Identity>();
            m_Registry.ctx().erase<RenderEcs*>();
        }

        void RequireMutable() const
        {
            RequireOwner();
            Asset::AssetDetail::RequireInvariant(!m_Extracting);
        }

        class ExtractionScope
        {
        public:
            ExtractionScope(const ExtractionScope&) = delete;
            ExtractionScope& operator=(const ExtractionScope&) = delete;
            ExtractionScope(ExtractionScope&& other) noexcept
                : m_Owner(std::exchange(other.m_Owner, nullptr)) {}
            ExtractionScope& operator=(ExtractionScope&&) = delete;
            ~ExtractionScope()
            {
                if (m_Owner)
                {
                    m_Owner->RequireOwner();
                    m_Owner->m_Extracting = false;
                }
            }
        private:
            friend class RenderEcs;
            explicit ExtractionScope(RenderEcs& owner) : m_Owner(&owner) {}
            RenderEcs* m_Owner;
        };

        std::expected<ExtractionScope, RenderEcsError> BeginExtraction()
        {
            RequireOwner();
            if (m_Extracting) return std::unexpected(RenderEcsError::ExtractionActive);
            // Legacy scene duplication copies authoring data, never runtime identity.
            // Register those components before freezing, including camera/light-only entities.
            auto identify = [this]<Component::RenderDataComponent T>() -> std::expected<void, RenderEcsError>
            {
                for (auto entity : m_Registry.view<T>())
                    if (auto id = Identify(entity); !id) return std::unexpected(id.error());
                return {};
            };
            if (auto r = identify.template operator()<Component::MeshRendererComponent>(); !r) return std::unexpected(r.error());
            if (auto r = identify.template operator()<Component::VisibilityComponent>(); !r) return std::unexpected(r.error());
            if (auto r = identify.template operator()<Component::RenderCameraComponent>(); !r) return std::unexpected(r.error());
            if (auto r = identify.template operator()<Component::RenderLightComponent>(); !r) return std::unexpected(r.error());
            m_Extracting = true;
            return ExtractionScope(*this);
        }

        // Bridge from an existing scene entity. Identity is runtime state, never copied
        // with authoring components. A private registry component dies with the entity,
        // so even EnTT's finite version wrap cannot resurrect an old render ID.
        std::expected<EntityRenderId, RenderEcsError> Identify(entt::entity entity)
        {
            RequireOwner();
            if (!m_Registry.valid(entity)) return std::unexpected(RenderEcsError::InvalidEntity);
            if (const auto* identity = m_Registry.try_get<Identity>(entity)) return identity->id;
            if (m_Extracting) return std::unexpected(RenderEcsError::ExtractionActive);
            if (!m_NextGeneration) return std::unexpected(RenderEcsError::IdentityExhausted);
            if (!m_Domain)
            {
                auto domain = Asset::AssetDetail::TakeRegistryIdentity(Asset::AssetDetail::nextRegistryIdentity);
                if (!domain) return std::unexpected(RenderEcsError::IdentityExhausted);
                m_Domain = *domain;
            }
            const auto index = static_cast<std::uint32_t>(entt::to_entity(entity));
            if (m_Entities.size() <= index) m_Entities.resize(std::size_t{index} + 1, entt::null);
            const EntityRenderId id{index, m_NextGeneration, m_Domain};
            m_Registry.emplace<Identity>(entity, id);
            m_Entities[index] = entity;
            m_NextGeneration = m_NextGeneration == UINT64_MAX ? 0 : m_NextGeneration + 1;
            return id;
        }

        std::expected<entt::entity, RenderEcsError> Resolve(EntityRenderId id) const
        {
            RequireOwner();
            if (!id) return std::unexpected(RenderEcsError::StaleId);
            if (id.registry != m_Domain) return std::unexpected(RenderEcsError::ForeignId);
            if (id.index >= m_Entities.size()) return std::unexpected(RenderEcsError::StaleId);
            const auto entity = m_Entities[id.index];
            if (!m_Registry.valid(entity)) return std::unexpected(RenderEcsError::StaleId);
            const auto* identity = m_Registry.try_get<Identity>(entity);
            if (!identity || identity->id != id) return std::unexpected(RenderEcsError::StaleId);
            return entity;
        }

        std::expected<EntityRenderId, RenderEcsError> ResolvePick(const EntityPickTable& table, std::int32_t pixel) const
        {
            RequireOwner();
            auto id = table.Decode(pixel);
            if (!id) return std::unexpected(id.error());
            auto entity = Resolve(*id);
            if (!entity) return std::unexpected(entity.error());
            return *id;
        }

        // Minimal serial read boundary for consumers before RenderFrame extraction.
        // Traversal order is unspecified; consumers choose their own stable ordering.
        template<Component::RenderDataComponent T, class F>
            requires std::invocable<F&, EntityRenderId, T>
        std::expected<void, RenderEcsError> Visit(F&& visitor) const
        {
            RequireOwner();
            if (!m_Extracting) return std::unexpected(RenderEcsError::ExtractionRequired);
            for (auto [entity, identity, component] : m_Registry.view<const Identity, const T>().each())
            {
                (void)entity;
                std::invoke(visitor, identity.id, T(component));
            }
            return {};
        }

        template<Component::RenderDataComponent T>
        std::expected<T, RenderEcsError> Get(EntityRenderId id) const
        {
            auto entity = Resolve(id);
            if (!entity) return std::unexpected(entity.error());
            const auto* value = m_Registry.try_get<T>(*entity);
            if (!value) return std::unexpected(RenderEcsError::MissingComponent);
            return *value; // Value copy; no mutable borrow can escape the boundary.
        }

        template<Component::RenderDataComponent T>
        std::expected<void, RenderEcsError> Add(EntityRenderId id, const T& value = {})
        {
            auto entity = ForMutation(id);
            if (!entity) return std::unexpected(entity.error());
            if (m_Registry.all_of<T>(*entity)) return std::unexpected(RenderEcsError::DuplicateComponent);
            m_Registry.emplace<T>(*entity, value);
            return {};
        }

        template<Component::RenderDataComponent T>
        std::expected<void, RenderEcsError> Replace(EntityRenderId id, const T& value)
        {
            auto entity = ForMutation(id);
            if (!entity) return std::unexpected(entity.error());
            if (!m_Registry.all_of<T>(*entity)) return std::unexpected(RenderEcsError::MissingComponent);
            m_Registry.replace<T>(*entity, value);
            return {};
        }

        template<Component::RenderDataComponent T>
        std::expected<void, RenderEcsError> Remove(EntityRenderId id)
        {
            auto entity = ForMutation(id);
            if (!entity) return std::unexpected(entity.error());
            if (!m_Registry.all_of<T>(*entity)) return std::unexpected(RenderEcsError::MissingComponent);
            m_Registry.remove<T>(*entity);
            return {};
        }

    private:
        void RequireOwner() const
        { Asset::AssetDetail::RequireInvariant(std::this_thread::get_id() == m_Owner); }

        std::expected<entt::entity, RenderEcsError> ForMutation(EntityRenderId id) const
        {
            RequireOwner();
            if (m_Extracting) return std::unexpected(RenderEcsError::ExtractionActive);
            return Resolve(id);
        }

        entt::registry& m_Registry;
        const std::thread::id m_Owner = std::this_thread::get_id();
        std::uint64_t m_Domain = 0;
        std::uint64_t m_NextGeneration = 1;
        std::vector<entt::entity> m_Entities;
        bool m_Extracting = false;
    };
}
