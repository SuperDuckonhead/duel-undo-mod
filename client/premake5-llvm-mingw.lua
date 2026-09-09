dofile("premake5.lua")

project "YGOPro"
    targetname "ygopro-undo"
    filter { "system:windows", "action:gmake" }
        links { "imm32", "opengl32", "gdi32", "bcrypt" }
        linkoptions { "-municode", "-static" }
