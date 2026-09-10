param([string]$RuntimeRoot='F:/MyCardLibrary/ygopro',[ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$WorkingHandler)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'PackageValidation.ps1')
$out=Join-Path $root 'out';Assert-PackagePath $out $root
$id=[guid]::NewGuid().ToString('N')
$testDir=Join-Path $out ('duel-shortcut-'+$id);Assert-PackagePath $testDir $out
[IO.Directory]::CreateDirectory($testDir)|Out-Null
$profile=Get-Content -LiteralPath (Join-Path $root 'build-profile.json') -Raw|ConvertFrom-Json
$compiler=Join-Path $root $profile.compiler.path
if(-not(Test-Path -LiteralPath $compiler)){$compiler=Join-Path $root '.cache/tools/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe'}
$build=Join-Path $root 'client/build';$makefile=Get-Content -Raw (Join-Path $build 'YGOPro.make')
$section=[regex]::Match($makefile,('(?s)ifeq \(\$\(config\),'+$Configuration.ToLowerInvariant()+'\)(.*?)(?:else ifeq|endif)')).Groups[1].Value
if(!$section){throw 'Build the selected native configuration first.'}
$defs=[regex]::Match($section,'(?m)^DEFINES \+= (.*)$').Groups[1].Value.Trim() -split ' '
$includes=[regex]::Match($makefile,'(?m)^INCLUDES \+= (.*)$').Groups[1].Value.Trim() -split ' '
$libs=[regex]::Match($section,'(?m)^LIBS \+= (.*)$').Groups[1].Value.Trim() -split ' '
$objects=[regex]::Matches($makefile,'(?m)^OBJECTS \+= \$\(OBJDIR\)/(.*\.o)')|ForEach-Object{$_.Groups[1].Value.Trim()}|Where-Object{$_ -notin @('gframe.o','duelclient.o') -and (!$WorkingHandler -or $_ -ne 'event_handler.o')}|ForEach-Object{Join-Path $root "client/obj/$Configuration/YGOPro/$_"}
$exe=Join-Path $testDir 'duel_undo_shortcut_tests.exe'
Push-Location $build
try{
 if($WorkingHandler){$local=Join-Path $testDir 'event_handler.o';& $compiler '-std=c++17' '-fno-rtti' '-g' @defs @includes '-I../gframe' '-c' '../gframe/event_handler.cpp' '-o' $local;if($LASTEXITCODE){throw 'Local handler compile failed'};$objects+=@($local)}
 & $compiler '-std=c++17' '-fno-rtti' '-static' '-g' @defs @includes '-I../gframe' '-I../../tests' (Join-Path $root 'tests/duel_undo_shortcut_tests.cpp') @objects @libs '-o' $exe
 if($LASTEXITCODE){throw 'Shortcut harness compile failed'}
}finally{Pop-Location}
$stageName='duel-shortcut-runtime-'+$id
& (Join-Path $PSScriptRoot 'Prepare-SmokeRuntime.ps1') -RuntimeRoot $RuntimeRoot -OutputName $stageName|Out-Null
$stage=Join-Path $out $stageName
# Replace only the newly-created staging junction itself; never its target.
$single=Join-Path $stage 'single'
if(-not ((Get-Item -LiteralPath $single).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Expected new smoke single junction'}
[IO.Directory]::Delete($single)
[IO.Directory]::CreateDirectory($single)|Out-Null
Copy-Item -LiteralPath (Join-Path $root 'tests/fixtures/duel/c4-single.lua') -Destination (Join-Path $single 'c4-single.lua')
$child=Start-Process -FilePath $exe -WorkingDirectory $stage -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDir 'run.log') -RedirectStandardError (Join-Path $testDir 'error.log')
$null=$child.Handle
try{
 if(-not $child.WaitForExit(60000)){throw 'Shortcut test timed out'}
 Get-Content (Join-Path $testDir 'run.log');Get-Content (Join-Path $testDir 'error.log')
 Write-Host "Shortcut evidence: $testDir"
 if($child.ExitCode -ne 0){throw "Shortcut test failed: $($child.ExitCode)"}
}finally{if(-not $child.HasExited){$child.Kill();[void]$child.WaitForExit(10000)};$child.Dispose()}
