#pragma once
#include <cstdint>

namespace GEngine::Input
{
	using MouseCodes = uint8_t;

	namespace Mouse
	{
		typedef enum : MouseCodes
		{
			GENGINE_BUTTON_LEFT = 1,
			GENGINE_BUTTON_MIDDLE = 2,
			GENGINE_BUTTON_RIGHT = 3,
			GENGINE_BUTTON_X1 = 4,
			GENGINE_BUTTON_X2 = 5,

		} GEngineMouseCode;


	}
}
