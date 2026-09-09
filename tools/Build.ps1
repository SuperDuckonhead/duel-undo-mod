param(
    [ValidateSet('Client', 'Bot', 'Tests', 'All')]
    [string]$Target = 'All',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$profilePath = Join-Path $root 'build-profile.json'
$profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
$selectedCount = 0

foreach ($command in @($profile.commands)) {
    if ($Target -ne 'All' -and @($command.targets) -notcontains $Target) { continue }
    if ($command.configuration -and $command.configuration -ne $Configuration) { continue }

    $workingDirectory = [IO.Path]::GetFullPath((Join-Path $root ([string]$command.workingDirectory)))
    if (-not (Test-Path -LiteralPath $workingDirectory -PathType Container)) {
        throw "Build working directory does not exist: $workingDirectory"
    }

    $buildProgram = [string]$command.program
    if (-not [IO.Path]::IsPathRooted($buildProgram)) {
        $repositoryProgram = [IO.Path]::GetFullPath((Join-Path $root $buildProgram))
        if (Test-Path -LiteralPath $repositoryProgram -PathType Leaf) {
            $buildProgram = $repositoryProgram
        }
    }
    $portableRoot = $root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar).Replace('\', '/')
    $buildArguments = @($command.arguments | ForEach-Object { ([string]$_).Replace('{repoRoot}', $portableRoot) })
    ++$selectedCount

    Push-Location -LiteralPath $workingDirectory
    try {
        & $buildProgram @buildArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Build command failed with exit code $LASTEXITCODE : $($command.program)"
        }
    }
    finally {
        Pop-Location
    }
}

if ($selectedCount -eq 0) {
    throw "No matching build commands for target '$Target' and configuration '$Configuration'."
}
