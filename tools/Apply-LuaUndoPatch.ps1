param([string]$LuaRoot = 'client/lua')
$ErrorActionPreference = 'Stop'
function FileHash([string]$Path) {
 $algorithm = [Security.Cryptography.SHA256]::Create()
 try { return ([BitConverter]::ToString($algorithm.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-', '').ToLowerInvariant() }
 finally { $algorithm.Dispose() }
}
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$target = [IO.Path]::GetFullPath((Join-Path $repository $LuaRoot))
$expected = [IO.Path]::GetFullPath((Join-Path $repository 'client/lua'))
$testRoot = [IO.Path]::GetFullPath((Join-Path $repository 'out')) + [IO.Path]::DirectorySeparatorChar
if ($target -ne $expected -and -not $target.StartsWith($testRoot, [StringComparison]::OrdinalIgnoreCase)) {
 throw "Lua patch target must be this checkout's client/lua or out test directory: $target"
}
$patchRoot = Join-Path $PSScriptRoot 'patches/lua-undo'
$manifest = Get-Content -LiteralPath (Join-Path $patchRoot 'manifest.json') -Raw | ConvertFrom-Json
# Validate all inputs before modifying any file. Refuse unrecognized source edits.
foreach ($entry in $manifest) {
 $path = Join-Path $target $entry.file
 $hash = (FileHash $path)
 if ($hash -ne $entry.before -and $hash -ne $entry.after) { throw "Unrecognized Lua source: $path ($hash)" }
}
Push-Location -LiteralPath $repository
try {
 $relative = $target.Substring($repository.Length + 1).Replace('\','/')
 foreach ($entry in $manifest) {
  $path = Join-Path $target $entry.file
  if ((FileHash $path) -eq $entry.after) { continue }
  $patch = Join-Path $patchRoot $entry.patch
  & git -c core.autocrlf=false apply --check "--directory=$relative" -- $patch
  if ($LASTEXITCODE -ne 0) { throw "Lua patch check failed: $patch" }
  & git -c core.autocrlf=false apply "--directory=$relative" -- $patch
  if ($LASTEXITCODE -ne 0) { throw "Lua patch failed: $patch" }
  if ((FileHash $path) -ne $entry.after) { throw "Lua patched hash mismatch: $path" }
 }
} finally { Pop-Location }
Write-Host 'Verified deterministic Lua constructor patch (4 exact source hashes).'