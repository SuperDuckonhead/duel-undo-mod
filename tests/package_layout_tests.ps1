$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repo 'tools/PackageValidation.ps1')
$id='r3-'+[guid]::NewGuid().ToString('N')
$fixture=Join-Path $repo ('out/tests/'+$id)
$source=Join-Path $repo ('out/release/'+$id)
$runtime=Join-Path $fixture 'runtime'
foreach($directory in @($fixture,$source,$runtime)){Assert-PackagePath $directory (Join-Path $repo 'out');[IO.Directory]::CreateDirectory($directory) | Out-Null}
$script:checks=0
function Check([bool]$Value,[string]$Name){if(-not $Value){throw $Name};++$script:checks}
function Rejects([scriptblock]$Action,[string]$Pattern='*'){try{& $Action | Out-Null;return $false}catch{return $_.Exception.Message -like $Pattern}}
function SaveJson($Value,[string]$Name='manifest.json'){$path=Join-Path $fixture $Name;[IO.File]::WriteAllText($path,($Value | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false));return $path}
$destinations=@('ygopro-undo.exe','WindBot/WindBot-undo.exe','WindBot/WindBot-undo.exe.config','WindBot/undo-deps/x86/sqlite3.dll','WindBot/undo-deps/x64/sqlite3.dll','undo-mod/THIRD-PARTY.md','undo-mod/licenses/SQLite.txt')
$entries=@(foreach($destination in $destinations){$path=Join-Path $source $destination;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null;[IO.File]::WriteAllText($path,('controlled package fixture '+$destination));[ordered]@{source=$id+'/'+$destination;destination=$destination;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();license='fixture-only'}})
$manifest=SaveJson ([ordered]@{schemaVersion=1;files=$entries})
$original=@('ygopro.exe','WindBot/WindBot.exe','system.conf','cards.cdb','pics/sentinel.jpg','deck/sentinel.ydk')
foreach($name in $original){$path=Join-Path $runtime $name;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null;[IO.File]::WriteAllText($path,('original '+$name))}
$before=@($original | ForEach-Object {(Get-FileHash -LiteralPath (Join-Path $runtime $_) -Algorithm SHA256).Hash})
$validator=Join-Path $repo 'tools/Test-InstallLayout.ps1'
$packager=Join-Path $repo 'tools/Package.ps1'
$result=& $validator -Manifest $manifest -RuntimeRoot $runtime
Check ($result.Files.Count -eq $entries.Count) 'actual validator accepts clean incremental layout'
# Real layout preflight must reject regular files at any proper ancestor.
$blockedAncestors=@()
foreach($blocked in @('WindBot','WindBot/undo-deps','undo-mod/licenses')){
 $blockedRuntime=Join-Path $fixture ('blocked-'+$blocked.Replace('/','-'))
 $sentinel=Join-Path $blockedRuntime $blocked
 [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($sentinel)) | Out-Null
 [IO.File]::WriteAllText($sentinel,('unmanaged ancestor '+$blocked))
 $sentinelHash=(Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
 $refused=Rejects {& $validator -Manifest $manifest -RuntimeRoot $blockedRuntime} '*ancestor*not a directory*'
 Write-Host "Regular-file ancestor '$blocked' refused: $refused"
 $blockedAncestors+=@{Path=$blocked;Refused=$refused}
 Check ((Get-Item -LiteralPath $sentinel) -is [IO.FileInfo] -and (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -eq $sentinelHash) ('blocked ancestor preserved '+$blocked)
}
foreach($blocked in $blockedAncestors){Check $blocked.Refused ('actual validator rejects regular-file ancestor '+$blocked.Path)}
foreach($name in @('ygopro.exe','Bot.exe','WindBot/WindBot.exe','system.conf','system-undo.conf','cards.cdb','pics/card.jpg','deck/private.ydk','script/c1.lua','expansions/private.cdb','undo-mod/secret.txt','undo-mod/licenses/nested/MIT.txt','undo-mod/licenses/.hidden.txt','undo-mod/licenses/CON.txt','../ygopro-undo.exe','/ygopro-undo.exe','C:/ygopro-undo.exe','ygopro-undo.exe:stream','YGOPRO-UNDO.EXE','WindBot\\WindBot-undo.exe')){
 $bad=@{}+$entries[0];$bad.destination=$name;$path=SaveJson ([ordered]@{schemaVersion=1;files=@($bad)}) 'bad.json'
 Check (Rejects {& $validator -Manifest $path -RuntimeRoot $runtime}) ('actual validator rejects '+$name)
}
foreach($value in @('../ygopro-undo.exe','C:/ygopro-undo.exe','ygopro-undo.exe:stream',$id+'/system.conf')){
 $bad=@{}+$entries[0];$bad.source=$value;$path=SaveJson ([ordered]@{schemaVersion=1;files=@($bad)}) 'bad.json'
 Check (Rejects {& $validator -Manifest $path -RuntimeRoot $runtime}) ('reject source '+$value)
}
$bad=@{}+$entries[0];$bad.sha256='0'*64;$path=SaveJson ([ordered]@{schemaVersion=1;files=@($bad)}) 'bad.json'
Check (Rejects {& $validator -Manifest $path -RuntimeRoot $runtime} '*hash*') 'reject actual source hash mismatch'
$bad=@{}+$entries[0];$bad.Remove('license');$path=SaveJson ([ordered]@{schemaVersion=1;files=@($bad)}) 'bad.json'
Check (Rejects {& $validator -Manifest $path -RuntimeRoot $runtime}) 'require license field'
$path=SaveJson ([ordered]@{schemaVersion=1;files=@('ygopro-undo.exe')}) 'bad.json'
Check (Rejects {& $validator -Manifest $path -RuntimeRoot $runtime}) 'reject old string-only allowlist as final manifest'
$duplicate=@{}+$entries[0];$duplicate.destination='YGOPRO-UNDO.EXE';$path=SaveJson ([ordered]@{schemaVersion=1;files=@($entries[0],$duplicate)}) 'bad.json'
Check (Rejects {& $validator -Manifest $path -RuntimeRoot $runtime}) 'reject case-colliding destination'
$path=SaveJson ([ordered]@{schemaVersion=1;files=@($entries[0],$entries[0])}) 'bad.json'
Check (Rejects {& $validator -Manifest $path -RuntimeRoot $runtime}) 'reject exact duplicate destination'
$path=Join-Path $fixture 'duplicate-key.json';[IO.File]::WriteAllText($path,'{"schemaVersion":1,"schemaVersion":1,"files":[]}')
Check (Rejects {& $validator -Manifest $path -RuntimeRoot $runtime} '*Duplicate JSON*') 'reject duplicate JSON key'
$installed=Join-Path $runtime 'ygopro-undo.exe';Copy-Item -LiteralPath (Join-Path $source 'ygopro-undo.exe') -Destination $installed
Check (Rejects {& $validator -Manifest $manifest -RuntimeRoot $runtime} '*ownership*') 'identical existing file still requires trusted ownership'
$receipt=SaveJson ([ordered]@{schemaVersion=1;modId='ygopro-undo';files=@([ordered]@{destination='ygopro-undo.exe';sha256=$entries[0].sha256})}) 'receipt.json'
$receiptHash=(Get-FileHash -LiteralPath $receipt -Algorithm SHA256).Hash
$result=& $validator -Manifest $manifest -RuntimeRoot $runtime -TrustedReceipt $receipt -TrustedReceiptSha256 $receiptHash
Check ($result.Files[0].Disposition -eq 'managed-identical') 'accept verified managed identical file'
Check (Rejects {& $validator -Manifest $manifest -RuntimeRoot $runtime -TrustedReceipt $receipt -TrustedReceiptSha256 ('0'*64)} '*receipt*hash*') 'reject untrusted receipt digest'
[IO.File]::WriteAllText($installed,'unmanaged collision')
Check (Rejects {& $validator -Manifest $manifest -RuntimeRoot $runtime -TrustedReceipt $receipt -TrustedReceiptSha256 $receiptHash} '*ownership*') 'reject file hash differing from trusted receipt'
$oldHash=(Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash
$receipt=SaveJson ([ordered]@{schemaVersion=1;modId='ygopro-undo';files=@([ordered]@{destination='ygopro-undo.exe';sha256=$oldHash})}) 'verified-old-receipt.json'
$result=& $validator -Manifest $manifest -RuntimeRoot $runtime -TrustedReceipt $receipt -TrustedReceiptSha256 (Get-FileHash -LiteralPath $receipt -Algorithm SHA256).Hash
Check ($result.Files[0].Disposition -eq 'managed-update') 'verified old managed hash permits read-only update plan'
Check ((Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -eq $oldHash) 'validator never installs update'
$target=Join-Path $fixture 'controlled-target';[IO.Directory]::CreateDirectory($target) | Out-Null;[IO.File]::WriteAllText((Join-Path $target 'keep.txt'),'unchanged')
$linkedRuntime=Join-Path $fixture 'linked-runtime';[IO.Directory]::CreateDirectory($linkedRuntime) | Out-Null
New-Item -ItemType Junction -Path (Join-Path $linkedRuntime 'WindBot') -Target $target | Out-Null
Check (Rejects {& $validator -Manifest $manifest -RuntimeRoot $linkedRuntime} '*reparse*') 'reject runtime destination ancestor junction'
$sourceLink=Join-Path $source 'linked';New-Item -ItemType Junction -Path $sourceLink -Target $target | Out-Null
$bad=@{}+$entries[0];$bad.source=$id+'/linked/ygopro-undo.exe';$path=SaveJson ([ordered]@{schemaVersion=1;files=@($bad)}) 'bad.json'
Check (Rejects {& $validator -Manifest $path -RuntimeRoot $runtime} '*reparse*') 'reject source ancestor junction'
$zip=Join-Path $repo ('out/packages/'+$id+'/candidate.zip')
$result=& $packager -Manifest $manifest -OutFile $zip
Check (Test-Path -LiteralPath $zip -PathType Leaf) 'actual ZIP created'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try{
 Check ($archive.Entries.Count -eq $entries.Count) 'ZIP has exactly manifest entry count'
 foreach($entry in $entries){$member=@($archive.Entries | Where-Object {$_.FullName -ceq $entry.destination});Check ($member.Count -eq 1) ('ZIP exact name '+$entry.destination);$stream=$member[0].Open();$sha=[Security.Cryptography.SHA256]::Create();try{$digest=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose();$stream.Dispose()};Check ($digest -eq $entry.sha256) ('ZIP hash '+$entry.destination)}
}finally{$archive.Dispose()}
$loaded=Read-PackageManifest $manifest $repo
$extraZip=Join-Path $fixture 'extra-entry.zip';[IO.File]::Copy($zip,$extraZip,$false)
$archive=[IO.Compression.ZipFile]::Open($extraZip,[IO.Compression.ZipArchiveMode]::Update)
try{[void]$archive.CreateEntry('deck/private.ydk')}finally{$archive.Dispose()}
Check (Rejects {Assert-PackageArchive $extraZip $loaded.Files} '*count*') 'archive verification rejects an unexpected entry'
$changedZip=Join-Path $fixture 'changed-entry.zip';[IO.File]::Copy($zip,$changedZip,$false)
$archive=[IO.Compression.ZipFile]::Open($changedZip,[IO.Compression.ZipArchiveMode]::Update)
try{$member=$archive.GetEntry('ygopro-undo.exe');$length=$member.Length;$member.Delete();$replacement=$archive.CreateEntry('ygopro-undo.exe');$stream=$replacement.Open();try{$bytes=[byte[]]::new($length);$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}}finally{$archive.Dispose()}
Check (Rejects {Assert-PackageArchive $changedZip $loaded.Files} '*hash*') 'archive verification rejects changed content with unchanged length'
$badOutput=Join-Path ([IO.Path]::GetDirectoryName($zip)) 'stream.zip:ads.zip'
Check (Rejects {& $packager -Manifest $manifest -OutFile $badOutput} '*Alternate data*') 'reject output ADS before staging'
$badManifest=SaveJson ([ordered]@{schemaVersion=1;files=@('ygopro-undo.exe')}) 'bad.json'
$unpublished=Join-Path ([IO.Path]::GetDirectoryName($zip)) 'invalid.zip'
Check (Rejects {& $packager -Manifest $badManifest -OutFile $unpublished}) 'actual packager rejects malformed manifest'
Check (-not (Test-Path -LiteralPath $unpublished)) 'malformed manifest publishes no ZIP'
$zipHash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
Check (Rejects {& $packager -Manifest $manifest -OutFile $zip} '*exists*') 'no force overwrite existing ZIP'
Check ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -eq $zipHash) 'existing ZIP preserved'
Check (Rejects {& $packager -Manifest $manifest -OutFile (Join-Path $fixture 'escape.zip')} '*escapes*') 'reject output outside fixed packages root'
$outputLink=Join-Path ([IO.Path]::GetDirectoryName($zip)) 'linked';New-Item -ItemType Junction -Path $outputLink -Target $target | Out-Null
Check (Rejects {& $packager -Manifest $manifest -OutFile (Join-Path $outputLink 'escape.zip')} '*reparse*') 'reject ZIP output ancestor junction'
$leafLink=Join-Path ([IO.Path]::GetDirectoryName($zip)) 'leaf.zip';New-Item -ItemType Junction -Path $leafLink -Target $target | Out-Null
Check (Rejects {& $packager -Manifest $manifest -OutFile $leafLink} '*reparse*') 'reject ZIP output leaf junction'
Check (@(Get-ChildItem -LiteralPath $target).Count -eq 1 -and [IO.File]::ReadAllText((Join-Path $target 'keep.txt')) -eq 'unchanged') 'controlled junction target untouched'
$after=@($original | ForEach-Object {(Get-FileHash -LiteralPath (Join-Path $runtime $_) -Algorithm SHA256).Hash})
Check (($before -join ',') -eq ($after -join ',')) 'all original fixture executables/config/resources unchanged'
Write-Host "Package tools: $script:checks checks passed. Controlled fixtures remain at $fixture; source artifacts at $source; ZIP $zip. No recursive link cleanup."
