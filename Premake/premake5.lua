-- premake5.lua

solution "MyProxy2"
    configurations { "Debug", "Release" }

    project "MyProxy2"
        kind "ConsoleApp"
        language "C++"
        flags "Unicode"

        files { "../*.h", "../*.hpp", "../*.cpp" }
        vpaths {
           ["Headers"] = { "../*.h", "../*.hpp", },
           ["Sources"] = "../*.cpp",
        }
        
        defines { "_CRT_SECURE_NO_WARNINGS", "UNICODE", "_UNICODE", "WIN32_LEAN_AND_MEAN" }

        filter "configurations:Debug"
            defines { "_DEBUG", "DEBUG" }
            flags { "Symbols" }

        filter "configurations:Release"
            defines { "NDEBUG" }
            optimize "On"

    project "AsyncTest"
        kind "ConsoleApp"
        language "C++"
        flags "Unicode"

        headers = { "../Async.hpp", "../Logger.hpp", "../ws-util.h", }
        sources = { "../Async.cpp", "../Logger.cpp", "../ws-util.cpp", "AsyncTest/main.cpp", }
        
        files(headers)
        files(sources)
        
        vpaths {
           ["Headers"] = headers,
           ["Sources"] = sources,
        }
        
        defines { "_CRT_SECURE_NO_WARNINGS", "UNICODE", "_UNICODE", "WIN32_LEAN_AND_MEAN" }

        filter "configurations:Debug"
            defines { "_DEBUG", "DEBUG" }
            flags { "Symbols" }

        filter "configurations:Release"
            defines { "NDEBUG" }
            optimize "On"
