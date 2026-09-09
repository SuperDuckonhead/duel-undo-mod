param(
    [Parameter(Mandatory=$true)][string]$RuntimeRoot,
    [string]$Cases = '10,100,1000',
    [string]$OutFile = 'out/performance/core.json',
    [ValidateRange(1,100)][int]$UndoCount = 20
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runtime=[IO.Path]::GetFullPath($RuntimeRoot)
if(-not (Test-Path -LiteralPath (Join-Path $runtime 'cards.cdb') -PathType Leaf)){throw "Missing runtime database: $runtime/cards.cdb"}
$caseNumbers=@($Cases.Split(',') | ForEach-Object {if($_ -notmatch '^[1-9][0-9]{0,4}$'){throw 'Cases must be distinct positive response counts'}; $n=[int]$_;if($n -gt 10000){throw 'Maximum case size is 10000'};$n})
if(@($caseNumbers | Select-Object -Unique).Count -ne $caseNumbers.Count){throw 'Duplicate case size'}
$output=[IO.Path]::GetFullPath((Join-Path $repoRoot $OutFile))
$outRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'out'))+[IO.Path]::DirectorySeparatorChar
if(-not $output.StartsWith($outRoot,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($output) -ne '.json'){throw 'Output must be a .json file beneath this checkout out directory'}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output)) | Out-Null
$profile=Get-Content -LiteralPath (Join-Path $repoRoot 'build-profile.json') -Raw | ConvertFrom-Json
$cmake=Join-Path $repoRoot $profile.dependencies.cmake
$compiler=Join-Path $repoRoot $profile.compiler.path
$make=Join-Path $repoRoot $profile.dependencies.make
$build=Join-Path $repoRoot 'out/tests/Performance'
function SourceSnapshot {
    $names=@('client/gframe/undo/core_driver.cpp','client/gframe/undo/core_driver.h','client/gframe/undo/rebuilder.cpp','client/gframe/undo/rebuilder.h','client/gframe/undo/resource_view.cpp','client/gframe/undo/resource_view.h','client/gframe/undo/duel_history.h','client/gframe/data_manager.cpp','client/gframe/data_manager.h','client/gframe/file_system.cpp','tests/undo_measure.cpp','tests/core_test_globals.cpp','sources.lock.json','build-profile.json')
    foreach($dir in @('client/ocgcore','client/lua/src')) {
        $names+=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot $dir) -File | Where-Object {$_.Extension -in @('.cpp','.c','.h')} | ForEach-Object {$_.FullName.Substring($repoRoot.Length+1).Replace('\','/')})
    }
    $records=@($names | Sort-Object -Unique | ForEach-Object {$_+' '+(Get-FileHash -LiteralPath (Join-Path $repoRoot $_) -Algorithm SHA256).Hash})
    $sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($records -join "\n"))))).Replace('-','').ToLowerInvariant()} finally {$sha.Dispose()}
}
function RunNative([string]$Program,[string[]]$Arguments,[string]$Log) {
    $savedPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $Program @Arguments > $Log 2>&1
        $nativeExit=$LASTEXITCODE
    } finally {$ErrorActionPreference=$savedPreference}
    if($nativeExit -ne 0){throw "Measurement command failed ($nativeExit); see $Log"}
}
Push-Location -LiteralPath $repoRoot
try {
    $commit=(& git rev-parse HEAD).Trim()
    if($LASTEXITCODE -ne 0){throw 'Cannot resolve source commit'}
    $sourceDigest=SourceSnapshot
    RunNative $cmake @('-S',$repoRoot,'-B',$build,'-G','MinGW Makefiles','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_CXX_COMPILER=$compiler","-DCMAKE_MAKE_PROGRAM=$make") ($output+'.configure.log')
    RunNative $cmake @('--build',$build,'--target','undo_measure','-j','2') ($output+'.build.log')
    if((SourceSnapshot) -ne $sourceDigest){throw 'Measured sources changed during compilation; rerun after edits stabilize'}
    $binary=Join-Path $build 'undo_measure.exe'
    $binaryDigest=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
    $raw=$output+'.samples.jsonl'
    RunNative $binary @($runtime,($caseNumbers -join ','),$raw,[string]$UndoCount) ($output+'.run.log')
    $rows=@(Get-Content -LiteralPath $raw | ForEach-Object {$_ | ConvertFrom-Json})
    if($rows.Count -ne (1+$caseNumbers.Count*(1+2*$UndoCount))){throw 'Incomplete measurement output'}
    $samples=@($rows | Where-Object {$_.kind -in @('success','failure')})
    foreach($n in $caseNumbers) {
        foreach($kind in @('success','failure')) {
            $selected=@($samples | Where-Object {$_.responses -eq $n -and $_.kind -eq $kind})
            if($selected.Count -ne $UndoCount -or @($selected.undoCount | Select-Object -Unique).Count -ne $UndoCount){throw 'Missing or repeated sample'}
        }
    }
    $dirty=[bool](& git status --porcelain)
    $result=[ordered]@{
        schemaVersion=1; scope='core-only; legal zero-draw practice history; no UI/network/AI participant'
        sourceCommit=$commit; sourceDirty=$dirty; sourceSnapshotSha256=$sourceDigest; binarySha256=$binaryDigest
        measuredAtUtc=[DateTime]::UtcNow.ToString('o'); runtimeRoot=$runtime
        compiler=$profile.compiler; resourceDigest=$rows[0].resourceDigest; fixture=$rows[0]; samples=@($rows | Select-Object -Skip 1)
    }
    [IO.File]::WriteAllText($output,($result | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    Write-Host "Measured $($caseNumbers -join ',') actual accepted responses, $UndoCount successes and failures per case. $output"
    Write-Host 'This measures the core only; integrated AI/network/UI performance remains pending.'
} finally {Pop-Location}
