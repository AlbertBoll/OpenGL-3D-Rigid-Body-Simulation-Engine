#include "gepch.h"
#include "Managers/WindowManager.h"
#include <Windows/SDLWindow.h>


namespace GEngine::Manager
{
	WindowManager::~WindowManager()
	{
		// SDLWindow owns its shutdown, including failed AddWindows initialization.
		m_Windows.clear();
		m_NumOfWindows = 0;
	}

	ScopedPtr<WindowManager> WindowManager::GetScopedInstance()
	{
		struct MkUniEnablr : public WindowManager {};
		auto instance = CreateScopedPtr<MkUniEnablr>();

		return instance;
	}

	/*WindowManager& WindowManager::Get()
	{
		static WindowManager windowManager;
		return windowManager;
	}*/

	void WindowManager::AddWindow(const std::string& title)
	{
		GENGINE_CORE_INFO("Initialize Windows...");
	
		auto windowPtr = Window::Create();
		windowPtr->Initialize();
		windowPtr->SetTitle(title);
		uint32_t ID = windowPtr->GetWindowID();
		m_Windows.insert({ ID, std::move(windowPtr) });
		m_NumOfWindows++;
	}

	void WindowManager::AddWindows(const WindowProperties& winProp)
	{
		GENGINE_CORE_INFO("Initialize Windows...");
		
		auto windowPtr = Window::Create();
		windowPtr->Initialize(winProp);

		uint32_t ID = windowPtr->GetWindowID();
		m_Windows.insert({ ID, std::move(windowPtr) });
		m_NumOfWindows++;
	}


	void WindowManager::AddWindows(const std::initializer_list<WindowProperties>& winProps)
	{
	
		for (auto& p : winProps)
		{
			AddWindows(p);
		}

		//GetInternalWindow(1)->BeginRender();
	}


	void WindowManager::RemoveWindow(uint32_t ID)
	{
		if (m_Windows.erase(ID)) --m_NumOfWindows;
		/*if (--m_NumOfWindows == 0)
		{
			FreeContext();
			
		}*/
	}
}
