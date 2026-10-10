#include "gepch.h"
#include "Core/RuntimeAssets.h"
#include "Core/LaunchConfig.h"

namespace GEngine::RuntimeAssets
{
    static std::filesystem::path root;
    static std::string application;

    static PlatformError Failure(const std::string& app, const std::filesystem::path& assetRoot,
        const std::string& resource, const std::string& reason)
    {
        return {PlatformErrorCode::ResourcePath, resource, "Runtime assets [" + app + "]: " + reason +
            "\nResource: " + resource + "\nAsset root: " + assetRoot.string() +
            "\nRebuild the graphical target to stage its assets (or run its postbuild.py "
            "with config=Debug|Release). Set GENGINE_ASSET_ROOT only to an absolute "
            "staged asset-package directory."};
    }

    static std::expected<std::string, PlatformError> Resolve(const std::string& app,
        const std::filesystem::path& assetRoot, std::string_view relative)
    {
        const std::filesystem::path path(relative);
        if (assetRoot.empty()) return std::unexpected(Failure(app, assetRoot, std::string(relative), "asset root was not initialized"));
        if (path.empty() || path.is_absolute() || path.has_root_name())
            return std::unexpected(Failure(app, assetRoot, std::string(relative), "expected a package-relative asset path"));
        for (const auto& part : path) if (part == "..")
            return std::unexpected(Failure(app, assetRoot, std::string(relative), "asset path escapes the package"));
        const auto resolved = (assetRoot / path).lexically_normal();
        std::error_code error;
        if (!std::filesystem::exists(resolved, error) || error)
            return std::unexpected(Failure(app, assetRoot, resolved.string(), error ?
                "system-category=" + std::string(error.category().name()) + "; system-code=" + std::to_string(error.value()) + "; " + error.message() :
                "required platform asset is missing"));
        return resolved.string();
    }

    std::expected<std::string, PlatformError> ResolvePath(std::string_view relative)
    {
        const std::filesystem::path path(relative);
        if (root.empty()) return std::unexpected(Failure(application, root, std::string(relative), "asset root was not initialized"));
        if (path.empty() || path.is_absolute() || path.has_root_name())
            return std::unexpected(Failure(application, root, std::string(relative), "expected a package-relative asset path"));
        for (const auto& part : path) if (part == "..")
            return std::unexpected(Failure(application, root, std::string(relative), "asset path escapes the package"));
        return (root / path).lexically_normal().string();
    }

    std::expected<std::string, PlatformError> TryFile(std::string_view relative)
    {
        return Resolve(application, root, relative);
    }

    namespace
    {
        PlatformResult InitializePackage(const std::string& executableName, const char* configured);
    }

    PlatformResult Initialize(const std::string& executableName)
    {
        return InitializePackage(executableName, SDL_getenv("GENGINE_ASSET_ROOT"));
    }

    PlatformResult Initialize(const std::string& executableName,
                              const EngineLaunchConfig& launchConfig)
    {
        return InitializePackage(executableName, launchConfig.assetRootUtf8
                                                     ? launchConfig.assetRootUtf8->c_str()
                                                     : nullptr);
    }

    namespace
    {
        PlatformResult InitializePackage(const std::string& executableName, const char* configured)
        {
            std::filesystem::path candidate;
            const auto fail = [&](const std::string& resource, const std::string& reason)
            {
                return std::unexpected(Failure(executableName, candidate, resource, reason));
            };
            if (executableName != "GEngineEditor" && executableName != "RigidBodySimulation" &&
                executableName != "Breakout" && executableName != "RayTracing")
                return fail(executableName, "unknown graphical application startup profile");

            if (configured)
            {
                candidate = std::filesystem::path(
                    std::u8string_view(reinterpret_cast<const char8_t*>(configured)));
                if (candidate.empty() || !candidate.is_absolute())
                    return fail(configured, "GENGINE_ASSET_ROOT must be a nonempty absolute path");
            }
            else
            {
                std::unique_ptr<char, decltype(&SDL_free)> base(SDL_GetBasePath(), SDL_free);
                if (!base)
                    return fail("executable directory", SDL_GetError());
                const auto executableDirectory = std::filesystem::path(
                    std::u8string_view(reinterpret_cast<const char8_t*>(base.get())));
                // SDL includes a trailing separator; normalize that before taking the parent.
                candidate = executableDirectory.parent_path().parent_path() / "assets";
            }
            candidate = candidate.lexically_normal();
            const auto manifest =
                Resolve(executableName, candidate, "startup/" + executableName + ".txt");
            if (!manifest)
                return std::unexpected(manifest.error());
            std::ifstream input(std::filesystem::path(*manifest), std::ios::binary);
            if (!input)
                return fail(*manifest, "missing or unreadable startup dependency list");
            std::string line;
            if (!std::getline(input, line) || line != "GENGINE_STARTUP_ASSETS_V1 " + executableName)
                return fail(*manifest, "invalid startup dependency list header");
            unsigned int count = 0;
            while (std::getline(input, line))
            {
                if (line.empty())
                    return fail(*manifest, "empty startup dependency");
                const auto path = Resolve(executableName, candidate, line);
                if (!path)
                    return std::unexpected(path.error());
                std::ifstream asset(std::filesystem::path(*path), std::ios::binary);
                if (!asset || asset.peek() == std::ifstream::traits_type::eof())
                    return fail(*path, "missing, empty or unreadable required startup asset");
                ++count;
            }
            if (input.bad() || count == 0)
                return fail(*manifest, "unreadable or empty startup dependency list");
            root = std::move(candidate);
            application = executableName;
            std::cout << "Runtime assets [" << application << "]: " << root.string() << " ("
                      << count << " startup dependencies)\n";
            return {};
        }
    }
}
