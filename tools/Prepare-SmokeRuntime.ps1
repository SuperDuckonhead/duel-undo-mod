param(
    [Parameter(Mandatory)][string]$RuntimeRoot,
    [string]$OutputName = 'baseline-runtime'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($OutputName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]*$') { throw 'OutputName must be a simple directory name.' }
$runtime = [IO.Path]::GetFullPath($RuntimeRoot)
if (-not (Test-Path -LiteralPath (Join-Path $runtime 'cards.cdb') -PathType Leaf)) {
    throw "Missing runtime database: $runtime"
}
$out = Join-Path $repoRoot 'out'
if (Test-Path -LiteralPath $out) {
    if ((Get-Item -LiteralPath $out).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Refusing an out directory that is a reparse point.'
    }
}
$stage = Join-Path $out $OutputName
$marker = Join-Path $stage '.undo-smoke-runtime'
if (Test-Path -LiteralPath $stage) {
    throw "Smoke directory already exists; use a fresh OutputName: $stage"
}
$directories = @('pics','script','expansions','pack','fonts','textures','sound','single')
$files = @('cards.cdb','strings.conf','lflist.conf','bot.conf')
foreach ($name in $directories) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $name) -PathType Container)) {
        throw "Missing runtime directory: $name"
    }
}
foreach ($name in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $name) -PathType Leaf)) {
        throw "Missing runtime file: $name"
    }
}
[IO.Directory]::CreateDirectory($stage) | Out-Null
[IO.File]::WriteAllText($marker, "Isolated baseline smoke directory. Resource links refer to: $runtime", [Text.UTF8Encoding]::new($false))
foreach ($name in $directories) {
    New-Item -ItemType Junction -Path (Join-Path $stage $name) -Target (Join-Path $runtime $name) | Out-Null
}
foreach ($name in $files) {
    New-Item -ItemType HardLink -Path (Join-Path $stage $name) -Target (Join-Path $runtime $name) | Out-Null
}
foreach ($name in @('deck','replay','WindBot','WindBot/replay')) {
    [IO.Directory]::CreateDirectory((Join-Path $stage $name)) | Out-Null
}
foreach ($name in @('Decks','Dialogs')) {
    $source = Join-Path $runtime "WindBot/$name"
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Missing WindBot data: $source" }
    New-Item -ItemType Junction -Path (Join-Path $stage "WindBot/$name") -Target $source | Out-Null
}
$botList = Join-Path $runtime 'WindBot/bots.json'
if (Test-Path -LiteralPath $botList -PathType Leaf) {
    New-Item -ItemType HardLink -Path (Join-Path $stage 'WindBot/bots.json') -Target $botList | Out-Null
}
# Use a fresh controlled configuration; never share the installed writable config/deck/replay directories.
$config = @(
    'use_d3d = 0', 'antialias = 0', 'nickname = Undo Baseline',
    'gamename = Undo Baseline', 'window_width = 1024', 'window_height = 640',
    'enable_sound = 0', 'enable_music = 0', 'bot_room_public = 0'
) -join [Environment]::NewLine
[IO.File]::WriteAllText((Join-Path $stage 'system.conf'), $config + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output $stage
