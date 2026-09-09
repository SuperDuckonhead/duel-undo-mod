$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = Join-Path $root 'out\client\Release'
$executable = Join-Path $output 'ygopro-undo.exe'

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'Release client output is missing'
}
foreach ($runtime in @('libc++.dll', 'libunwind.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $output $runtime) -PathType Leaf)) {
        throw "Portable compiler runtime is missing from client output: $runtime"
    }
}

$objdump = Join-Path $root '.cache\tools\llvm-mingw-20260908-ucrt-x86_64\bin\llvm-objdump.exe'
$headers = (& $objdump -p $executable) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect built client PE headers' }
foreach ($requiredImport in @('IMM32.dll', 'OPENGL32.dll', 'WINMM.dll')) {
    if ($headers -notmatch [regex]::Escape($requiredImport)) {
        throw "Built client lacks required feature import: $requiredImport"
    }
}

$machine = (& $objdump -f $executable) -join "`n"
if ($machine -notmatch 'coff-x86-64') { throw 'Built client is not x86-64 PE/COFF' }

Write-Host 'Built client output contract passed.'
