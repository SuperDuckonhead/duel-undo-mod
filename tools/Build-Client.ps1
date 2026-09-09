param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$client = Join-Path $root 'client'
$toolRoot = Join-Path $root '.cache\tools'
$llvmBin = Join-Path $toolRoot 'llvm-mingw-20260908-ucrt-x86_64\bin'
$premake = Join-Path $toolRoot 'premake\premake5.exe'
$make = Join-Path $llvmBin 'mingw32-make.exe'

$requiredDirectories = @(
    'gframe', 'ocgcore', 'lua', 'sqlite3', 'freetype', 'event', 'jpeg',
    'png', 'zlib', 'lzma', 'irrlicht', 'miniaudio',
    'miniaudio/external/ogg', 'miniaudio/external/opus',
    'miniaudio/external/opusfile', 'miniaudio/external/vorbis'
)
foreach ($relative in $requiredDirectories) {
    if (-not (Test-Path -LiteralPath (Join-Path $client $relative) -PathType Container)) {
        throw "Required client dependency is missing: client/$relative"
    }
}
foreach ($program in @($premake, $make, (Join-Path $llvmBin 'clang++.exe'))) {
    if (-not (Test-Path -LiteralPath $program -PathType Leaf)) {
        throw "Required portable build tool is missing: $program"
    }
}

& (Join-Path $PSScriptRoot 'Apply-LuaUndoPatch.ps1')
& (Join-Path $PSScriptRoot 'Apply-IrrlichtUndoPatch.ps1')

$premakeDirectories = @('event', 'freetype', 'irrlicht', 'jpeg', 'lua', 'lzma', 'miniaudio', 'png', 'sqlite3', 'zlib')
foreach ($name in $premakeDirectories) {
    Copy-Item -Path (Join-Path $client "premake/$name/*") -Destination (Join-Path $client $name) -Recurse -Force
}
foreach ($name in @('ygopro.ico', 'ygopro.rc')) {
    Copy-Item -LiteralPath (Join-Path $client "resource/gframe/$name") -Destination (Join-Path $client "gframe/$name") -Force
}

Copy-Item -LiteralPath (Join-Path $client 'event/msvc-event-config.h') -Destination (Join-Path $client 'event/include/event2/event-config.h') -Force
# LLVM-MinGW's UCRT provides strtok_r; the upstream MSVC config correctly omits it.
Add-Content -LiteralPath (Join-Path $client 'event/include/event2/event-config.h') -Value "`n#define EVENT__HAVE_STRTOK_R 1"
Copy-Item -LiteralPath (Join-Path $client 'event/WIN32-Code/nmake/evconfig-private.h') -Destination (Join-Path $client 'event/include/evconfig-private.h') -Force
Copy-Item -LiteralPath (Join-Path $client 'jpeg/src/jversion.h.in') -Destination (Join-Path $client 'jpeg/src/jversion.h') -Force
Copy-Item -LiteralPath (Join-Path $client 'png/scripts/pnglibconf.h.prebuilt') -Destination (Join-Path $client 'png/pnglibconf.h') -Force
Copy-Item -LiteralPath (Join-Path $client 'miniaudio/extras/miniaudio_split/miniaudio.c') -Destination (Join-Path $client 'miniaudio/miniaudio.c') -Force
Copy-Item -LiteralPath (Join-Path $client 'miniaudio/extras/miniaudio_split/miniaudio.h') -Destination (Join-Path $client 'miniaudio/miniaudio.h') -Force

# These SQLite package metadata files shadow libc++'s <version> on a case-insensitive filesystem.
foreach ($name in @('version', 'VERSION')) {
    $metadataPath = Join-Path $client "sqlite3/$name"
    if (Test-Path -LiteralPath $metadataPath -PathType Leaf) {
        Remove-Item -LiteralPath $metadataPath -Force
    }
}

if ($PrepareOnly) {
    Write-Host 'Prepared pinned client dependencies for Premake.'
    exit 0
}

$previousPath = $env:Path
$env:Path = "$llvmBin;$previousPath"
try {
    Push-Location $client
    try {
        & $premake --file=premake5-llvm-mingw.lua gmake --build-all --use-simd=none --no-dxsdk
        if ($LASTEXITCODE -ne 0) { throw "Premake failed with exit code $LASTEXITCODE" }

        $makeConfiguration = $Configuration.ToLowerInvariant()
        # Generated Windows target-directory rules race when projects create the shared output directory.
        & $make -C build '-j1' "config=$makeConfiguration"
        if ($LASTEXITCODE -ne 0) { throw "Client build failed with exit code $LASTEXITCODE" }
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:Path = $previousPath
}

$builtBinary = Join-Path $client "bin/$($Configuration.ToLowerInvariant())/ygopro-undo.exe"
if (-not (Test-Path -LiteralPath $builtBinary -PathType Leaf)) {
    throw "Expected client binary was not produced: $builtBinary"
}

$outputDirectory = Join-Path $root "out/client/$Configuration"
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$outputBinary = Join-Path $outputDirectory 'ygopro-undo.exe'
Copy-Item -LiteralPath $builtBinary -Destination $outputBinary -Force
& (Join-Path $PSScriptRoot 'Test-RuntimeImports.ps1') -Binary "out/client/$Configuration/ygopro-undo.exe"
Write-Host "Built client: $outputBinary"
