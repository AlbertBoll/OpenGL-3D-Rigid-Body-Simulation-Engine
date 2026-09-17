#include "gepch.h"
#include "Managers/WindowManager.h"

namespace GEngine::Manager
{
    WindowManager::~WindowManager() { m_Windows.clear(); m_NumOfWindows = 0; }
    ScopedPtr<WindowManager> WindowManager::GetScopedInstance()
    {
        struct Enable : WindowManager {};
        return CreateScopedPtr<Enable>();
    }
    PlatformResult WindowManager::AddWindow(const std::string& title)
    {
        WindowProperties properties; properties.m_Title = title;
        return AddWindows(properties);
    }
    PlatformResult WindowManager::AddWindows(const WindowProperties& properties) { return AddWindows({properties}); }
    PlatformResult WindowManager::AddWindows(const std::initializer_list<WindowProperties>& properties)
    {
        if (properties.size() > 255 - m_Windows.size())
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState, "add windows", "Window count exceeds supported capacity"});
        // Keep existing owners intact if any requested window fails.
        Window* previous = nullptr;
        for (const auto& [id, window] : m_Windows) if (window->IsCurrent()) { previous = window.get(); break; }
        std::unordered_map<uint32_t, ScopedPtr<Window>> pending;
        for (const auto& property : properties)
        {
            auto window = Window::Create(property);
            if (!window)
            {
                pending.clear();
                if (previous)
                    if (auto restored = previous->BeginRender(); !restored) return restored;
                return std::unexpected(window.error());
            }
            const auto id = (*window)->GetWindowID();
            pending.emplace(id, std::move(*window));
        }
        m_Windows.merge(pending);
        m_NumOfWindows = static_cast<uint8_t>(m_Windows.size());
        return {};
    }
    void WindowManager::RemoveWindow(uint32_t id)
    {
        if (m_Windows.erase(id)) --m_NumOfWindows;
    }
}
