param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$RuntimeRoot='F:/MyCardLibrary/ygopro',
 [ValidateRange(1,256)][int]$CaseId=47,
 [switch]$All,
 [switch]$UseExistingBuild,
 [ValidateRange(1,2)][int]$Workers=2,
 [ValidateRange(30,900)][int]$CaseTimeoutSeconds=300
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'PackageValidation.ps1')
$root=Join-Path $repo 'out/tests/W3Host'
$results=Join-Path $root ('runs/'+[guid]::NewGuid().ToString('N'))
Assert-PackagePath $results $root
[IO.Directory]::CreateDirectory($results)|Out-Null
$runtime=[IO.Path]::GetFullPath($RuntimeRoot)
$listPath=Join-Path $runtime 'WindBot/bots.json'
$listBytes=[IO.File]::ReadAllBytes($listPath)
if($listBytes.Length -gt 1MB){throw 'Bot list exceeds bound'}
$list=[Text.Encoding]::UTF8.GetString($listBytes)|ConvertFrom-Json
$rows=@($list.windbots)
if($rows.Count -lt 1 -or $rows.Count -gt 256){throw 'Invalid actual list count'}
foreach($row in $rows){
 foreach($field in @('name','deck','dialog')){if($row.$field -isnot [string] -or [string]::IsNullOrWhiteSpace($row.$field) -or $row.$field.Length -gt 1024 -or $row.$field -match '[\x00-\x1f]'){throw "Invalid actual list $field"}}
 foreach($field in $row.PSObject.Properties.Name){if($field -cnotin @('name','deck','dialog','hidden')){throw "Uncovered list field: $field"}}
 if($null -ne $row.hidden -and $row.hidden -isnot [bool]){throw 'Invalid hidden field'}
}
if(-not $All -and $CaseId -gt $rows.Count){throw 'CaseId outside actual list'}
$sha=[Security.Cryptography.SHA256]::Create()
try{$hashBytes=$sha.ComputeHash($listBytes)}finally{$sha.Dispose()}
$listHash=([BitConverter]::ToString($hashBytes)).Replace('-','').ToLowerInvariant()
$manifest=Join-Path $results 'cases.bin'
$stream=[IO.File]::Open($manifest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
$writer=[IO.BinaryWriter]::new($stream,[Text.Encoding]::UTF8)
try{
 $writer.Write([uint32]0x314c3357);$writer.Write([byte[]]$hashBytes);$writer.Write([uint32]$rows.Count)
 foreach($row in $rows){foreach($value in @($row.name,$row.deck,$row.dialog)){$bytes=[Text.Encoding]::UTF8.GetBytes($value);$writer.Write([uint32]$bytes.Length);$writer.Write([byte[]]$bytes)};$writer.Write([byte][bool]$row.hidden)}
}finally{$writer.Dispose();$stream.Dispose()}
$portable=$repo.Replace('\','/')
$cmake=Join-Path $repo '.cache/tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe'
$toolchain="$portable/.cache/tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$build=Join-Path $root $Configuration
Assert-PackagePath $build $root
$bot=Join-Path $repo "out/bot/$Configuration/WindBot-undo.exe"
$botHash=(Get-FileHash -LiteralPath $bot -Algorithm SHA256).Hash
if(-not $UseExistingBuild){
$ErrorActionPreference='Continue'
& $cmake -S $repo -B $build -G 'MinGW Makefiles' "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_CXX_COMPILER=$toolchain/clang++.exe" "-DCMAKE_MAKE_PROGRAM=$toolchain/mingw32-make.exe" *> (Join-Path $results 'configure.log')
$configExit=$LASTEXITCODE
$ErrorActionPreference='Stop'
if($configExit){throw "Configure failed: $results/configure.log"}
$ErrorActionPreference='Continue'
& $cmake --build $build --target ai_host_history_tests -j2 *> (Join-Path $results 'build.log')
$buildExit=$LASTEXITCODE
$ErrorActionPreference='Stop'
if($buildExit){throw "Build failed: $results/build.log"}
}
$binary=Join-Path $build 'ai_host_history_tests.exe'
$binaryHash=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
$provenance=@("listSha256=$listHash","rows=$($rows.Count)","distinctExecutors=$(@($rows.deck|Select-Object -Unique).Count)","botExecutableSha256=$botHash","testExecutableSha256=$binaryHash",'scope=actual host/core callbacks; legal surrender; no GUI or natural-win claim')
[IO.File]::WriteAllLines((Join-Path $results 'provenance.txt'),$provenance,[Text.UTF8Encoding]::new($false))
$ids=if($All){@(1..$rows.Count)}else{@($CaseId)}
$pending=[Collections.Generic.Queue[int]]::new();foreach($id in $ids){$pending.Enqueue($id)}
$running=[Collections.Generic.List[object]]::new()
$completed=[Collections.Generic.List[object]]::new()
Write-Host "Actual host history evidence: $results"
try {
while($pending.Count -or $running.Count){
 while($pending.Count -and $running.Count -lt $Workers){
  $id=$pending.Dequeue();$caseRoot=Join-Path $results ('case-'+$id)
  [IO.Directory]::CreateDirectory($caseRoot)|Out-Null
  $proof=Join-Path $caseRoot 'proof.txt'
  $arguments=@($bot,$runtime,$manifest,[string]$id,$proof)|ForEach-Object{if($_.Contains('"')){throw 'Unexpected quote in argument'};'"'+$_+'"'}
  $process=Start-Process -FilePath $binary -ArgumentList $arguments -WorkingDirectory $repo -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $caseRoot 'stdout.log') -RedirectStandardError (Join-Path $caseRoot 'stderr.log')
  $running.Add([PSCustomObject]@{id=$id;process=$process;began=[DateTime]::UtcNow;proof=$proof;path=$caseRoot})
 }
 foreach($job in @($running.ToArray())){
  $seconds=([DateTime]::UtcNow-$job.began).TotalSeconds;$timeout=$seconds -gt $CaseTimeoutSeconds
  if(-not $job.process.HasExited -and -not $timeout){continue}
  if($timeout -and -not $job.process.HasExited){$job.process.Kill();$job.process.WaitForExit()}
  $job.process.WaitForExit();$code=$job.process.ExitCode;$fields=@{}
  if(Test-Path -LiteralPath $job.proof){foreach($line in [IO.File]::ReadAllLines($job.proof)){if($line.Contains('=')){$pair=$line.Split(@('='),2);$fields[$pair[0]]=$pair[1]}}}
  $row=$rows[$job.id-1]
  $ok= -not $timeout -and $code -eq 0 -and $fields.history -ceq 'pass' -and $fields.executor -ceq $row.deck -and $fields.dialog -ceq $row.dialog
  $detail=if($ok){'two undo; normal summon B; real bot continuation; human surrender'}elseif($timeout){'case process timed out; child bot jobs close with parent'}else{[IO.File]::ReadAllText((Join-Path $job.path 'stderr.log')).Trim()}
  if(-not $ok -and -not $detail){$detail="Exit $code or incomplete case proof"}
  $completed.Add([PSCustomObject]@{botId=$job.id;name=$row.name;executor=$row.deck;dialog=$row.dialog;hidden=[int][bool]$row.hidden;deckRelativePath=$fields.deckRelativePath;history=if($ok){'pass'}else{'fail'};visual='pending';termination=$fields.termination;seconds=[Math]::Round($seconds,3);detail=$detail})
  $completed|Sort-Object botId|Export-Csv -LiteralPath (Join-Path $results 'results.csv') -NoTypeInformation -Encoding UTF8
  Write-Host "$(if($ok){'PASS'}else{'FAIL'}) $($job.id)/$($rows.Count) $($row.deck): $detail"
  $job.process.Dispose();[void]$running.Remove($job)
 }
 if($running.Count){Start-Sleep -Milliseconds 100}
}
} finally {
 foreach($job in @($running.ToArray())){if(-not $job.process.HasExited){$job.process.Kill();$job.process.WaitForExit()};$job.process.Dispose()}
}
if((Get-FileHash -LiteralPath $bot -Algorithm SHA256).Hash -ne $botHash -or (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ne $binaryHash){throw 'Executable changed during history run'}
if((Get-FileHash -LiteralPath $listPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $listHash){throw 'List changed during history run'}
if($completed.Count -ne $ids.Count -or @($completed|Where-Object history -ne 'pass').Count){throw "Actual history failures retained: $results/results.csv"}
Write-Host "Actual history PASS $($completed.Count)/$($ids.Count); visual and natural-win acceptance are separate. Evidence: $results"