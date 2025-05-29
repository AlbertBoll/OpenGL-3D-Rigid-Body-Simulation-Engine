#include"RigidBodySimulation.h"

WindowProperties winProp{ .m_Title = "RigidBodySimulation", .m_Width = 1280, .m_Height = 720, .m_MinWidth = 480, .m_MinHeight = 320, .m_AspectRatio = 16.f / 9.f,
									.m_WinPos = WindowPos::ButtomRight,
									.m_XPaddingToCenterY = 5,
									.m_YPaddingToCenterX = 20,
	.ImGuiWindowProperties = {.bMoveFromTitleBarOnly = true, .bDockingEnabled = true, .bViewPortEnabled = false }
};