$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run = Join-Path $repository ('out/irrlicht-patch-tests-' + [Guid]::NewGuid().ToString('N'))
$fixture = Join-Path $run 'checkout'
$null = New-Item -ItemType Directory -Path (Join-Path $fixture 'tools/patches') -Force
Copy-Item -LiteralPath (Join-Path $repository 'tools/Apply-IrrlichtUndoPatch.ps1') -Destination (Join-Path $fixture 'tools')
Copy-Item -LiteralPath (Join-Path $repository 'tools/patches/irrlicht-undo') -Destination (Join-Path $fixture 'tools/patches') -Recurse
$manifest = Get-Content -LiteralPath (Join-Path $fixture 'tools/patches/irrlicht-undo/manifest.json') -Raw | ConvertFrom-Json
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert([bool]$Condition,[string]$Message) { if(-not $Condition) { throw $Message } }
function New-Pristine([string]$Relative) {
    $root = Join-Path $fixture $Relative
    foreach($entry in $manifest) {
        $dest = Join-Path $root $entry.file
        $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($dest)) -Force
        Copy-Item -LiteralPath (Join-Path $repository ('client/irrlicht/' + $entry.file)) -Destination $dest
        Assert ((Hash $dest) -eq $entry.after) "Installed fixture source is not the pinned patched version: $dest"
    }
    Push-Location -LiteralPath $fixture
    try {
        foreach($entry in $manifest) {
            & git apply -R "--directory=$($Relative.Replace('\','/'))" -- (Join-Path $fixture ('tools/patches/irrlicht-undo/' + $entry.patch))
            if($LASTEXITCODE -ne 0) { throw 'Fixture reverse patch failed' }
        }
    } finally { Pop-Location }
    foreach($entry in $manifest) { Assert ((Hash (Join-Path $root $entry.file)) -eq $entry.before) 'Fixture pristine hash mismatch' }
    return $root
}
function Invoke-Helper([string]$Relative) {
    & (Join-Path $fixture 'tools/Apply-IrrlichtUndoPatch.ps1') -IrrlichtRoot $Relative
}
function Expect-Rejected([string]$Relative,[string]$SentinelRoot) {
    $before = @($manifest | ForEach-Object { Hash (Join-Path $SentinelRoot $_.file) })
    $errorText = ''
    try { Invoke-Helper $Relative } catch { $errorText = $_.Exception.Message }
    $after = @($manifest | ForEach-Object { Hash (Join-Path $SentinelRoot $_.file) })
    Assert (($before -join ',') -eq ($after -join ',')) "REPARSE ESCAPE MODIFIED SENTINELS: $Relative"
    Assert ($errorText -match 'reparse point') "Expected reparse rejection for $Relative; got: $errorText"
    Write-Host "PASS rejected unchanged: $Relative"
}
# Every tree is a uniquely owned copy beneath out. The fixture's .cache is
# intentionally outside the actual helper's declared out/client target roots.
$outside = New-Pristine '.cache/target'
$null = New-Item -ItemType Directory -Path (Join-Path $fixture 'out') -Force
$null = New-Item -ItemType Junction -Path (Join-Path $fixture 'out/root-link') -Target $outside
Expect-Rejected 'out/root-link' $outside
$null = New-Item -ItemType Junction -Path (Join-Path $fixture 'out/parent-link') -Target (Join-Path $fixture '.cache')
Expect-Rejected 'out/parent-link/target' $outside
$nested = New-Pristine 'out/nested'
[IO.Directory]::Move((Join-Path $nested 'source'), (Join-Path $fixture '.cache/nested-source'))
$null = New-Item -ItemType Junction -Path (Join-Path $nested 'source') -Target (Join-Path $fixture '.cache/nested-source')
Expect-Rejected 'out/nested' $nested
$include = New-Pristine 'out/include-link'
[IO.Directory]::Move((Join-Path $include 'include'), (Join-Path $fixture '.cache/nested-include'))
$null = New-Item -ItemType Junction -Path (Join-Path $include 'include') -Target (Join-Path $fixture '.cache/nested-include')
Expect-Rejected 'out/include-link' $include
$leaf = New-Pristine 'out/leaf'
$leafSource = Join-Path $leaf 'include/IGUIEnvironment.h'
$leafTarget = Join-Path $fixture '.cache/leaf.h'
[IO.File]::Move($leafSource, $leafTarget)
# A junction at the source-file name exercises the same leaf ReparsePoint
# guard without requiring the Windows file-symbolic-link privilege.
$null = New-Item -ItemType Junction -Path $leafSource -Target (Join-Path $outside 'include')
Expect-Rejected 'out/leaf' $outside
Assert ((Hash $leafTarget) -eq $manifest[0].before) 'Moved source sentinel changed'
# The exact default dependency root must be subject to the same checks.
$null = New-Item -ItemType Directory -Path (Join-Path $fixture 'client') -Force
$null = New-Item -ItemType Junction -Path (Join-Path $fixture 'client/irrlicht') -Target $outside
Expect-Rejected 'client/irrlicht' $outside
# Delete only the owned junction object, never its target or a recursive tree.
[IO.Directory]::Delete((Join-Path $fixture 'client/irrlicht'))
foreach($relative in @('client/irrlicht','out/ordinary')) {
    $normal = New-Pristine $relative
    Invoke-Helper $relative
    Invoke-Helper $relative
    foreach($entry in $manifest) { Assert ((Hash (Join-Path $normal $entry.file)) -eq $entry.after) 'Normal apply/idempotence mismatch' }
    Write-Host "PASS normal apply and idempotence: $relative"
}
$tampered = New-Pristine 'out/tampered'
[IO.File]::AppendAllText((Join-Path $tampered $manifest[1].file), '// controlled tamper')
$before = @($manifest | ForEach-Object { Hash (Join-Path $tampered $_.file) })
$errorText = ''
try { Invoke-Helper 'out/tampered' } catch { $errorText = $_.Exception.Message }
$after = @($manifest | ForEach-Object { Hash (Join-Path $tampered $_.file) })
Assert ($errorText -match 'Unrecognized Irrlicht source') 'Expected pinned hash rejection'
Assert (($before -join ',') -eq ($after -join ',')) 'Hash preflight partially modified sources'
Write-Host 'PASS unrecognized source rejects before any patch'
Write-Host "PASS Irrlicht patch containment; owned fixtures: $run"
