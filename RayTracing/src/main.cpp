#include "RayTracing.h"

WindowProperties winProp{ .m_Title = "RayTracingApp", .m_Width = 1600, .m_Height = 900, .m_MinWidth = 480, .m_MinHeight = 320, .m_AspectRatio = 16.f / 9.f,
									.m_WinPos = WindowPos::Center,
									.m_XPaddingToCenterY = 5,
									.m_YPaddingToCenterX = 20,
	.ImGuiWindowProperties = {.bMoveFromTitleBarOnly = true, .bDockingEnabled = true, .bViewPortEnabled = false }
};