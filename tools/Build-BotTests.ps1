param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$dotnet = Join-Path $repoRoot '.cache/tools/dotnet/dotnet.exe'
$refs = Join-Path $repoRoot '.cache/net48-ref/build'
$botOutput = Join-Path $repoRoot "out/bot/$Configuration"
$testOutput = Join-Path $repoRoot 'out/bot-tests'
$targets = Join-Path $repoRoot 'tools/Bot.Build.targets'
& $dotnet msbuild (Join-Path $repoRoot 'bot/WindBot.csproj') /t:Rebuild "/p:Configuration=$Configuration" /p:Platform=AnyCPU /p:UndoBuild=true "/p:TargetFrameworkRootPath=$refs" "/p:OutputPath=$botOutput" "/p:CustomAfterMicrosoftCommonTargets=$targets" /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Adapted WindBot build failed' }
& $dotnet msbuild (Join-Path $repoRoot 'bot-tests/UndoTests.csproj') /t:Rebuild "/p:Configuration=$Configuration" "/p:TargetFrameworkRootPath=$refs" "/p:BotOutputPath=$botOutput" "/p:OutputPath=$testOutput" /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Bot tests build failed' }
# Only build outputs/native dependencies, never runtime Decks/Dialogs/databases.
Copy-Item -LiteralPath (Join-Path $botOutput 'WindBot-undo.exe.config') -Destination $testOutput -Force
foreach ($arch in @('x86','x64')) {
    $destination = Join-Path $testOutput $arch
    [IO.Directory]::CreateDirectory($destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $botOutput "$arch/sqlite3.dll") -Destination $destination -Force
}
Write-Host "W1 build completed ($Configuration): $testOutput/UndoTests.exe"