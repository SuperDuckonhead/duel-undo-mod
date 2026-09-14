param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$BuildOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$compiler = Join-Path $root '.cache/tools/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe'
$build = Join-Path $root 'client/build'
$makefile = Get-Content -Raw (Join-Path $build 'YGOPro.make')
$mode = $Configuration.ToLowerInvariant()
$section = [regex]::Match($makefile, ('(?s)ifeq \(\$\(config\),' + $mode + '\)(.*?)(?:else ifeq|endif)')).Groups[1].Value
if(!$section) { throw 'Missing client make configuration; build client first.' }
$defs = [regex]::Match($section, '(?m)^DEFINES \+= (.*)$').Groups[1].Value.Trim() -split ' '
$includes = [regex]::Match($makefile, '(?m)^INCLUDES \+= (.*)$').Groups[1].Value.Trim() -split ' '
$libs = [regex]::Match($section, '(?m)^LIBS \+= (.*)$').Groups[1].Value.Trim() -split ' '
$objects = [regex]::Matches($makefile, '(?m)^OBJECTS \+= \$\(OBJDIR\)/(.*\.o)') | ForEach-Object { $_.Groups[1].Value.Trim() } | Where-Object { $_ -ne 'gframe.o' } | ForEach-Object { Join-Path $root "client/obj/$Configuration/YGOPro/$_" }
$testDir = Join-Path $root "out/editor-integration/$Configuration"
New-Item -ItemType Directory -Force -Path $testDir | Out-Null
$exe = Join-Path $testDir 'editor_integration_tests.exe'
Push-Location $build
try {
    & $compiler '-std=c++17' '-fno-rtti' '-g' @defs @includes '-I../gframe' '-I../../tests' (Join-Path $root 'tests/editor_integration_tests.cpp') @objects @libs '-o' $exe
    if($LASTEXITCODE) { throw "Integration compile failed: $LASTEXITCODE" }
} finally { Pop-Location }
foreach($dll in @('libc++.dll','libunwind.dll')) { Copy-Item -LiteralPath (Join-Path (Split-Path $compiler) $dll) -Destination $testDir -Force }
if($BuildOnly) { Write-Host "Prepared actual editor integration binary: $exe"; exit 0 }
$runtime = Join-Path $root 'out/baseline-runtime'
if(!(Test-Path -LiteralPath (Join-Path $runtime '.undo-smoke-runtime'))) { throw 'Prepare isolated smoke runtime first.' }
foreach($name in @('editor-observer-ready','editor-observer-loaded','editor-observer-done','editor-observer-passed')) {
    $barrier = Join-Path $runtime $name
    if(Test-Path -LiteralPath $barrier) { Remove-Item -LiteralPath $barrier -Force }
}
$observer = Start-Process -FilePath $exe -ArgumentList '--observe' -WorkingDirectory $runtime -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDir 'observer.stdout.log') -RedirectStandardError (Join-Path $testDir 'observer.stderr.log')
try {
    Push-Location $runtime
    try { & $exe '--with-observer'; $testExit = $LASTEXITCODE } finally { Pop-Location }
    if(!$observer.WaitForExit(15000)) { throw 'Second editor process timed out.' }
    Get-Content -LiteralPath (Join-Path $testDir 'observer.stdout.log')
    Get-Content -LiteralPath (Join-Path $testDir 'observer.stderr.log')
    if($testExit) { throw "Integration failed: $testExit" }
    if($observer.ExitCode) { throw "Second process failed: $($observer.ExitCode)" }
    $entryImages = [ordered]@{
        'deck-test-entry-080-enabled.png' = @(819, 512)
        'deck-test-entry-100-enabled.png' = @(1024, 640)
        'deck-test-entry-125-enabled.png' = @(1280, 800)
        'deck-test-entry-150-enabled.png' = @(1536, 960)
        'deck-test-entry-100-pending-disabled.png' = @(1024, 640)
        'deck-test-entry-100-drag-disabled.png' = @(1024, 640)
        'deck-test-entry-100-dialog-disabled.png' = @(1024, 640)
        'deck-test-entry-100-pack-disabled.png' = @(1024, 640)
        'deck-test-entry-100-siding-hidden.png' = @(1024, 640)
    }
    foreach($entry in $entryImages.GetEnumerator()) {
        $path = Join-Path $testDir $entry.Key
        if(!(Test-Path -LiteralPath $path) -or !(Get-Item -LiteralPath $path).Length) { throw "Missing entry image: $path" }
        $png = [IO.File]::ReadAllBytes($path)
        if($png.Length -lt 24 -or $png[0] -ne 137 -or $png[1] -ne 80 -or $png[2] -ne 78 -or $png[3] -ne 71) { throw "Invalid PNG: $path" }
        [uint32]$width = ([uint32]$png[16] -shl 24) -bor ([uint32]$png[17] -shl 16) -bor ([uint32]$png[18] -shl 8) -bor $png[19]
        [uint32]$height = ([uint32]$png[20] -shl 24) -bor ([uint32]$png[21] -shl 16) -bor ([uint32]$png[22] -shl 8) -bor $png[23]
        if($width -ne $entry.Value[0] -or $height -ne $entry.Value[1]) { throw "Unexpected entry image dimensions $($width)x$($height): $path" }
        Get-FileHash -Algorithm SHA256 -LiteralPath $path | Format-List Algorithm,Hash,Path
    }
    Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $runtime 'deck/undo-editor-integration.ydk') | Format-List Algorithm,Hash,Path
} finally {
    if(!$observer.HasExited) { Stop-Process -Id $observer.Id -Force }
}
