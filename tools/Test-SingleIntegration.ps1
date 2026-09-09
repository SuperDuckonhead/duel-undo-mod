param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [string]$RuntimeRoot)
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
$testDir = Join-Path $root "out/single-integration/$Configuration"
New-Item -ItemType Directory -Force -Path $testDir | Out-Null
$exe = Join-Path $testDir 'single_mode_integration_tests.exe'
Push-Location $build
try {
    & $compiler '-std=c++17' '-fno-rtti' '-static' '-g' @defs @includes '-I../gframe' '-I../../tests' (Join-Path $root 'tests/single_mode_integration_tests.cpp') @objects @libs '-o' $exe
    if($LASTEXITCODE) { throw "Integration compile failed: $LASTEXITCODE" }
} finally { Pop-Location }
$runtime = Join-Path $root 'out/c4-runtime'
if(!(Test-Path -LiteralPath $runtime)) {
 if(!$RuntimeRoot) { throw 'Supply -RuntimeRoot with the installed read-only resource directory for first run.' }
 $source=[IO.Path]::GetFullPath($RuntimeRoot)
 if(!(Test-Path -LiteralPath (Join-Path $source 'cards.cdb'))) { throw 'Missing installed cards.cdb.' }
 New-Item -ItemType Directory -Path $runtime | Out-Null
 foreach($name in @('pics','script','expansions','pack','fonts','textures','sound')) { New-Item -ItemType Junction -Path (Join-Path $runtime $name) -Target (Join-Path $source $name) | Out-Null }
 foreach($name in @('cards.cdb','strings.conf','lflist.conf','bot.conf')) { New-Item -ItemType HardLink -Path (Join-Path $runtime $name) -Target (Join-Path $source $name) | Out-Null }
 foreach($name in @('single','deck','replay')) { New-Item -ItemType Directory -Path (Join-Path $runtime $name) | Out-Null }
 [IO.File]::WriteAllText((Join-Path $runtime 'system.conf'),"use_d3d = 0`nantialias = 0`nnickname = C4 Test`nenable_sound = 0`nenable_music = 0`n",[Text.UTF8Encoding]::new($false))
 [IO.File]::WriteAllText((Join-Path $runtime '.undo-smoke-runtime'),"C4 controlled scenario; installed resources read-only links to $source")
}
if(!(Test-Path -LiteralPath (Join-Path $runtime '.undo-smoke-runtime'))) { throw 'Expected owned C4 staging marker.' }
$singleDirectory = Join-Path $runtime 'single'
if((Get-Item -LiteralPath $singleDirectory).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'C4 scenario directory must be owned staging storage, not a resource link.' }
Copy-Item -LiteralPath (Join-Path $root 'tests/fixtures/duel/c4-single.lua') -Destination (Join-Path $singleDirectory 'c4-single.lua') -Force
Push-Location $runtime
try { & $exe; if($LASTEXITCODE) { throw "Single integration failed: $LASTEXITCODE" } } finally { Pop-Location }
