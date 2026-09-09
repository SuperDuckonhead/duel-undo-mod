# Dependency bootstrap

From the independent development checkout:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 -Target All -Configuration Release
~~~

Bootstrap acquires only the pinned portable tools and third-party libraries. Client, ocgcore and WindBot source are already tracked; it never reimports those over modifications. All downloads use HTTPS and require the archive SHA-256/SHA-512 from sources.lock.json before extraction. The MyCard CI content cache is a fallback for library archives and uses the same required checksum.

- Target Tools prepares CMake, LLVM-MinGW, Premake, the .NET SDK and net48 reference assemblies.
- Target Libraries prepares the fourteen fixed libraries; LibraryName can select named libraries.
- Offline forbids downloads and requires matching existing cache archives.
- Existing source/tool directories are preserved. Archive verification does not prove that an existing extracted tree is unmodified.
- An incomplete extraction is diagnosed and preserved for inspection. Use a fresh checkout/cache after resolving its cause; bootstrap does not recursively delete directories.

No system installer, global environment change, or original game resource copy is performed. Existing directories below dependency/cache destinations may not be reparse points. Read/write outputs are confined to the development checkout. Runtime pics/deck/script/expansions/pack/database/config remain outside the tracked source tree.

Observed validation: five focused fixture cases pass (verified extraction, checksum mismatch, forbidden destination, offline missing archive, incomplete source directory). The current baseline's five tool archives and fourteen library archives pass Offline verification. This is not yet a full clean-checkout rebuild of the finished mod; that final build/package check remains part of release acceptance.
