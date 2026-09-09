param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$RuntimeRoot='F:/MyCardLibrary/ygopro/WindBot',
 [string]$ResultsDirectory='',
 [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'PackageValidation.ps1') # plain ancestor/leaf validation only
$root=Join-Path $repo 'out/tests/W3List'
if(-not $ResultsDirectory){$ResultsDirectory=Join-Path $root ('runs/'+[guid]::NewGuid().ToString('N'))}
$results=Get-PackageFullPath $ResultsDirectory $repo
Assert-PackagePath $results $root
$runtime=[IO.Path]::GetFullPath($RuntimeRoot)
$listPath=Join-Path $runtime 'bots.json'
$listBytes=[IO.File]::ReadAllBytes($listPath)
if($listBytes.Length -gt 1MB){throw 'Actual bot list exceeds bound'}
$sha=[Security.Cryptography.SHA256]::Create()
try{$listHashBytes=$sha.ComputeHash($listBytes)}finally{$sha.Dispose()}
$listHash=([BitConverter]::ToString($listHashBytes)).Replace('-','').ToLowerInvariant()
$list=[Text.Encoding]::UTF8.GetString($listBytes) | ConvertFrom-Json
$rows=@($list.windbots)
if($rows.Count -lt 1 -or $rows.Count -gt 256){throw 'Actual bot list is empty or oversized'}
foreach($row in $rows){
 foreach($field in @('name','deck','dialog')){if($row.$field -isnot [string] -or [string]::IsNullOrWhiteSpace($row.$field) -or $row.$field.Length -gt 1024 -or $row.$field -match '[\x00-\x1f]'){throw "Invalid list field $field"}}
 foreach($field in $row.PSObject.Properties.Name){if($field -cnotin @('name','deck','dialog','hidden')){throw "Uncovered actual list field $field"}}
 if($null -ne $row.hidden -and $row.hidden -isnot [bool]){throw 'Invalid hidden flag'}
}
$csv=Join-Path $results 'results.csv'
$provenance=Join-Path $results 'provenance.txt'
function CheckCoverage {
 if(-not (Test-Path -LiteralPath $csv -PathType Leaf) -or -not (Test-Path -LiteralPath $provenance -PathType Leaf)){throw 'AI list preflight incomplete: no actual results/provenance'}
 Assert-PackagePath $csv $root;Assert-PackagePath $provenance $root
 $values=@(Import-Csv -LiteralPath $csv -Encoding UTF8)
 if($values.Count -ne $rows.Count){throw 'AI list preflight incomplete: row count differs'}
 if(-not ([IO.File]::ReadAllLines($provenance) -ccontains ('listSha256='+$listHash))){throw 'AI list preflight results belong to a different list'}
 $proof=[IO.File]::ReadAllLines($provenance)
 if(-not ($proof -ccontains 'nativeRunExit=0')){throw 'Preflight lacks completed list and representative execution'}
 if(-not ($proof -ccontains ('resultsSha256='+(Get-FileHash -LiteralPath $csv -Algorithm SHA256).Hash.ToLowerInvariant()))){throw 'Preflight result bytes changed'}
 for($i=0;$i -lt $rows.Count;++$i){
  $expected=$rows[$i];$got=$values[$i]
  if($got.botId -ne [string]($i+1) -or $got.name -cne $expected.name -or $got.executor -cne $expected.deck -or $got.dialog -cne $expected.dialog -or $got.hidden -ne [string][int][bool]$expected.hidden){throw "AI list identity mismatch at row $($i+1)"}
  if($got.preflight -ne 'pass' -or $got.fullMatch -ne 'pending'){throw "AI list preflight failed/unverified row $($i+1): $($got.detail)"}
 }
 Write-Host "Complete list preflight coverage: $($values.Count) rows, $(@($rows.deck | Select-Object -Unique).Count) distinct executors. Full matches remain pending."
}
if($CheckOnly){CheckCoverage;return}
if(Test-Path -LiteralPath $results){throw 'Use an absent results directory; prior evidence is never overwritten'}
[IO.Directory]::CreateDirectory($results) | Out-Null
$manifest=Join-Path $results 'cases.bin'
$stream=[IO.File]::Open($manifest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
$writer=[IO.BinaryWriter]::new($stream,[Text.Encoding]::UTF8)
try{
 $writer.Write([uint32]0x314c3357);$writer.Write([byte[]]$listHashBytes);$writer.Write([uint32]$rows.Count)
 foreach($row in $rows){foreach($value in @($row.name,$row.deck,$row.dialog)){$bytes=[Text.Encoding]::UTF8.GetBytes($value);$writer.Write([uint32]$bytes.Length);$writer.Write([byte[]]$bytes)};$writer.Write([byte][bool]$row.hidden)}
}finally{$writer.Dispose();$stream.Dispose()}
$portable=$repo.Replace('\','/')
$cmake=Join-Path $repo '.cache/tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe'
$toolchain="$portable/.cache/tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$build=Join-Path $root $Configuration
Assert-PackagePath $build $root
$bot=Join-Path $repo "out/bot/$Configuration/WindBot-undo.exe"
$botHash=(Get-FileHash -LiteralPath $bot -Algorithm SHA256).Hash
$ErrorActionPreference='Continue'
& $cmake -S $repo -B $build -G 'MinGW Makefiles' "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_CXX_COMPILER=$toolchain/clang++.exe" "-DCMAKE_MAKE_PROGRAM=$toolchain/mingw32-make.exe" *> (Join-Path $results 'configure.log')
$configureExit=$LASTEXITCODE
$ErrorActionPreference='Stop'
if($configureExit){throw "Preflight configure failed: $results/configure.log"}
$ErrorActionPreference='Continue'
& $cmake --build $build --target ai_list_preflight_tests -j2 *> (Join-Path $results 'build.log')
$buildExit=$LASTEXITCODE
$ErrorActionPreference='Stop'
if($buildExit){throw "Preflight build failed: $results/build.log"}
$binary=Join-Path $build 'ai_list_preflight_tests.exe'
# The actual native test writes one flushed CSV row per attempted list record.
$ErrorActionPreference='Continue'
& $binary $bot $runtime $manifest $csv $provenance 2>&1 | Tee-Object -FilePath (Join-Path $results 'run.log')
$testExit=$LASTEXITCODE
$ErrorActionPreference='Stop'
if((Get-FileHash -LiteralPath $bot -Algorithm SHA256).Hash -ne $botHash){throw 'Bot executable changed during preflight'}
if((Get-FileHash -LiteralPath $listPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $listHash){throw 'Actual list changed during preflight'}
[IO.File]::AppendAllText($provenance,('testExecutableSha256='+(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
Write-Host "Preflight evidence: $results"
if($testExit){throw 'One or more actual list/representative preflight cases failed; see retained results'}
[IO.File]::AppendAllText($provenance,('nativeRunExit=0'+[Environment]::NewLine+'resultsSha256='+(Get-FileHash -LiteralPath $csv -Algorithm SHA256).Hash.ToLowerInvariant()+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
CheckCoverage