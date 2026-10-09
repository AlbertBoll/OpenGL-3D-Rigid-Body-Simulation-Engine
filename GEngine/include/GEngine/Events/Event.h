#pragma once
#include "Assets/AssetHandle.h"
#include "Core/Log.h"
#include <atomic>
#include <expected>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace GEngine
{
#define BIND_EVENT_FN(fn) [this](auto&& ... args)->decltype(auto){return this->fn(std::forward<decltype(args)>(args)...); }

    enum class SubscriptionError { Allocation, IdentityExhausted, WrongThread, Closed,
        StaleTarget, DepthLimit, Full, PayloadTooLarge, ReentrantDrain };
    using SubscriptionResult = std::expected<void, SubscriptionError>;
    struct SubscriptionTag;
    using SubscriptionIdentity = Asset::AssetHandle<SubscriptionTag>;

    inline void ReportSubscriptionError(SubscriptionError error)
    {
        if (auto logger = Log::GetCoreLogger())
            logger->error("Subscription operation rejected: {}", static_cast<int>(error));
    }
    // Opt-in owner-thread observations cover provider storage, not callback allocations.
    struct SubscriptionObservations
    { std::uint64_t allocations{}, allocatedBytes{}, lookups{}, callbacks{}; };

    namespace SubscriptionDetail
    {
        template<class T> inline const char Type = 0;
        struct Callback
        {
            virtual ~Callback() = default;
            virtual void Invoke(void*) = 0;
        };
        template<class Tuple, class F> struct Callable final : Callback
        {
            F function;
            explicit Callable(F&& value) noexcept : function(std::move(value)) {}
            void Invoke(void* value) override { std::apply(function, *static_cast<Tuple*>(value)); }
        };
        struct Slot
        {
            Slot* next{};
            Callback* callback{};
            std::uint32_t index{};
            std::uint64_t generation = 1, sequence{};
            unsigned executing{};
            bool active{};
            ~Slot() { delete callback; }
        };
        // Intrusive retention lets tokens/queued targets outlive the publisher.
        // All slot access is owner-thread-only; workers only retain the state.
        struct State
        {
            std::atomic<unsigned> references{1};
            std::atomic<bool> closed{false};
            const std::thread::id owner = std::this_thread::get_id();
            const std::uint64_t domain;
            Slot* first{}, *last{};
            std::uint64_t nextSequence = 1, cutoff{};
            std::uint32_t slots{};
            unsigned depth{};
            SubscriptionObservations* observations{};
            explicit State(std::uint64_t identity) : domain(identity) {}
            ~State() { while (first) { auto* next = first->next; delete first; first = next; } }
            void Retain() noexcept { references.fetch_add(1, std::memory_order_relaxed); }
            void Drop() noexcept { if (references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this; }
            bool IsOwner() const noexcept { return owner == std::this_thread::get_id(); }
            void ObserveAllocation(std::size_t bytes)
            { if (observations) { ++observations->allocations; observations->allocatedBytes += bytes; } }
            Slot* Find(SubscriptionIdentity id)
            {
                if (id.registry != domain) return nullptr;
                for (auto* slot = first; slot; slot = slot->next)
                {
                    if (observations) ++observations->lookups;
                    if (slot->index == id.index)
                        return slot->active && slot->generation == id.generation ? slot : nullptr;
                }
                return nullptr;
            }
            SubscriptionResult Release(SubscriptionIdentity id)
            {
                if (closed.load(std::memory_order_acquire)) return {};
                if (!IsOwner()) return std::unexpected(SubscriptionError::WrongThread);
                if (auto* slot = Find(id))
                {
                    slot->active = false;
                    if (!slot->executing) delete std::exchange(slot->callback, nullptr);
                }
                return {};
            }
            void Close() noexcept
            {
                if (closed.load(std::memory_order_acquire)) return;
                Asset::AssetDetail::RequireInvariant(IsOwner());
                closed.store(true, std::memory_order_release);
                for (auto* slot = first; slot; slot = slot->next)
                {
                    slot->active = false;
                    if (!slot->executing) delete std::exchange(slot->callback, nullptr);
                }
                observations = nullptr;
            }
            std::expected<SubscriptionIdentity, SubscriptionError> Add(Callback* callback)
            {
                std::unique_ptr<Callback> pending(callback);
                if (!IsOwner()) return std::unexpected(SubscriptionError::WrongThread);
                if (closed.load(std::memory_order_acquire)) return std::unexpected(SubscriptionError::Closed);
                if (!nextSequence) return std::unexpected(SubscriptionError::IdentityExhausted);
                Slot* slot = nullptr;
                // Recycling outside dispatch preserves traversal links; recycled
                // slots are appended to retain registration order.
                if (!depth)
                {
                    Slot* previous = nullptr;
                    for (auto* candidate = first; candidate; previous = candidate, candidate = candidate->next)
                        if (!candidate->active && candidate->generation != UINT64_MAX)
                        {
                            slot = candidate;
                            if (previous) previous->next = slot->next; else first = slot->next;
                            if (last == slot) last = previous;
                            ++slot->generation; break;
                        }
                }
                if (!slot)
                {
                    if (slots == SubscriptionIdentity::NullIndex)
                        return std::unexpected(SubscriptionError::IdentityExhausted);
                    slot = new (std::nothrow) Slot;
                    if (!slot) return std::unexpected(SubscriptionError::Allocation);
                    ObserveAllocation(sizeof(Slot)); slot->index = slots++;
                }
                slot->next = nullptr; slot->sequence = nextSequence++;
                slot->callback = pending.release(); slot->active = true;
                if (last) last->next = slot; else first = slot;
                last = slot;
                return SubscriptionIdentity{slot->index, slot->generation, domain};
            }
            SubscriptionResult Deliver(void* arguments, SubscriptionIdentity target = {})
            {
                if (!IsOwner()) return std::unexpected(SubscriptionError::WrongThread);
                if (closed.load(std::memory_order_acquire)) return std::unexpected(SubscriptionError::Closed);
                if (depth == 16) return std::unexpected(SubscriptionError::DepthLimit);
                auto* selected = target ? Find(target) : nullptr;
                if (target && !selected) return std::unexpected(SubscriptionError::StaleTarget);
                if (!depth) cutoff = nextSequence - 1;
                if (selected && selected->sequence > cutoff) return std::unexpected(SubscriptionError::StaleTarget);
                Retain(); ++depth;
                struct Exit { State* state; ~Exit() { --state->depth; state->Drop(); } } exit{this};
                for (auto* slot = selected ? selected : first; slot; slot = slot->next)
                {
                    if (observations) ++observations->lookups;
                    if (slot->active && slot->sequence <= cutoff)
                    {
                        ++slot->executing;
                        if (observations) ++observations->callbacks;
                        slot->callback->Invoke(arguments);
                        --slot->executing;
                        if (!slot->active && !slot->executing) delete std::exchange(slot->callback, nullptr);
                    }
                    if (selected) break;
                }
                return {};
            }
        };
        class StateRef
        {
            State* value{};
        public:
            StateRef() = default;
            explicit StateRef(State* state) : value(state) { if (value) value->Retain(); }
            StateRef(const StateRef& other) : StateRef(other.value) {}
            StateRef(StateRef&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
            StateRef& operator=(StateRef other) noexcept { std::swap(value, other.value); return *this; }
            ~StateRef() { if (value) value->Drop(); }
            State* Get() const { return value; }
            State* operator->() const { return value; }
            explicit operator bool() const { return value != nullptr; }
        };
    }

    template<class> class TypedSubscriptions;
    template<class T> class CompletionTarget;

    class Subscription
    {
        template<class> friend class TypedSubscriptions;
        SubscriptionDetail::StateRef state;
        SubscriptionIdentity identity;
        Subscription(SubscriptionDetail::State* value, SubscriptionIdentity id) : state(value), identity(id) {}
        void ResetInvariant() noexcept { Asset::AssetDetail::RequireInvariant(bool(Reset())); }
    public:
        Subscription() = default;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept : state(std::move(other.state)), identity(std::exchange(other.identity, {})) {}
        Subscription& operator=(Subscription&& other) noexcept
        {
            if (this != &other) { ResetInvariant(); state = std::move(other.state); identity = std::exchange(other.identity, {}); }
            return *this;
        }
        ~Subscription() { ResetInvariant(); }
        SubscriptionIdentity Identity() const { return identity; }
        explicit operator bool() const { return bool(identity) && state && !state->closed.load(std::memory_order_acquire); }
        SubscriptionResult Reset()
        {
            if (state) { auto released = state->Release(identity); if (!released) return released; }
            state = {}; identity = {}; return {};
        }
    };

    template<class R, class... Args> class TypedSubscriptions<R(Args...)>
    {
        using Tuple = std::tuple<Args...>;
        std::thread::id owner = std::this_thread::get_id();
        SubscriptionDetail::StateRef state;
        SubscriptionResult Ensure()
        {
            if (owner != std::this_thread::get_id()) return std::unexpected(SubscriptionError::WrongThread);
            if (state) return {};
            auto domain = Asset::AssetDetail::TakeRegistryIdentity(Asset::AssetDetail::nextRegistryIdentity);
            if (!domain) return std::unexpected(SubscriptionError::IdentityExhausted);
            auto* created = new (std::nothrow) SubscriptionDetail::State(*domain);
            if (!created) return std::unexpected(SubscriptionError::Allocation);
            state = SubscriptionDetail::StateRef(created); created->Drop(); return {};
        }
    public:
        TypedSubscriptions() = default;
        TypedSubscriptions(const TypedSubscriptions&) = delete;
        TypedSubscriptions& operator=(const TypedSubscriptions&) = delete;
        TypedSubscriptions(TypedSubscriptions&&) noexcept = default;
        TypedSubscriptions& operator=(TypedSubscriptions&& other) noexcept
        { if (this != &other) { Close(); owner = other.owner; state = std::move(other.state); } return *this; }
        ~TypedSubscriptions() { Close(); }
        void Close() noexcept { if (state) state->Close(); }
        template<class F> requires std::is_nothrow_move_constructible_v<std::decay_t<F>> && std::is_invocable_r_v<R, F&, Args...>
        std::expected<Subscription, SubscriptionError> Subscribe(F function)
        {
            if (auto ready = Ensure(); !ready) return std::unexpected(ready.error());
            using Stored = SubscriptionDetail::Callable<Tuple, std::decay_t<F>>;
            auto* callback = new (std::nothrow) Stored(std::move(function));
            if (!callback) return std::unexpected(SubscriptionError::Allocation);
            state->ObserveAllocation(sizeof(Stored));
            auto added = state->Add(callback);
            if (!added) return std::unexpected(added.error());
            return Subscription(state.Get(), *added);
        }
        SubscriptionResult Dispatch(Args... args)
        {
            if (owner != std::this_thread::get_id()) return std::unexpected(SubscriptionError::WrongThread);
            if (!state) return {};
            auto retained = state; Tuple arguments(args...); return retained->Deliver(&arguments);
        }
        SubscriptionResult Release(SubscriptionIdentity id)
        {
            if (owner != std::this_thread::get_id()) return std::unexpected(SubscriptionError::WrongThread);
            return state ? state->Release(id) : SubscriptionResult{};
        }
        bool HasListeners() const
        {
            Asset::AssetDetail::RequireInvariant(owner == std::this_thread::get_id());
            if (state) for (auto* slot = state->first; slot; slot = slot->next) if (slot->active) return true;
            return false;
        }
        SubscriptionResult Observe(SubscriptionObservations* observer)
        {
            if (auto ready = Ensure(); !ready) return ready;
            state->observations = observer; return {};
        }
        template<class T> requires std::is_same_v<R(Args...), void(const T&)>
        std::expected<CompletionTarget<T>, SubscriptionError> Target(const Subscription& token)
        {
            if (owner != std::this_thread::get_id()) return std::unexpected(SubscriptionError::WrongThread);
            if (!state || token.state.Get() != state.Get() || !state->Find(token.identity))
                return std::unexpected(SubscriptionError::StaleTarget);
            return CompletionTarget<T>(state.Get(), token.identity);
        }
    };

    template<class T> class CompletionTarget
    {
        template<class> friend class TypedSubscriptions;
        friend class CompletionQueue;
        SubscriptionDetail::StateRef state;
        SubscriptionIdentity identity;
        CompletionTarget(SubscriptionDetail::State* value, SubscriptionIdentity id) : state(value), identity(id) {}
    public:
        CompletionTarget() = default;
        SubscriptionIdentity Identity() const { return identity; }
        bool Closed() const { return !state || state->closed.load(std::memory_order_acquire); }
        SubscriptionResult Deliver(const T& value) const
        {
            if (Closed()) return std::unexpected(SubscriptionError::Closed);
            std::tuple<const T&> arguments(value); return state->Deliver(&arguments, identity);
        }
    };

    template<class Signature> struct EventKey { const char* name; };
    struct RoutedEvent { std::string_view name; const void* signature; void* arguments; };
    class IEvent
    {
    public:
        virtual const std::string& GetName() const = 0;
        virtual void Route(const RoutedEvent&) = 0;
        virtual ~IEvent() = default;
    };
    template<class> class Events;
    template<class R, class... Args> class Events<R(Args...)> : public IEvent
    {
        std::string name;
        TypedSubscriptions<R(Args...)> source;
        struct Legacy { Subscription token; Legacy* next{}; };
        Legacy* legacy{};
    public:
        Events() = default;
        explicit Events(const std::string& eventName) : name(eventName) {}
        ~Events() { while (legacy) { auto* next = legacy->next; delete legacy; legacy = next; } }
        const std::string& GetName() const override { return name; }
        void Reserve(std::size_t) {} // Linked slots do not invalidate callback storage.
        SubscriptionIdentity Subscribe(std::function<R(Args...)> function)
        {
            auto token = source.Subscribe(std::move(function));
            if (!token) { ReportSubscriptionError(token.error()); return {}; }
            auto* stored = new (std::nothrow) Legacy{std::move(*token), legacy};
            if (!stored) { ReportSubscriptionError(SubscriptionError::Allocation); return {}; }
            legacy = stored; return stored->token.Identity();
        }
        void Unsubscribe(SubscriptionIdentity id)
        { if (auto result = source.Release(id); !result) ReportSubscriptionError(result.error()); }
        void Fire(Args... args)
        { if (auto result = source.Dispatch(args...); !result) ReportSubscriptionError(result.error()); }
        void operator()(Args... args) { Fire(args...); }
        void Route(const RoutedEvent& event) override
        {
            if (event.name == name && event.signature == &SubscriptionDetail::Type<R(Args...)>)
                std::apply([this](auto&&... args) { Fire(args...); }, *static_cast<std::tuple<Args...>*>(event.arguments));
        }
    };

    class EventDispatcher
    {
        TypedSubscriptions<void(const RoutedEvent&)> source;
        struct Group { IEvent* event; Subscription token; Group* next{}; };
        Group* groups{};
    public:
        EventDispatcher() = default;
        EventDispatcher(const EventDispatcher&) = delete;
        EventDispatcher& operator=(const EventDispatcher&) = delete;
        ~EventDispatcher() { while (groups) { auto* next = groups->next; delete groups; groups = next; } }
        template<class R, class... Args, class F>
        std::expected<Subscription, SubscriptionError> Subscribe(EventKey<R(Args...)> key, F function)
        {
            // EventKey names have static lifetime (the semantic manager keys).
            return source.Subscribe([key, function = std::move(function)](const RoutedEvent& event) mutable {
                if (event.name == key.name && event.signature == &SubscriptionDetail::Type<R(Args...)>)
                    std::apply(function, *static_cast<std::tuple<Args...>*>(event.arguments));
            });
        }
        template<class R, class... Args>
        SubscriptionResult Dispatch(EventKey<R(Args...)> key, std::type_identity_t<Args>... args)
        {
            std::tuple<Args...> arguments(args...);
            return source.Dispatch(RoutedEvent{key.name, &SubscriptionDetail::Type<R(Args...)>, &arguments});
        }
        void RegisterEvent(IEvent* events)
        {
            if (!events) return;
            auto token = source.Subscribe([owned = std::unique_ptr<IEvent>(events)](const RoutedEvent& event) { owned->Route(event); });
            if (!token) { ReportSubscriptionError(token.error()); return; }
            auto* group = new (std::nothrow) Group{events, std::move(*token), groups};
            if (!group) { ReportSubscriptionError(SubscriptionError::Allocation); return; }
            groups = group;
        }
        void UnregisterEvent(const std::string& name)
        {
            for (auto** link = &groups; *link;)
                if ((*link)->event->GetName() == name)
                { auto* removed = *link; *link = removed->next; delete removed; }
                else link = &(*link)->next;
        }
        template<class R = void, class... Args> void DispatchEvent(const std::string& name, Args&&... args)
        {
            auto result = Dispatch(EventKey<R(std::decay_t<Args>...)>{name.c_str()}, std::forward<Args>(args)...);
            if (!result) ReportSubscriptionError(result.error());
        }
    };
}
