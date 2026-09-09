$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $root 'tools/UndoMeasurementSafety.ps1')
$stage=Join-Path $root ('out/r2-safety-'+[guid]::NewGuid().ToString('N'))
$allowed=Join-Path $stage 'output'
$target=Join-Path $stage 'separate-target'
[IO.Directory]::CreateDirectory($allowed) | Out-Null
[IO.Directory]::CreateDirectory($target) | Out-Null
[IO.File]::WriteAllText((Join-Path $target 'keep.txt'),'unchanged')
$junction=Join-Path $allowed 'linked'
New-Item -ItemType Junction -Path $junction -Target $target | Out-Null
$script:checks=0
function Check([bool]$Ok,[string]$Name){if(-not $Ok){throw $Name};++$script:checks}
function Rejects([scriptblock]$Action,[string]$Pattern='*'){try{& $Action;return $false}catch{return $_.Exception.Message -like $Pattern}}
Check (Rejects {Assert-MeasurementOutputPath (Join-Path $junction 'result.json') $allowed}) 'reject ancestor junction'
Check (Rejects {Assert-MeasurementOutputPath $junction $allowed}) 'reject leaf junction'
Check (Rejects {Assert-MeasurementOutputPath (Join-Path $target 'escape.json') $allowed}) 'reject lexical escape'
Assert-MeasurementOutputPath (Join-Path $allowed 'fresh/nested/result.json') $allowed
++$script:checks
Check (Rejects {Assert-MeasurementOutputTree $allowed $allowed}) 'reject nested build junction'
Check ([IO.File]::ReadAllText((Join-Path $target 'keep.txt')) -eq 'unchanged' -and @(Get-ChildItem -LiteralPath $target).Count -eq 1) 'junction target untouched'
$inputRoot=Join-Path $stage 'input'
[IO.Directory]::CreateDirectory((Join-Path $inputRoot 'nested')) | Out-Null
[IO.File]::WriteAllText((Join-Path $inputRoot 'nested/omitted.h'),'before')
[IO.File]::WriteAllText((Join-Path $inputRoot 'library.lib'),'library1')
[IO.File]::WriteAllText((Join-Path $inputRoot 'flags.make'),'option1')
[IO.File]::WriteAllText((Join-Path $inputRoot 'compiler.exe'),'compiler1')
$before=Get-MeasurementManifest @($inputRoot) @() $stage
foreach($name in @('nested/omitted.h','library.lib','flags.make','compiler.exe')){
 $file=Join-Path $inputRoot $name;$old=[IO.File]::ReadAllText($file);[IO.File]::WriteAllText($file,$old+'change')
 $after=Get-MeasurementManifest @($inputRoot) @() $stage
 Check ($after.sha256 -ne $before.sha256) ('fingerprint changes for '+$name)
 [IO.File]::WriteAllText($file,$old)
}
Check ((Get-MeasurementManifest @($inputRoot) @() $stage).sha256 -eq $before.sha256) 'stable manifest'
Check ($before.files.Count -eq 4) 'manifest preserves individual file identities'
Write-Host "Measurement safety: $script:checks checks passed. Fixture junction remains under $stage and targets only its controlled sibling."
$runtime=Join-Path $stage 'runtime'
[IO.Directory]::CreateDirectory($runtime) | Out-Null
[IO.File]::WriteAllText((Join-Path $runtime 'cards.cdb'),'read-only path-check fixture')
$badOutput=Join-Path $junction 'must-not-exist.json'
Check (Rejects {& (Join-Path $root 'tools/Measure-Undo.ps1') -RuntimeRoot $runtime -OutFile $badOutput} '*reparse point*') 'real runner rejects output junction before configure/log writes'
Check (@(Get-ChildItem -LiteralPath $target).Count -eq 1) 'real runner leaves controlled target untouched'
Write-Host "Including real runner: $script:checks checks passed."
$ordinary=Join-Path $allowed 'ordinary.json'
New-Item -ItemType Junction -Path ($ordinary+'.build.log') -Target $target | Out-Null
Check (Rejects {& (Join-Path $root 'tools/Measure-Undo.ps1') -RuntimeRoot $runtime -OutFile $ordinary} '*reparse point*') 'real runner rejects sidecar junction leaf'
Check (-not (Test-Path -LiteralPath $ordinary) -and @(Get-ChildItem -LiteralPath $target).Count -eq 1) 'sidecar rejection publishes nothing'
Write-Host "Total measurement safety: $script:checks checks passed."
