param([string]$IrrlichtRoot = 'client/irrlicht')
$ErrorActionPreference = 'Stop'
function FileHash([string]$Path) {
 $algorithm = [Security.Cryptography.SHA256]::Create()
 try { return ([BitConverter]::ToString($algorithm.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-', '').ToLowerInvariant() }
 finally { $algorithm.Dispose() }
}
# Lexical containment alone does not constrain git apply on Windows: it follows
# junctions. Check every existing component, including the checkout ancestors and
# file leaf, before any source hash/read or patch. Reject traversal rather than
# permitting an alternate resolved dependency tree.
function Assert-PlainPath([string]$Path) {
 $components = [Collections.Generic.Stack[string]]::new()
 $cursor = [IO.Path]::GetFullPath($Path)
 while ($cursor) {
  $components.Push($cursor)
  $parent = [IO.Path]::GetDirectoryName($cursor)
  if ($parent -eq $cursor) { break }
  $cursor = $parent
 }
 while ($components.Count) {
  $component = $components.Pop()
  $item = Get-Item -LiteralPath $component -Force -ErrorAction Stop
  if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
   throw "Irrlicht patch path contains a reparse point: $component"
  }
 }
}
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$target = [IO.Path]::GetFullPath((Join-Path $repository $IrrlichtRoot))
$expected = [IO.Path]::GetFullPath((Join-Path $repository 'client/irrlicht'))
$testRoot = [IO.Path]::GetFullPath((Join-Path $repository 'out')) + [IO.Path]::DirectorySeparatorChar
if ($target -ne $expected -and -not $target.StartsWith($testRoot, [StringComparison]::OrdinalIgnoreCase)) {
 throw "Irrlicht patch target must be this checkout's client/irrlicht or out test directory: $target"
}
Assert-PlainPath $target
$patchRoot = Join-Path $PSScriptRoot 'patches/irrlicht-undo'
$manifest = Get-Content -LiteralPath (Join-Path $patchRoot 'manifest.json') -Raw | ConvertFrom-Json
# Validate every path before reading any source, then preflight all hashes.
foreach ($entry in $manifest) { Assert-PlainPath (Join-Path $target $entry.file) }
# Validate all inputs before modifying any file. Refuse unrecognized source edits.
foreach ($entry in $manifest) {
 $path = Join-Path $target $entry.file
 $hash = (FileHash $path)
 if ($hash -ne $entry.before -and $hash -ne $entry.after) { throw "Unrecognized Irrlicht source: $path ($hash)" }
}
Push-Location -LiteralPath $repository
try {
 $relative = $target.Substring($repository.Length + 1).Replace('\','/')
 foreach ($entry in $manifest) {
  $path = Join-Path $target $entry.file
  if ((FileHash $path) -eq $entry.after) { continue }
  $patch = Join-Path $patchRoot $entry.patch
  & git apply --check "--directory=$relative" -- $patch
  if ($LASTEXITCODE -ne 0) { throw "Irrlicht patch check failed: $patch" }
  & git apply "--directory=$relative" -- $patch
  if ($LASTEXITCODE -ne 0) { throw "Irrlicht patch failed: $patch" }
  if ((FileHash $path) -ne $entry.after) { throw "Irrlicht patched hash mismatch: $path" }
 }
} finally { Pop-Location }
Write-Host 'Verified transactional Irrlicht focus patch (3 exact source hashes).'