# Soundify Reborn Bedrock - DLL source (GPL-3.0)

Complete corresponding source of `Latite-Soundify-0.4.0.dll`, the native module that GDuck's
Soundify Reborn for Minecraft Bedrock (Windows) loads into the game. The DLL is
[Latite Client](https://github.com/LatiteClient/Latite) built together with the Soundify module,
so it is licensed under the GNU General Public License version 3 (`LICENSE`).

| | |
|---|---|
| Soundify release | 0.4.0 (tag `v0.4.0`) |
| Latite commit | `9f7463515dd298a496da918285936d78c7416aad` (submodule `bedrock/third_party/Latite`) |
| Minecraft line | see `NativeGameLine` in `bedrock/windows/include/soundify/core/GameVersion.hpp` |

Not included and not covered by this license: the Soundify launcher, the sound pack and the
GDuck account service. Without a Soundify account the module stays silent.

## Build

Requirements: Windows, Visual Studio 2022 Build Tools (MSVC 19.43+), CMake 3.24+, Git and MinGW
`ld.exe`. Clone into a short path (MSBuild's 260-character limit):

```powershell
git clone --recurse-submodules <this repository> D:\sfy
powershell -NoProfile -File D:\sfy\bedrock\tools\build_native.ps1
```

The DLL is written to `bedrock\windows\out\latite\Release\Latite.dll`; the release renames it
to `Latite-Soundify-0.4.0.dll`. `bedrock/windows/integrations/latite/integrate.cmake`
patches the Latite build in the build directory only and refuses any other Latite commit.
