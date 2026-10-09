#pragma once
#include "Events/Event.h"
#include "Managers/ManagerBase.h"

#include "Core/Platform.h"
#include "Inputs/KeyCodes.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>

namespace GEngine
{
    struct CompletionObservations
    {
        std::uint64_t posted{}, examined{}, delivered{}, stale{}, cancelled{}, copies{}, moves{};
        std::size_t backlog{}, peakBacklog{};
    };

    // One bounded mailbox, with no worker execution or blocking capacity wait.
    // The caller owns producer lifetime and joins workers before queue destruction.
    class CompletionQueue
    {
        struct Entry
        {
            SubscriptionDetail::StateRef state;
            SubscriptionIdentity identity;
            SubscriptionResult (*invoke)(Entry&){};
            alignas(std::max_align_t) std::array<std::byte, 256> payload{};
            bool cancelled{};
        };
        std::array<Entry, 256> entries;
        std::mutex mutex;
        std::size_t head{}, count{};
        const std::thread::id owner = std::this_thread::get_id();
        bool closed{}, draining{}, observing{};
        CompletionObservations observations;
    public:
        static constexpr std::size_t Capacity = 256, DrainLimit = 64, PayloadBytes = 256;
        CompletionQueue() = default;
        CompletionQueue(const CompletionQueue&) = delete;
        CompletionQueue& operator=(const CompletionQueue&) = delete;
        ~CompletionQueue() { Close(); }

        // T must contain only owned values. Raw pointers/references are not payloads;
        // composite value types must likewise not hide borrowed storage.
        template<class T> requires std::is_trivially_copyable_v<T> && (!std::is_pointer_v<T>)
        SubscriptionResult Post(const CompletionTarget<T>& target, const T& value)
        {
            if constexpr (sizeof(T) > PayloadBytes || alignof(T) > alignof(std::max_align_t))
                return std::unexpected(SubscriptionError::PayloadTooLarge);
            else
            {
                std::lock_guard lock(mutex);
                if (closed) return std::unexpected(SubscriptionError::Closed);
                if (target.Closed()) return std::unexpected(SubscriptionError::StaleTarget);
                if (count == Capacity) return std::unexpected(SubscriptionError::Full);
                auto& entry = entries[(head + count) % Capacity];
                entry.state = target.state; entry.identity = target.identity; entry.cancelled = false;
                std::memcpy(entry.payload.data(), &value, sizeof(T));
                entry.invoke = [](Entry& stored) -> SubscriptionResult {
                    // memcpy creates the implicit-lifetime trivially-copyable value.
                    const auto& payload = *reinterpret_cast<const T*>(stored.payload.data());
                    std::tuple<const T&> arguments(payload);
                    return stored.state->Deliver(&arguments, stored.identity);
                };
                ++count;
                if (observing) { ++observations.posted; ++observations.copies;
                    observations.peakBacklog = (std::max)(observations.peakBacklog, count); }
                return {};
            }
        }
        std::size_t Cancel(SubscriptionIdentity id)
        {
            std::lock_guard lock(mutex);
            std::size_t cancelled = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                auto& entry = entries[(head + i) % Capacity];
                if (entry.identity == id && !entry.cancelled) { entry.cancelled = true; ++cancelled; }
            }
            return cancelled;
        }
        std::expected<CompletionObservations, SubscriptionError> Drain()
        {
            if (owner != std::this_thread::get_id()) return std::unexpected(SubscriptionError::WrongThread);
            if (draining) return std::unexpected(SubscriptionError::ReentrantDrain);
            std::size_t budget;
            { std::lock_guard lock(mutex); if (closed) return std::unexpected(SubscriptionError::Closed);
                budget = (std::min)(count, DrainLimit); }
            draining = true;
            struct Exit { bool& flag; ~Exit() { flag = false; } } exit{draining};
            CompletionObservations result;
            for (std::size_t i = 0; i < budget; ++i)
            {
                Entry entry;
                { std::lock_guard lock(mutex);
                    if (!count || closed) break;
                    entry = std::move(entries[head]); entries[head] = {};
                    head = (head + 1) % Capacity; --count;
                    if (observing) ++observations.moves; }
                ++result.examined;
                if (entry.cancelled) { ++result.cancelled; continue; }
                auto delivered = entry.invoke(entry);
                if (delivered) ++result.delivered;
                else if (delivered.error() == SubscriptionError::StaleTarget || delivered.error() == SubscriptionError::Closed)
                    ++result.stale;
                else return std::unexpected(delivered.error());
            }
            { std::lock_guard lock(mutex); result.backlog = count;
                if (observing) { observations.examined += result.examined; observations.delivered += result.delivered;
                    observations.stale += result.stale; observations.cancelled += result.cancelled; } }
            return result;
        }
        void Close()
        {
            std::lock_guard lock(mutex); closed = true;
            for (auto& entry : entries) entry = {};
            count = 0;
        }
        void Observe(bool enabled)
        { std::lock_guard lock(mutex); observing = enabled; observations = {}; }
        CompletionObservations Observations()
        { std::lock_guard lock(mutex); auto result = observations; result.backlog = count; return result; }
    };

	namespace Manager
	{
        struct KeyboardParam { Input::Key::GEngineKeyCode Key; bool Repeated = false; };
        struct MouseScrollWheelParam
		{
			unsigned int ID;
			float X;
			float Y;
		};


		struct MouseMoveParam
		{
			unsigned int ID;
			int32_t XPos;
			int32_t YPos;
			int32_t XRel;
			int32_t YRel;
		};

		struct MouseButtonParam
		{

			unsigned int ID;
			int32_t X;
			int32_t Y;
			uint8_t Button;
			uint8_t Clicks;
		};

		struct WindowCloseParam
		{

			unsigned int ID;
		};


		struct WindowResizeParam
		{
			unsigned int ID;
			int Width;
			int Height;
		};

        namespace Event
        {
            inline constexpr EventKey<void()> AppClose{"AppClose"}, AppPause{"AppPause"},
                AppResume{"AppResume"}, DebugShow{"DebugShow"}, ViewportChange{"ViewportChange"};
            inline constexpr EventKey<void(MouseScrollWheelParam)> MouseScrollWheel{"MouseScrollWheel"};
            inline constexpr EventKey<void(MouseMoveParam)> MouseMove{"MouseMove"};
            inline constexpr EventKey<void(MouseButtonParam)> MouseButtonPress{"MouseButtonPress"};
            inline constexpr EventKey<void(WindowCloseParam)> WindowClose{"WindowClose"};
            inline constexpr EventKey<void(WindowResizeParam)> WindowResize{"WindowResize"};
            inline constexpr EventKey<void(WindowStateEvent)> WindowState{"WindowState"};
        }


		class EventManager : ManagerBase<EventManager>
		{
			friend class ManagerBase<EventManager>;


		public:
			EventManager() = default;
			static ScopedPtr<EventManager> GetScopedInstance();
			void Initialize();
			void PollEvents();
			EventDispatcher& GetEventDispatcher() { return m_EventDispatcher; }
            CompletionQueue& Completions() { return m_Completions; }
            template<class Signature, class F>
            auto Subscribe(EventKey<Signature> event, F callback)
            { return m_EventDispatcher.Subscribe(event, std::move(callback)); }

		private:
			friend class GEngine;
			//EventManager() = default;

		private:
			EventDispatcher m_EventDispatcher;
            CompletionQueue m_Completions;
            Subscription m_MouseMoveConnection;


		};

	}

}
