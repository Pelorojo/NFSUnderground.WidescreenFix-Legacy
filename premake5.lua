-- Build setup for NFSUnderground.WidescreenFix Legacy, reduced from ThirteenAG's WidescreenFixesPack premake5.lua
-- (state of commit 17c4408) to this one project. Generate the Visual Studio solution with premake5.bat.
workspace "NFSUnderground.WidescreenFix-Legacy"
   configurations { "Release", "Debug" }
   platforms { "Win32" }
   architecture "x32"
   location "build"
   objdir ("build/obj")
   buildlog ("build/log/%{prj.name}.log")
   cppdialect "C++latest"

   kind "SharedLib"
   language "C++"
   targetdir "data/%{prj.name}/scripts"
   targetextension ".asi"
   characterset ("UNICODE")
   toolset "v142" -- VS 2019 toolset: newer ones drop Windows 7 support
   staticruntime "On"

   defines { "rsc_CompanyName=\"ThirteenAG, Pelorojo\"" }
   defines { "rsc_LegalCopyright=\"MIT License\""}
   defines { "rsc_FileVersion=\"1.0.0.0\"", "rsc_ProductVersion=\"1.0.0.0\"" }
   defines { "rsc_InternalName=\"%{prj.name}\"", "rsc_ProductName=\"%{prj.name} Legacy\"", "rsc_OriginalFilename=\"%{prj.name}.asi\"" }
   defines { "rsc_FileDescription=\"NFSUnderground.WidescreenFix Legacy (EU/SoftClub compatible)\"" }
   defines { "rsc_UpdateUrl=\"https://github.com/Pelorojo/NFSUnderground.WidescreenFix-Legacy\"" }

   files { "source/%{prj.name}/*.cpp" }
   files { "data/%{prj.name}/**" }
   files { "resources/*.rc" }
   files { "external/hooking/Hooking.Patterns.h", "external/hooking/Hooking.Patterns.cpp" }
   files { "includes/stdafx.h", "includes/stdafx.cpp" }
   includedirs { "includes" }
   includedirs { "external/hooking" }
   includedirs { "external/injector/include" }
   includedirs { "external/inireader" }
   includedirs { "external/spdlog/include" }
   includedirs { "external/filewatch" }
   includedirs { "external/modutils" }

   local dxsdk = os.getenv "DXSDK_DIR"
   if dxsdk then
      includedirs { dxsdk .. "/include" }
      libdirs { dxsdk .. "/lib/x86" }
   elseif os.isdir("external/minidx9") then
      includedirs { "external/minidx9/Include" }
      libdirs { "external/minidx9/Lib/x86" }
   else
      includedirs { "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/include" }
      libdirs { "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/lib/x86" }
   end

   -- Optional: set GAME_DIR (folder with speed.exe, ending in a slash) to copy the built .asi into its scripts folder
   local gamepath = os.getenv "GAME_DIR"
   if gamepath then
      postbuildcommands {
         "if exist \"" .. gamepath .. "scripts\\\" copy /y \"$(TargetPath)\" \"" .. gamepath .. "scripts\\\""
      }
      debugdir (gamepath)
      debugcommand (gamepath .. "speed.exe")
   end

   -- Hooking.Patterns.cpp uses std::string without including <string>; newer MSVC versions no longer pull it in
   filter "files:external/hooking/Hooking.Patterns.cpp"
      forceincludes { "string" }

   filter "configurations:Debug*"
      defines "DEBUG"
      symbols "On"

   filter "configurations:Release*"
      defines "NDEBUG"
      optimize "On"

project "NFSUnderground.WidescreenFix"
   files { "textures/NFS/NFSU/icon.rc" }
   defines { "IDR_NFSUICON=200" }
