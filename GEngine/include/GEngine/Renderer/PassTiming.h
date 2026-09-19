#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace GEngine
{
    enum class RenderPass;
    enum class TimingError { Context, Allocation, Driver, Output, FrameActive };
    enum class GpuTiming { Pending, Available, Unsupported, PoolExhausted, NotMeasured, Abandoned };
    enum class TimingCounts { Explicit, EngineCalls, Unavailable };
    std::string_view PassLabel(RenderPass) noexcept;
    std::string_view TimingLabel(GpuTiming) noexcept;
    struct PassTimingSample
    {
        std::uint64_t frame{}, collectedFrame{};
        RenderPass pass{};
        std::uint64_t cpuNanoseconds{}, submittedItems{}, submittedDraws{};
        std::optional<std::uint64_t> gpuNanoseconds;
        GpuTiming gpu = GpuTiming::NotMeasured;
        bool itemsKnown = true, drawsKnown = true, completed = false;
    };
    // Opt-in, bounded, move-only context owner. Reset before its creating window
    // destroys the context. No query results are fetched at pass/frame end.
    class PassTiming final
    {
    public:
        static constexpr std::size_t PoolCapacity = 64, FrameCapacity = 32;
        static constexpr std::uint64_t CollectionDelay = 2;
        PassTiming();
        ~PassTiming();
        PassTiming(PassTiming&&) noexcept;
        PassTiming& operator=(PassTiming&&) noexcept;
        PassTiming(const PassTiming&) = delete;
        PassTiming& operator=(const PassTiming&) = delete;
        [[nodiscard]] std::expected<void, TimingError> Initialize(bool enabled, const char* output = nullptr);
        void Reset() noexcept;
        [[nodiscard]] std::expected<void, TimingError> BeginFrame();
        void EndFrame() noexcept;
        bool Enabled() const noexcept;
        bool Active() const noexcept;
        bool OutputHealthy() const noexcept;
        std::span<const PassTimingSample> Current() const noexcept;
        // Completed GPU samples collected at the most recent BeginFrame, carrying
        // their original frame/pass/CPU/count data. Pending is never a zero time.
        std::span<const PassTimingSample> Collected() const noexcept;
        class Scope final
        {
        public:
            explicit Scope(RenderPass); // Uses the scheduler's active owner.
            Scope(PassTiming&, RenderPass, TimingCounts = TimingCounts::Explicit, bool gpu = true);
            ~Scope();
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;
            void Complete() noexcept { completed = true; }
        private:
            friend class PassTiming;
            PassTiming* owner{};
            Scope* previous{};
            std::size_t sample{}, slot = PoolCapacity;
            TimingCounts counts = TimingCounts::Explicit;
            std::uint64_t initialDraws{};
            std::chrono::steady_clock::time_point start;
            bool completed = false;
        };
        class Activation final
        {
        public:
            explicit Activation(PassTiming&) noexcept;
            ~Activation();
            Activation(const Activation&) = delete;
            Activation& operator=(const Activation&) = delete;
        private:
            PassTiming* previous;
        };
        static void Submitted(std::uint64_t items, std::uint64_t draws) noexcept;
    private:
        struct Storage;
        std::unique_ptr<Storage> storage;
        static thread_local PassTiming* active;
        static thread_local Scope* scope;
    };
}
