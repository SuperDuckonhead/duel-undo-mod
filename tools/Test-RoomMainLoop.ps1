param([string]$RuntimeRoot='F:/MyCardLibrary/ygopro',[ValidateSet('Debug','Release')][string]$Configuration='Release',[ValidateSet('first','tie','second','unchecked')][string]$Scenario='first',[string]$BotDirectory='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'PackageValidation.ps1')
$out=Join-Path $root 'out';Assert-PackagePath $out $root
$botRoot=if($BotDirectory){[IO.Path]::GetFullPath($BotDirectory)}else{Join-Path $root "out/bot/$Configuration"}
Assert-PackagePath (Join-Path $botRoot 'WindBot-undo.exe')
if(-not (Test-Path -LiteralPath (Join-Path $botRoot 'WindBot-undo.exe') -PathType Leaf)){throw 'Selected private bot executable is missing'}
$id=[guid]::NewGuid().ToString('N')
$testDir=Join-Path $out ('room-mainloop-'+$id);Assert-PackagePath $testDir $out
[IO.Directory]::CreateDirectory($testDir)|Out-Null
$profile=Get-Content -LiteralPath (Join-Path $root 'build-profile.json') -Raw|ConvertFrom-Json
$compiler=Join-Path $root $profile.compiler.path
$build=Join-Path $root 'client/build';$makefile=Get-Content -LiteralPath (Join-Path $build 'YGOPro.make') -Raw
$section=[regex]::Match($makefile,('(?s)ifeq \(\$\(config\),'+$Configuration.ToLowerInvariant()+'\)(.*?)(?:else ifeq|endif)')).Groups[1].Value
if(-not $section){throw 'Build client first: missing native object configuration'}
$defs=[regex]::Match($section,'(?m)^DEFINES \+= (.*)$').Groups[1].Value.Trim() -split ' '
$includes=[regex]::Match($makefile,'(?m)^INCLUDES \+= (.*)$').Groups[1].Value.Trim() -split ' '
$libs=[regex]::Match($section,'(?m)^LIBS \+= (.*)$').Groups[1].Value.Trim() -split ' '
$objects=@([regex]::Matches($makefile,'(?m)^OBJECTS \+= \$\(OBJDIR\)/(.*\.o)')|ForEach-Object {$_.Groups[1].Value.Trim()}|Where-Object {$_ -ne 'gframe.o'}|ForEach-Object {Join-Path $root "client/obj/$Configuration/YGOPro/$_"})
$before=@(foreach($file in $objects){[pscustomobject]@{path=$file;sha256=(Get-FileHash -LiteralPath $file).Hash}})
$exe=Join-Path $testDir 'room_mainloop_tests.exe'
Push-Location $build
try{& $compiler '-std=c++17' '-fno-rtti' '-static' '-g' @defs @includes '-I../gframe' '-I../../tests' (Join-Path $root 'tests/room_mainloop_tests.cpp') @objects @libs '-o' $exe *> (Join-Path $testDir 'build.log');if($LASTEXITCODE){throw "MainLoop compile failed: $testDir/build.log"}}finally{Pop-Location}
foreach($file in $before){if((Get-FileHash -LiteralPath $file.path).Hash -ne $file.sha256){throw 'Native objects changed during harness link'}}
[IO.File]::WriteAllText((Join-Path $testDir 'objects.json'),($before|ConvertTo-Json -Depth 3),[Text.UTF8Encoding]::new($false))
New-Item -ItemType Junction -Path (Join-Path $testDir 'WindBot') -Target $botRoot|Out-Null
$stageName='room-mainloop-runtime-'+$id
& (Join-Path $PSScriptRoot 'Prepare-SmokeRuntime.ps1') -RuntimeRoot $RuntimeRoot -OutputName $stageName|Out-Null
$stage=Join-Path $out $stageName
Add-Content -LiteralPath (Join-Path $stage 'system.conf') -Value "enable_bot_mode = 1`nquick_animation = 0" -Encoding utf8
function BindFile([string]$Path){[pscustomobject]@{path=[IO.Path]::GetFullPath($Path);sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}}
$binding=[ordered]@{schemaVersion=1;scenario=$Scenario;runtimeRoot=[IO.Path]::GetFullPath($RuntimeRoot);stage=$stage;testExe=(BindFile $exe);testSource=(BindFile (Join-Path $root 'tests/room_mainloop_tests.cpp'));driverSource=(BindFile $PSCommandPath);clientExeReference=(BindFile (Join-Path $root "out/client/$Configuration/ygopro-undo.exe"));eventHandlerSource=(BindFile (Join-Path $root 'client/gframe/event_handler.cpp'));mainLoopSource=(BindFile (Join-Path $root 'client/gframe/game.cpp'));botExe=(BindFile (Join-Path $botRoot 'WindBot-undo.exe'));objectsFile=(BindFile (Join-Path $testDir 'objects.json'))}
[IO.File]::WriteAllText((Join-Path $testDir 'binding.json'),($binding|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
$child=Start-Process -FilePath $exe -ArgumentList $Scenario -WorkingDirectory $stage -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDir 'run.log') -RedirectStandardError (Join-Path $testDir 'error.log')
$null=$child.Handle
try{
    Write-Host "Actual MainLoop pid=$($child.Id) artifacts=$testDir stage=$stage"
    if(-not $child.WaitForExit(65000)){throw "Actual MainLoop process timeout: $testDir"}
    Get-Content -LiteralPath (Join-Path $testDir 'run.log'),(Join-Path $testDir 'error.log')
    if($child.ExitCode -ne 0){throw "Actual MainLoop failed exit=$($child.ExitCode): $testDir"}
    if((Get-FileHash -LiteralPath $binding.botExe.path).Hash.ToLowerInvariant() -ne $binding.botExe.sha256){throw 'Selected bot executable changed during execution'}
    Write-Host "PASS actual MainLoop: $testDir"
}finally{if(-not $child.HasExited){$child.Kill();[void]$child.WaitForExit(10000)};$child.Dispose()}