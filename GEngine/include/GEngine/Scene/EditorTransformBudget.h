#pragma once
#include <cstddef>
namespace GEngine::EditorTransformBudget
{
    // Interactive admission budgets, independent of selection and solver criteria.
    // 4096 matches the current Jolt body owner capacity. Scratch grows on demand;
    // it is not a 4096-entry allocation on every mouse event or history slot.
    inline constexpr std::size_t Roots=4096;
    inline constexpr std::size_t NarrowQueries=131072; // 32 exact queries/body at maximum size.
    inline constexpr std::size_t BroadphasePairs=4*1024*1024;
    inline constexpr std::size_t HistoryBytes=8*1024*1024;
    inline constexpr std::size_t ColliderPoints=262144; // Aggregate staged hull/box vertices.
}
