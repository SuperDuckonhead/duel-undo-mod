param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [string]$RuntimeRoot, [switch]$Pair, [switch]$Ai, [switch]$FreePair, [switch]$TimedPair)
$ErrorActionPreference = 'Stop'
if($FreePair -and $Ai){throw 'FreePair and Ai are distinct test modes.'}
if($TimedPair -and $Ai){throw 'TimedPair and Ai are distinct test modes.'}
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
$testDir = Join-Path $root "out/room-client-integration/$Configuration"
New-Item -ItemType Directory -Force -Path $testDir | Out-Null
$exe = Join-Path $testDir 'room_client_integration_tests.exe'
Push-Location $build
try {
    & $compiler '-std=c++17' '-fno-rtti' '-static' '-g' @defs @includes '-I../gframe' '-I../../tests' (Join-Path $root 'tests/room_client_integration_tests.cpp') @objects @libs '-o' $exe
    if($LASTEXITCODE) { throw "Integration compile failed: $LASTEXITCODE" }
} finally { Pop-Location }
$runtime = Join-Path $root 'out/c4-runtime'
if(!(Test-Path -LiteralPath (Join-Path $runtime '.undo-smoke-runtime'))) { throw 'Expected initialized owned C4 staging runtime.' }
# Capture fixtures own configuration/custom bytes; fixed AI resources remain
# read-only links. Never write through a runtime/resource junction.
$botFixture=Join-Path $runtime 'WindBot'
if(!(Test-Path -LiteralPath $botFixture)){New-Item -ItemType Directory -Path $botFixture | Out-Null}
if((Get-Item -LiteralPath $botFixture).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Bot fixture must be owned staging storage.'}
$scriptTarget=(Get-Item -LiteralPath (Join-Path $runtime 'script')).Target
if($scriptTarget -is [array]){$scriptTarget=$scriptTarget[0]}
$original=Split-Path -Parent $scriptTarget
foreach($name in @('Decks','Dialogs')){if(!(Test-Path -LiteralPath (Join-Path $botFixture $name))){New-Item -ItemType Junction -Path (Join-Path $botFixture $name) -Target (Join-Path $original "WindBot/$name") | Out-Null}}
if(!(Test-Path -LiteralPath (Join-Path $botFixture 'bots.json'))){New-Item -ItemType HardLink -Path (Join-Path $botFixture 'bots.json') -Target (Join-Path $original 'WindBot/bots.json') | Out-Null}
$privateBot=Join-Path $testDir 'WindBot'
if(!(Test-Path -LiteralPath $privateBot)){New-Item -ItemType Junction -Path $privateBot -Target (Join-Path $root "out/bot/$Configuration") | Out-Null}
if($Pair -or $Ai -or $FreePair -or $TimedPair) {
 $pairRoot=Join-Path $root ('out/room-pair-'+[Guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $pairRoot | Out-Null
 $roles=if($Ai){@('ai')}else{@('host','guest')}
 foreach($role in $roles) {
  $stage=Join-Path $pairRoot $role;New-Item -ItemType Directory -Path $stage | Out-Null
  foreach($name in @('pics','script','expansions','pack','fonts','textures','sound','single')){New-Item -ItemType Junction -Path (Join-Path $stage $name) -Target (Join-Path $runtime $name) | Out-Null}
  foreach($name in @('cards.cdb','strings.conf','lflist.conf','bot.conf')){New-Item -ItemType HardLink -Path (Join-Path $stage $name) -Target (Join-Path $runtime $name) | Out-Null}
  foreach($name in @('deck','replay')){New-Item -ItemType Directory -Path (Join-Path $stage $name) | Out-Null}
  [IO.File]::WriteAllText((Join-Path $stage 'system.conf'),"use_d3d = 0`nenable_sound = 0`nenable_music = 0`nenable_bot_mode = 1`n",[Text.UTF8Encoding]::new($false))
 }
 if($Ai) {
  $stage=Join-Path $pairRoot 'ai'
  New-Item -ItemType Junction -Path (Join-Path $stage 'WindBot') -Target (Join-Path $original 'WindBot') | Out-Null
  $process=Start-Process -FilePath $exe -ArgumentList @('--ai',('"'+$stage+'"')) -WorkingDirectory $stage -WindowStyle Hidden -RedirectStandardOutput (Join-Path $pairRoot 'ai.log') -RedirectStandardError (Join-Path $pairRoot 'ai-error.log') -PassThru
  $null=$process.Handle
  if(!$process.WaitForExit(180000)){$process.Kill();throw "AI Game timed out: $pairRoot"}
  Get-Content -LiteralPath (Join-Path $pairRoot 'ai.log'),(Join-Path $pairRoot 'ai-error.log')
  if($process.ExitCode -ne 0){throw "AI Game failed ($($process.ExitCode)): $pairRoot"}
  Write-Host "PASS actual AI menu Game: $pairRoot"
  exit 0
 }
 $pairModeArgs=if($FreePair){@('--free')}else{@()}
 if($TimedPair){$pairModeArgs+=@('--timed')}
 $hostProcess=Start-Process -FilePath $exe -ArgumentList (@('--pair-host',('"'+$pairRoot+'"')) + $pairModeArgs) -WorkingDirectory (Join-Path $pairRoot 'host') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $pairRoot 'host.log') -RedirectStandardError (Join-Path $pairRoot 'host-error.log') -PassThru
 $guestProcess=Start-Process -FilePath $exe -ArgumentList (@('--pair-guest',('"'+$pairRoot+'"')) + $pairModeArgs) -WorkingDirectory (Join-Path $pairRoot 'guest') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $pairRoot 'guest.log') -RedirectStandardError (Join-Path $pairRoot 'guest-error.log') -PassThru
 $null=$hostProcess.Handle; $null=$guestProcess.Handle
 $deadline=[DateTime]::UtcNow.AddSeconds(110)
 while((!$hostProcess.HasExited -or !$guestProcess.HasExited) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 200}
 foreach($process in @($hostProcess,$guestProcess)){if(!$process.HasExited){$process.Kill();throw "Paired Game timed out: $pairRoot"}}
 $hostProcess.WaitForExit(); $guestProcess.WaitForExit()
 Write-Host "Pair exit codes host=$($hostProcess.ExitCode) guest=$($guestProcess.ExitCode)"
 Get-Content -LiteralPath (Join-Path $pairRoot 'host.log'),(Join-Path $pairRoot 'host-error.log'),(Join-Path $pairRoot 'guest.log'),(Join-Path $pairRoot 'guest-error.log')
 if($hostProcess.ExitCode -ne 0 -or $guestProcess.ExitCode -ne 0){throw "Paired Game failed: $pairRoot"}
 Write-Host "PASS paired actual Game processes (FreePair=$FreePair, TimedPair=$TimedPair): $pairRoot"
 exit 0
}
Push-Location $runtime
try { & $exe; if($LASTEXITCODE) { throw "Room client integration failed: $LASTEXITCODE" } } finally { Pop-Location }
