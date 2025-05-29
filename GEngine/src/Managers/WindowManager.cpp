#include "gepch.h"
#include "Managers/WindowManager.h"
#include <Windows/SDLWindow.h>


namespace GEngine::Manager
{
	WindowManager::~WindowManager()
	{
		for (auto& p : m_Windows)
		{
			if (p.second)
			{
				p.second->ShutDown();
			}
		}

		m_Windows.clear();
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
		--m_NumOfWindows;
		ASSERT(m_Windows[ID]);
		m_Windows[ID]->ShutDown();
		m_Windows.erase(ID);
		/*if (--m_NumOfWindows == 0)
		{
			FreeContext();
			
		}*/
	}
}
