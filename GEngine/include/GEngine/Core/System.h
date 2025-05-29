#pragma once

#pragma once
#include "Core/Timestep.h"

namespace GEngine
{
	class System
	{
	public:
		virtual ~System() = default;

		virtual void Initialize() = 0;
		virtual void Update(Timestep ts) = 0;
		virtual void OnExit() = 0;
	};

}