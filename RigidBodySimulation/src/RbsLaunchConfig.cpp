#include "RbsLaunchConfig.h"
#include <algorithm>

namespace Rbs
{
    bool ValidationProfile::Includes(ValidationCheck check) const noexcept
    {
        return std::find(checks.begin(), checks.end(), check) != checks.end();
    }

    bool LaunchConfig::AsyncPerformanceEnabled() const noexcept
    {
        return validation.asyncPerformance.has_value();
    }
}
