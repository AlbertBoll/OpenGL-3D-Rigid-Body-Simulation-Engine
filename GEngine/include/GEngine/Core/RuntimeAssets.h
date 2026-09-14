#pragma once

#include <string>
#include <string_view>

namespace GEngine::RuntimeAssets
{
    // Called once by the graphical entry point, before application/platform initialization.
    void Initialize(const std::string& executableName);
    std::string File(std::string_view relative);

    // Existing prefix+filename expressions resolve lazily, after Initialize.
    struct Directory
    {
        const char* relative;
        std::string operator+(std::string_view suffix) const
        {
            return File(std::string(relative) + std::string(suffix));
        }
    };
}
