param([string]$Binary = 'out/client/Release/ygopro-undo.exe')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$reader = Join-Path $root '.cache/tools/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-readobj.exe'
$imports = & $reader --coff-imports (Join-Path $root $Binary)
if($LASTEXITCODE) { throw 'Cannot inspect PE imports' }
$names = @($imports | Select-String '^  Name: (.+)$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
if(!$names.Count) { throw 'No PE imports found' }
foreach($name in $names) {
    if($name -notmatch '^(api-ms-win-.*|WS2_32|WINMM|bcrypt|SHELL32|USER32|KERNEL32|IPHLPAPI|ADVAPI32|IMM32|GDI32|OPENGL32|OLE32|OLEAUT32|COMDLG32|COMCTL32|VERSION|DINPUT8|DXGUID|DSOUND|SETUPAPI)\.dll$') {
        throw "Unisolated startup dependency: $name"
    }
}
Write-Output "PASS PE imports contain only Windows system libraries: $($names -join ', ')"
