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
		-- Static dependency also links into non-incremental/ASAN consumers. Keep symbols without /ZI.
		editandcontinue "Off"

	-- RELEASE
	filter "configurations:Release"
		runtime "Release"
		symbols "off"
		optimize "on"
