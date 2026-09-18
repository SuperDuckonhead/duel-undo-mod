param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [ValidateSet('Debug','Release')][string]$BotConfiguration='Release',
 [string]$RuntimeRoot,
 [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$SkipBuild){& (Join-Path $PSScriptRoot 'Test-RoomClientIntegration.ps1') -Configuration $Configuration -BuildOnly}
$exe=Join-Path $root "out/room-client-integration/$Configuration/room_client_integration_tests.exe"
if(!$RuntimeRoot){$RuntimeRoot=Join-Path $root 'out/baseline-runtime'}
$RuntimeRoot=[IO.Path]::GetFullPath($RuntimeRoot)
if(!(Test-Path -LiteralPath (Join-Path $RuntimeRoot '.undo-smoke-runtime'))){throw "Expected initialized owned runtime: $RuntimeRoot"}
$privateBot=Join-Path $root "out/bot/$BotConfiguration"
if(!(Test-Path -LiteralPath (Join-Path $privateBot 'WindBot-undo.exe'))){throw "Build private bot before running: $privateBot"}
$scriptTarget=(Get-Item -LiteralPath (Join-Path $RuntimeRoot 'script')).Target
if($scriptTarget -is [array]){$scriptTarget=$scriptTarget[0]}
if(!$scriptTarget){throw 'Expected the staging runtime script directory to identify the original resources.'}
$original=Split-Path -Parent $scriptTarget
$stage=Join-Path $root ('out/deck-test-session-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
foreach($name in @('pics','script','expansions','pack','fonts','textures','sound','single')) {
 New-Item -ItemType Junction -Path (Join-Path $stage $name) -Target (Join-Path $RuntimeRoot $name) | Out-Null
}
foreach($name in @('cards.cdb','strings.conf','lflist.conf')) {
 New-Item -ItemType HardLink -Path (Join-Path $stage $name) -Target (Join-Path $RuntimeRoot $name) | Out-Null
}
Copy-Item -LiteralPath (Join-Path $RuntimeRoot 'bot.conf') -Destination (Join-Path $stage 'bot.conf')
foreach($name in @('deck','replay','WindBot')){New-Item -ItemType Directory -Path (Join-Path $stage $name) | Out-Null}
$stageBot=Join-Path $stage 'WindBot'
foreach($name in @('Decks','Dialogs')) {
 New-Item -ItemType Junction -Path (Join-Path $stageBot $name) -Target (Join-Path $original "WindBot/$name") | Out-Null
}
Copy-Item -LiteralPath (Join-Path $original 'WindBot/bots.json') -Destination (Join-Path $stageBot 'bots.json')
foreach($name in @('WindBot-undo.exe','WindBot-undo.exe.config')) {
 Copy-Item -LiteralPath (Join-Path $privateBot $name) -Destination (Join-Path $stageBot $name)
}
if(Test-Path -LiteralPath (Join-Path $privateBot 'undo-deps')) {
 New-Item -ItemType Junction -Path (Join-Path $stageBot 'undo-deps') -Target (Join-Path $privateBot 'undo-deps') | Out-Null
}
if(Test-Path -LiteralPath (Join-Path $original 'WindBot/WindBot.exe.config')) {
 Copy-Item -LiteralPath (Join-Path $original 'WindBot/WindBot.exe.config') -Destination (Join-Path $stageBot 'WindBot.exe.config')
}
[IO.File]::WriteAllText((Join-Path $stage 'system.conf'),"use_d3d = 0`nenable_sound = 0`nenable_music = 0`nenable_bot_mode = 1`n",[Text.UTF8Encoding]::new($false))
$stageExe=Join-Path $stage 'room_client_integration_tests.exe'
Copy-Item -LiteralPath $exe -Destination $stageExe
$output=Join-Path $stage 'session.log'
$errors=Join-Path $stage 'session-error.log'
$process=Start-Process -FilePath $stageExe -ArgumentList '--deck-test-session' -WorkingDirectory $stage -WindowStyle Hidden -RedirectStandardOutput $output -RedirectStandardError $errors -PassThru
$null=$process.Handle
$deadline=[DateTime]::UtcNow.AddSeconds(360)
while(!$process.HasExited -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 200}
if(!$process.HasExited){$process.Kill();throw "Deck-test session timed out: $stage"}
$process.WaitForExit()
Get-Content -LiteralPath $output,$errors
if($process.ExitCode -ne 0){throw "Deck-test session failed ($($process.ExitCode)): $stage"}
Write-Host "PASS actual editor deck-test session: $stage"
