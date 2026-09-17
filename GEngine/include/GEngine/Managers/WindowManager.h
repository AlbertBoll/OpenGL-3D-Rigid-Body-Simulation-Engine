#pragma once

#include <unordered_map>
#include <concepts>
#include "Core/Window.h"
#include "Managers/ManagerBase.h"

namespace GEngine::Manager
{

	class WindowManager: public ManagerBase<WindowManager>
	{
		//declare friend ManagerBase to invoke managerbase Get()    
		friend class ManagerBase<WindowManager>;

	public:
		
		
		~WindowManager();
		static ScopedPtr<WindowManager> GetScopedInstance();

		//void Initialize();
		[[nodiscard]] std::expected<Window*, PlatformError> GetInternalWindow(uint32_t ID)
		{
			auto found = m_Windows.find(ID);
			if (found == m_Windows.end())
				return std::unexpected(PlatformError{PlatformErrorCode::WindowNotFound, "window lookup", "Unknown window ID " + std::to_string(ID)});
			return found->second.get();
		}

		std::unordered_map<uint32_t, ScopedPtr<Window>>& GetWindows() { return m_Windows; }

		[[nodiscard]] PlatformResult AddWindow(const std::string& title = "GEngine Editor App");

        template<class... Args> requires (sizeof...(Args) > 1 && (std::same_as<Args, WindowProperties> && ...))
        [[nodiscard]] PlatformResult AddWindows(const Args&... properties) { return AddWindows({properties...}); }

		[[nodiscard]] PlatformResult AddWindows(const WindowProperties& winProp = WindowProperties{});

		[[nodiscard]] PlatformResult AddWindows(const std::initializer_list<WindowProperties>& winProps);

		uint8_t& GetNumOfWindows() { return m_NumOfWindows; }
		uint8_t GetNumOfWindows()const { return m_NumOfWindows; }

		void RemoveWindow(uint32_t ID);


	private:

		friend class GEngine;
		WindowManager() = default;

	private:
		std::unordered_map<uint32_t, ScopedPtr<Window>> m_Windows{};
		uint8_t m_NumOfWindows{};
	};

}
