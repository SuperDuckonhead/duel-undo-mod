dofile("premake5.lua")

project "YGOPro"
    filter { "system:windows", "action:gmake" }
        links { "imm32", "opengl32", "gdi32" }
        linkoptions { "-municode" }