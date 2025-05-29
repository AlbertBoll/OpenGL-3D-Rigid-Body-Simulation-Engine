#pragma once

//suppress the warning of macro redefinition
#pragma warning (disable : 4005)

//Since the NOMINMAX conflicts the standard library(std::min and std::max) using the following the
#ifdef GENGINE_PLATFORM_WINDOWS
	#ifndef NOMINMAX   
		#define NOMINMAX

	#endif 
#endif 

#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <memory>
#include <vector>
#include <glad/glad.h> 
#include <filesystem>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <functional>

#include "sdl2/SDL.h"
#include <sdl2/SDL_ttf.h>
#include <fstream>
#include <variant>
#include "Core/Log.h"
#include "Core/Base.h"
#include <limits>
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include <mutex>       // std::mutex, std::scoped_lock
#include <queue>       // std::queue
#include <thread>      // std::this_thread, std::thread
#include <type_traits> // std::common_type_t, std::decay_t, std::enable_if_t, std::is_void_v, std::invoke_result_t
#include <utility>     // std::move
#include <future>  
#include <atomic> 

//#include "Math/Math.h"

#ifdef GENGINE_PLATFORM_WINDOWS
	#include<Windows.h>
#endif 


