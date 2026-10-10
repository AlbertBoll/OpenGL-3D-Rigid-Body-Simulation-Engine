#pragma once
#include "Core/Platform.h"
#include <string>
#include <string_view>

namespace GEngine
{
    struct EngineLaunchConfig;
}

namespace GEngine::RuntimeAssets
{
    // Validate the complete startup package before publishing a new root.
    // Failure leaves any previously initialized root usable.
    [[nodiscard]] PlatformResult Initialize(const std::string& executableName);
    [[nodiscard]] PlatformResult Initialize(const std::string& executableName,
                                            const EngineLaunchConfig& launchConfig);
    // Resolve a package-relative name without requiring existence. Optional asset
    // consumers retain their explicit logged fallback policy after resolution.
    [[nodiscard]] std::expected<std::string, PlatformError> ResolvePath(std::string_view relative);
    // Files and package directories share the same checked relative-path contract.
    [[nodiscard]] std::expected<std::string, PlatformError> TryFile(std::string_view relative);
}
