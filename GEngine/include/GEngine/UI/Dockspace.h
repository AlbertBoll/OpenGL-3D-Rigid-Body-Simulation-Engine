#pragma once

#include "Core/Platform.h"

namespace GEngine::UI
{
    struct DockspaceHostDesc
    {
        const char* windowName;
        const char* dockspaceName;
    };

    // UI/context thread only, inside an active UI frame. Names are borrowed for
    // this call and must remain stable across sessions for layout restoration.
    // Submit before all hosted panels; pair each successful begin with one end.
    [[nodiscard]] PlatformResult BeginDockspaceHost(const DockspaceHostDesc&);
    void EndDockspaceHost();
}
