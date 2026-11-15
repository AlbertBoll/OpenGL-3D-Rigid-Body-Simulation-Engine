#include "RbsStartup.h"
#include "RigidBodySimulation.h"
#include <sdl2/SDL.h>
#include <utility>

// Allocation observers remain defined exactly once in this entry translation
// unit; existing isolated qualification builds attach their flags here.
#define GENGINE_OWNER_REVIEW_ALLOC_IMPLEMENTATION
#include "../tests/OwnerReviewProbe.h"
#ifdef GENGINE_JOLT_FINAL_AUDIT
#define GENGINE_JOLT_FINAL_AUDIT_ALLOC_IMPLEMENTATION
#include "../tests/JoltFinalAudit.h"
#endif
#ifdef GENGINE_PHASE26_QUALIFICATION
#define GENGINE_PHASE26_ALLOC_IMPLEMENTATION
#include "../tests/DistanceJointChecks.h"
#endif
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include "../tests/SandboxChecks.h"
#endif

int main(int argc, char* args[])
{
    if (const auto diagnostic = Rbs::TryRunDiagnosticEntry(argc, args)) return *diagnostic;
    auto parsed = Rbs::ReadLaunchConfig(argc, args);
    if (!parsed)
    {
        Rbs::ReportStartupError(parsed.error());
        return 1;
    }
    return Rbs::RunApplication(std::move(*parsed), args[0]);
}
