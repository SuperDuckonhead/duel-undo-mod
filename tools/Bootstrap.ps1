param(
    [ValidateSet('All','Tools','Libraries')][string]$Target = 'All',
    [string[]]$LibraryName = @(),
    [switch]$Offline
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lockPath = Join-Path $root 'sources.lock.json'
& (Join-Path $PSScriptRoot 'Test-SourceLock.ps1') -Path $lockPath
$sourceLock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
$libraryDestinations = @{
    lua='client/lua'; sqlite3='client/sqlite3'; freetype='client/freetype'
    event='client/event'; jpeg='client/jpeg'; png='client/png'; zlib='client/zlib'; lzma='client/lzma'
    ogg='client/miniaudio/external/ogg'; opus='client/miniaudio/external/opus'
    opusfile='client/miniaudio/external/opusfile'; vorbis='client/miniaudio/external/vorbis'
    irrlicht='client/irrlicht'; miniaudio='client/miniaudio'
}
$libraryProbes = @{
    lua='src/lua.h'; sqlite3='sqlite3.h'; freetype='include/ft2build.h'; event='include/event2/event.h'
    jpeg='src/jpeglib.h'; png='png.h'; zlib='zlib.h'; lzma='src/liblzma/api/lzma.h'
    ogg='include/ogg/ogg.h'; opus='include/opus.h'; opusfile='include/opusfile.h'; vorbis='include/vorbis/codec.h'
    irrlicht='include/irrlicht.h'; miniaudio='miniaudio.h'
}

$tools = @{
    cmake=@{cache='.cache/tools/cmake.zip'; destination='.cache/tools'; probe='.cache/tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe'}
    'llvm-mingw'=@{cache='.cache/tools/llvm-mingw.zip';destination='.cache/tools';probe='.cache/tools/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe'}
    premake=@{cache='.cache/tools/premake.zip';destination='.cache/tools/premake';probe='.cache/tools/premake/premake5.exe'}
    'dotnet-sdk'=@{cache='.cache/tools/dotnet-sdk-8.0.425.zip';destination='.cache/tools/dotnet';probe='.cache/tools/dotnet/dotnet.exe'}
    'net48-reference-assemblies'=@{cache='.cache/microsoft.netframework.referenceassemblies.net48.1.0.3.nupkg';destination='.cache/net48-ref';probe='.cache/net48-ref/build/.NETFramework/v4.8/mscorlib.dll'}
}
function Assert-LocalDestination([string]$Relative) {
    $candidate = [IO.Path]::GetFullPath((Join-Path $root $Relative))
    $prefix = $root.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe destination: $Relative" }
    $cursor = $candidate
    while ($cursor.Length -gt $root.Length) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing dependency destination through a reparse point: $cursor"
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $candidate
}
function Get-VerifiedArchive([object]$Record, [string]$RelativeCache, [string]$UrlProperty = 'url') {
    $url = [string]$Record.$UrlProperty
    if (-not [Uri]::IsWellFormedUriString($url, [UriKind]::Absolute) -or ([Uri]$url).Scheme -ne 'https') {
        throw "Archive URL must use HTTPS: $($Record.name)"
    }
    $algorithm = 'SHA256'
    $expected = [string]$Record.sha256
    if (-not $expected) { $algorithm = 'SHA512'; $expected = [string]$Record.sha512 }
    $length = $(if ($algorithm -eq 'SHA256') { 64 } else { 128 })
    if ($expected -notmatch ("^[0-9a-fA-F]{" + $length + "}$")) { throw "Invalid archive checksum: $($Record.name)" }
    $cache = Assert-LocalDestination $RelativeCache
    # Reuse the archive locations from the first recorded baseline acquisition.
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
        $legacyRoot = Join-Path $root '.cache/native-deps'
        if (Test-Path -LiteralPath $legacyRoot -PathType Container) {
            foreach ($legacy in Get-ChildItem -LiteralPath $legacyRoot -File) {
                if ($legacy.Name -notmatch [regex]::Escape([string]$Record.name)) { continue }
                if ((Get-FileHash -LiteralPath $legacy.FullName -Algorithm $algorithm).Hash -eq $expected) { return $legacy.FullName }
            }
        }
        if ($Offline) { throw "Offline archive missing: $cache" }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($cache)) | Out-Null
        $partial = $cache + '.' + [Guid]::NewGuid().ToString('N') + '.partial'
        $urls = @($url)
        if ($UrlProperty -eq 'url' -and $RelativeCache -like '.cache/dependencies/*') {
            $urls += 'https://mat-cacher.moenext.com/' + $url + '?sha256sum=' + $expected
        }
        $downloaded = $false
        foreach ($source in $urls) {
            & curl.exe --fail --location --retry 2 --silent --show-error --output $partial $source
            if ($LASTEXITCODE -eq 0) { $downloaded = $true; break }
        }
        if (-not $downloaded) { throw "Archive download failed: $url" }
        if ((Get-FileHash -LiteralPath $partial -Algorithm $algorithm).Hash -ne $expected) {
            throw "Downloaded archive checksum mismatch: $($Record.name)"
        }
        Move-Item -LiteralPath $partial -Destination $cache
    }
    if ((Get-FileHash -LiteralPath $cache -Algorithm $algorithm).Hash -ne $expected) {
        throw "Cached archive checksum mismatch: $($Record.name)"
    }
    return $cache
}
if ($Target -in @('All','Tools')) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($name in @('cmake','llvm-mingw','premake','dotnet-sdk','net48-reference-assemblies')) {
        $records = @($sourceLock.toolchainArchives | Where-Object name -eq $name)
        if ($records.Count -ne 1) { throw "Expected one locked tool archive: $name" }
        $layout = $tools[$name]
        $archive = Get-VerifiedArchive $records[0] $layout.cache
        $destination = Assert-LocalDestination $layout.destination
        $probe = Assert-LocalDestination $layout.probe
        if (Test-Path -LiteralPath $probe -PathType Leaf) {
            Write-Host "Verified cached archive; preserving existing tool: $name"
            continue
        }
        [IO.Directory]::CreateDirectory($destination) | Out-Null
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $destination)
        if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) { throw "Tool extraction did not produce expected executable: $probe" }
        Write-Host "Prepared tool: $name"
    }
}
if ($Target -in @('All','Libraries')) {
    $names = @('irrlicht','miniaudio','lua','sqlite3','freetype','event','jpeg','png','zlib','lzma','ogg','opus','opusfile','vorbis')
    if ($LibraryName.Count) {
        foreach ($name in $LibraryName) {
            if (-not $libraryDestinations.ContainsKey($name)) { throw "Unknown dependency name: $name" }
        }
        $names = @($names | Where-Object { $LibraryName -contains $_ })
    }
    foreach ($name in $names) {
        $isGitArchive = $name -in @('irrlicht','miniaudio')
        $records = $(if ($isGitArchive) { @($sourceLock.components | Where-Object name -eq $name) } else { @($sourceLock.archives | Where-Object name -eq $name) })
        $records = @($records)
        if ($records.Count -ne 1) { throw "Expected one locked library: $name" }
        $record = $records[0]
        if ([string]$record.destination -cne $libraryDestinations[$name]) { throw "Forbidden dependency destination for $name : $($record.destination)" }
        $destination = Assert-LocalDestination $libraryDestinations[$name]
        $urlProperty = $(if ($isGitArchive) { 'archiveUrl' } else { 'url' })
        $archive = Get-VerifiedArchive $record ".cache/dependencies/$name.tar.gz" $urlProperty
        $incomplete = Join-Path $destination '.undo-bootstrap-incomplete'
        $probe = Join-Path $destination $libraryProbes[$name]
        if (Test-Path -LiteralPath $destination -PathType Container) {
            if ((Test-Path -LiteralPath $incomplete) -or -not (Test-Path -LiteralPath $probe -PathType Leaf)) {
                throw "Existing dependency is incomplete; use a fresh cache/source checkout: $destination"
            }
            Write-Host "Verified cached archive; preserving existing dependency source: $name"
            continue
        }
        [IO.Directory]::CreateDirectory($destination) | Out-Null
        [IO.File]::WriteAllText($incomplete, 'Extraction in progress')
        & tar.exe -xf $archive --strip-components=1 -C $destination
        if ($LASTEXITCODE -ne 0) { throw "Library extraction failed: $name" }
        if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) { throw "Library extraction incomplete: $name" }
        Remove-Item -LiteralPath $incomplete
        Write-Host "Prepared library: $name"
    }
}
Write-Host 'Bootstrap completed. Existing source/tool directories were preserved; archive verification does not certify local edits.'
