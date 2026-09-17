#include "gepch.h"
#include "Core/RuntimeAssets.h"
#include <stdexcept>

namespace GEngine::RuntimeAssets
{
    static std::filesystem::path root;
    static std::string application;

    [[noreturn]] static void Fail(const std::string& resource, const std::string& reason)
    {
        throw std::runtime_error("Runtime assets [" + application + "]: " + reason +
            "\nResource: " + resource + "\nAsset root: " + root.string() +
            "\nRebuild the graphical target to stage its assets (or run its postbuild.py "
            "with config=Debug|Release). Set GENGINE_ASSET_ROOT only to an absolute "
            "staged asset-package directory.");
    }

    std::expected<std::string, PlatformError> TryFile(std::string_view relative)
    {
        const std::filesystem::path path(relative);
        if (root.empty()) return std::unexpected(PlatformError{PlatformErrorCode::ResourcePath,
            std::string(relative), "Runtime asset root was not initialized"});
        if (path.empty() || path.is_absolute() || path.has_root_name())
            return std::unexpected(PlatformError{PlatformErrorCode::ResourcePath,
                std::string(relative), "Expected a package-relative asset path"});
        for (const auto& part : path) if (part == "..")
            return std::unexpected(PlatformError{PlatformErrorCode::ResourcePath,
                std::string(relative), "Asset path escapes the package"});
        const auto resolved = (root / path).lexically_normal();
        std::error_code error;
        if (!std::filesystem::exists(resolved, error) || error)
            return std::unexpected(PlatformError{PlatformErrorCode::ResourcePath,
                resolved.string(), error ? error.message() : "Required platform asset is missing"});
        return resolved.string();
    }

    std::string File(std::string_view relative)
    {
        const std::filesystem::path path(relative);
        if (root.empty()) Fail(std::string(relative), "asset root was not initialized");
        if (path.empty() || path.is_absolute() || path.has_root_name())
            Fail(std::string(relative), "expected a package-relative asset path");
        for (const auto& part : path)
            if (part == "..") Fail(std::string(relative), "asset path escapes the package");
        return (root / path).lexically_normal().string();
    }

    void Initialize(const std::string& executableName)
    {
        application = executableName;
        if (application != "GEngineEditor" && application != "RigidBodySimulation" &&
            application != "Breakout" && application != "RayTracing")
            Fail(executableName, "unknown graphical application startup profile");

        if (const char* configured = SDL_getenv("GENGINE_ASSET_ROOT"))
        {
            root = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(configured)));
            if (root.empty() || !root.is_absolute())
                Fail(configured, "GENGINE_ASSET_ROOT must be a nonempty absolute path");
        }
        else
        {
            std::unique_ptr<char, decltype(&SDL_free)> base(SDL_GetBasePath(), SDL_free);
            if (!base) Fail("executable directory", SDL_GetError());
            const auto executableDirectory = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(base.get())));
            // SDL includes a trailing separator; normalize that before taking the parent.
            root = executableDirectory.parent_path().parent_path() / "assets";
        }
        root = root.lexically_normal();
        const auto manifest = File("startup/" + application + ".txt");
        std::ifstream input(std::filesystem::path(manifest), std::ios::binary);
        if (!input) Fail(manifest, "missing or unreadable startup dependency list");
        std::string line;
        if (!std::getline(input, line) || line != "GENGINE_STARTUP_ASSETS_V1 " + application)
            Fail(manifest, "invalid startup dependency list header");
        unsigned int count = 0;
        while (std::getline(input, line))
        {
            if (line.empty()) Fail(manifest, "empty startup dependency");
            const auto path = File(line);
            std::ifstream asset(std::filesystem::path(path), std::ios::binary);
            if (!asset || asset.peek() == std::ifstream::traits_type::eof())
                Fail(path, "missing, empty or unreadable required startup asset");
            ++count;
        }
        if (input.bad() || count == 0) Fail(manifest, "unreadable or empty startup dependency list");
        std::cout << "Runtime assets [" << application << "]: " << root.string()
                  << " (" << count << " startup dependencies)\n";
    }
}
