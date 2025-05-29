project "glad"
	kind "StaticLib"
	language "C"
	staticruntime "on"
	targetdir(tdir)
	objdir(odir)

	files
	{
		"include/**.h",
		"src/**.c"
	}

	sysincludedirs
	{
		"include"
	}


	-- Windows
	filter
	{
		"system:windows"
	}
	buildoptions "/MTd"
	systemversion "latest"


	-- MACOS
	filter
	{
		"system:macosx"
	}

		xcodebuildsettings
		{
			["MACOSX_DEPLOYMENT_TARGET"] = "10.15",
			["UseModernBuildSystem"] = "NO"

		}


	-- DEBUG
	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

	-- RELEASE
	filter "configurations:Release"
		runtime "Release"
		symbols "off"
		optimize "on"
