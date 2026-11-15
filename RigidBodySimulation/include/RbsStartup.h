#pragma once

#include "RbsLaunchConfig.h"
#include <cstdio>
#include <expected>
#include <new>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace Rbs
{
    enum class DiagnosticWriteError { Output, Allocation, Format, Capacity };

    // Bootstrap diagnostics are best-effort. Keep the throwing standard-library
    // operation at one boundary, separate from actual startup failures. Restrict
    // arguments to built-in formatters: no user formatter/conversion can throw
    // an unrelated exception through this nonthrowing reporting API.
    template<class... Args>
        requires ((std::is_arithmetic_v<std::remove_cvref_t<Args>> ||
            std::is_same_v<std::decay_t<Args>, const char*> ||
            std::is_same_v<std::decay_t<Args>, char*>) && ...)
    [[nodiscard]] std::expected<void, DiagnosticWriteError> TryPrintDiagnosticLine(
        std::FILE* stream, std::format_string<Args...> format, Args&&... args) noexcept
    {
        try
        {
            std::println(stream, format, std::forward<Args>(args)...);
            return {};
        }
        catch (const std::system_error&) { return std::unexpected(DiagnosticWriteError::Output); }
        catch (const std::bad_alloc&) { return std::unexpected(DiagnosticWriteError::Allocation); }
        catch (const std::format_error&) { return std::unexpected(DiagnosticWriteError::Format); }
        catch (const std::length_error&) { return std::unexpected(DiagnosticWriteError::Capacity); }
    }

    // A handled diagnostic returns its existing process result; otherwise startup continues.
    [[nodiscard]] std::optional<int> TryRunDiagnosticEntry(int argc, char* const args[]);
    void ReportStartupError(const std::string& message) noexcept;
    int RunApplication(LaunchConfig launch, const char* executablePath);
}
