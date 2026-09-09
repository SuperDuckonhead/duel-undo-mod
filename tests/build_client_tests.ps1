$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script = Join-Path $root 'tools\Build-Client.ps1'

if (-not (Test-Path -LiteralPath $script -PathType Leaf)) {
    throw 'Build-Client.ps1 is missing'
}

& $script -Configuration Release -PrepareOnly
if ($LASTEXITCODE -ne 0) {
    throw "Prepare-only build failed with exit code $LASTEXITCODE"
}

$expected = @(
    'client/irrlicht/defines.lua',
    'client/event/include/event2/event-config.h',
    'client/event/include/evconfig-private.h',
    'client/jpeg/src/jversion.h',
    'client/png/pnglibconf.h',
    'client/miniaudio/premake5.lua'
)

foreach ($relative in $expected) {
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Prepared build input missing: $relative"
    }
}

foreach ($relative in @('client/load-once.conf', 'client/gframe/ygopro.icns')) {
    if (Test-Path -LiteralPath (Join-Path $root $relative)) {
        throw "Build preparation copied an unrelated runtime resource: $relative"
    }
}

if (Test-Path -LiteralPath (Join-Path $root 'client/sqlite3/version')) {
    throw 'SQLite metadata file shadows the C++ standard <version> header under LLVM-MinGW'
}

Write-Host 'Build-Client prepare-only contract passed.'
