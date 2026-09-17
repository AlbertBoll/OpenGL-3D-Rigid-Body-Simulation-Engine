#include "gepch.h"
#include "Events/ApplicationEvent.h"
#include "Core/BaseApp.h"
namespace GEngine::Event
{
    void ApplicationEvent::OnWindowClose(WindowStateEvent& event)
    {
        BaseApp::GetEventManager()->GetEventDispatcher().DispatchEvent("WindowClose", Manager::WindowCloseParam{event.ID});
    }
    void ApplicationEvent::OnWindowResize(WindowStateEvent& event)
    {
        auto window = BaseApp::GetWindowManager()->GetInternalWindow(event.ID);
        if (window) (*window)->RefreshDimensions();
        else ReportPlatformError(window.error());
    }
    void ApplicationEvent::OnAppQuit() { BaseApp::GetEngine().ShutDown(); }
}
