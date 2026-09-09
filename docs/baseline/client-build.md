> Historical baseline build record. Current undo-client build, runtime isolation and packaging instructions are in [BUILD.md](../BUILD.md).

# Portable client build

The pinned client can be built on this machine with the repository-local Premake 5.0.0-beta8 and LLVM-MinGW 20260908 toolchain:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build-Client.ps1 -Configuration Release
```

The result is `out/client/Release/ygopro-undo.exe`, accompanied by the portable toolchain's `libc++.dll` and `libunwind.dll`. The script never copies into the installed runtime directory.

The build requires the source archives pinned by `sources.lock.json` to have already been verified and extracted into the ignored dependency directories under `client/`. `Build-Client.ps1` validates that those directories and the repository-local tools exist, but it is not currently a download/bootstrap script.

This is a compatibility build, not a reproduction of the installed executable. The installed package used Visual Studio 2026/MSVC Release x64, static CRT, SSE2, and optional Direct3D 9 support. The portable build uses LLVM-MinGW/UCRT, a dynamic libc++ runtime, scalar SIMD mode, and OpenGL. Audio, fonts, Win32 IME support, networking, and the Windows GUI subsystem remain enabled.

LLVM-MinGW requires four mechanical adaptations captured by the build script and `client/premake5-llvm-mingw.lua`:

- serialize Premake's generated multi-project Makefile because its Windows directory creation rules race in parallel;
- tell libevent that UCRT supplies `strtok_r`;
- remove SQLite package metadata files named `version`/`VERSION`, which shadow libc++'s standard `<version>` header on a case-insensitive filesystem;
- add the Windows Unicode entry-point flag and explicit IME, OpenGL, and GDI system libraries absent from the upstream Windows-gmake path.

GUI/resource compatibility remains a separate Gate-A check. Do not launch this executable with the original installation as its working directory: the client writes `system.conf` on normal exit. Use the isolated smoke runtime prepared by the project tooling.
