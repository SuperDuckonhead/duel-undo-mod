param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$OutputDirectory = 'out/baseline-bot',
    [string]$DotNetPath = '.cache/tools/dotnet/dotnet.exe',
    [string]$ReferenceAssembliesRoot = '.cache/net48-ref/build'
)

$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'out'))

function Resolve-RepositoryPath {
    param([Parameter(Mandatory)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

$outputPath = Resolve-RepositoryPath $OutputDirectory
$allowedPrefix = $outRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $outputPath.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDirectory must be inside '$outRoot'."
}

$dotnet = Resolve-RepositoryPath $DotNetPath
if (-not (Test-Path -LiteralPath $dotnet -PathType Leaf)) {
    throw "Portable .NET SDK not found: $dotnet"
}

$frameworkRoot = Resolve-RepositoryPath $ReferenceAssembliesRoot
$mscorlib = Join-Path $frameworkRoot '.NETFramework/v4.8/mscorlib.dll'
if (-not (Test-Path -LiteralPath $mscorlib -PathType Leaf)) {
    throw "Official .NET Framework 4.8 reference assemblies not found: $frameworkRoot"
}

$solution = Join-Path $repoRoot 'bot/WindBot.sln'
$afterTargets = Join-Path $repoRoot 'tools/Bot.Build.targets'
foreach ($requiredPath in @($solution, $afterTargets)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required bot build input not found: $requiredPath"
    }
}

$runtimeOwnedNames = @('Decks', 'Dialogs', 'bots.json', 'bot.conf', 'cards.cdb')
$contaminants = @($runtimeOwnedNames | Where-Object {
    Test-Path -LiteralPath (Join-Path $outputPath $_)
})
if ($contaminants.Count -gt 0) {
    throw "Output directory contains runtime-owned assets and was left unchanged: $($contaminants -join ', ')"
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$arguments = @(
    'msbuild',
    $solution,
    '/t:Rebuild',
    "/p:Configuration=$Configuration",
    '/p:Platform=Any CPU',
    # Relative paths remain per-project inside the solution (WindBot and BotWrapper).
    '/p:BaseIntermediateOutputPath=obj/normal/',
    "/p:IntermediateOutputPath=obj/normal/$Configuration/",
    "/p:TargetFrameworkRootPath=$frameworkRoot",
    "/p:OutputPath=$outputPath",
    "/p:CustomAfterMicrosoftCommonTargets=$afterTargets",
    '/v:minimal'
)

Write-Host "Building pinned WindBot ($Configuration, Any CPU) into $outputPath"
& $dotnet @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Bot build failed with exit code $LASTEXITCODE."
}

foreach ($fileName in @('WindBot.exe', 'Bot.exe', 'WindBot.exe.config', 'x86/sqlite3.dll', 'x64/sqlite3.dll')) {
    $artifact = Join-Path $outputPath $fileName
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "Expected bot build artifact not found: $artifact"
    }
}

Write-Host "Bot build completed: $outputPath"
