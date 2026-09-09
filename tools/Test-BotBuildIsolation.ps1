param([ValidateSet('Debug', 'Release')][string[]]$Configuration = @('Debug', 'Release'))
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$logRoot = Join-Path $repoRoot 'out/w1-build-isolation'
[IO.Directory]::CreateDirectory($logRoot) | Out-Null

function Invoke-BuildCheck {
    param([string]$Script, [string[]]$BuildArguments, [string]$LogName)
    $logPath = Join-Path $logRoot $LogName
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot $Script) @BuildArguments *> $logPath
    if ($LASTEXITCODE -ne 0) { throw "Build failed; see $logPath" }
}

function Require-BotArtifacts {
    param([string]$OutputPath, [string]$Stage)
    foreach ($name in @('WindBot.exe', 'WindBot.exe.config', 'WindBot-undo.exe', 'WindBot-undo.exe.config', 'Bot.exe', 'Bot.exe.config', 'x86/sqlite3.dll', 'x64/sqlite3.dll', 'undo-deps/x86/sqlite3.dll', 'undo-deps/x64/sqlite3.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $OutputPath $name) -PathType Leaf)) {
            throw "$Stage lost required artifact: $name"
        }
    }
    Write-Output "PASS $Stage : both WindBot executables, wrapper, configs, and SQLite dependencies coexist"
}

function Snapshot-Artifacts {
    param([string]$OutputPath, [string[]]$Names)
    $hashes = @{}
    foreach ($name in $Names) { $hashes[$name] = (Get-FileHash -LiteralPath (Join-Path $OutputPath $name) -Algorithm SHA256).Hash }
    return $hashes
}

function Require-Unchanged {
    param([string]$OutputPath, [hashtable]$Hashes, [string]$Stage)
    foreach ($name in $Hashes.Keys) {
        $path = Join-Path $OutputPath $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $Hashes[$name]) {
            throw "$Stage modified or removed the other build's artifact: $name"
        }
    }
    Write-Output "PASS $Stage : other build's artifact hashes unchanged"
}

foreach ($buildConfiguration in $Configuration) {
    $outputPath = Join-Path $repoRoot "out/bot/$buildConfiguration"
    # The registered profile executes normal first, then adapted, in their shared final directory.
    Invoke-BuildCheck 'tools/Build.ps1' @('-Target', 'Bot', '-Configuration', $buildConfiguration) "$buildConfiguration-profile.log"
    Require-BotArtifacts $outputPath "$buildConfiguration combined Bot profile"

    $adaptedHashes = Snapshot-Artifacts $outputPath @('WindBot-undo.exe', 'WindBot-undo.exe.config', 'undo-deps/x86/sqlite3.dll', 'undo-deps/x64/sqlite3.dll')
    # Reverse order: rebuild normal after adapted already exists.
    Invoke-BuildCheck 'tools/Build-Bot.ps1' @('-Configuration', $buildConfiguration, '-OutputDirectory', "out/bot/$buildConfiguration") "$buildConfiguration-normal-rebuild.log"
    Require-BotArtifacts $outputPath "$buildConfiguration adapted -> normal rebuild"
    Require-Unchanged $outputPath $adaptedHashes "$buildConfiguration normal rebuild"

    $normalHashes = Snapshot-Artifacts $outputPath @('WindBot.exe', 'WindBot.exe.config', 'Bot.exe', 'Bot.exe.config', 'x86/sqlite3.dll', 'x64/sqlite3.dll')
    Invoke-BuildCheck 'tools/Build-BotTests.ps1' @('-Configuration', $buildConfiguration) "$buildConfiguration-adapted-rebuild.log"
    Require-BotArtifacts $outputPath "$buildConfiguration repeated adapted rebuild"
    Require-Unchanged $outputPath $normalHashes "$buildConfiguration adapted rebuild"
}
