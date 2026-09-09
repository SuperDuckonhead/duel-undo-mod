$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$run=Join-Path $repo ('out/candidate-fixture-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($run)|Out-Null
$runtime=Join-Path $run 'runtime';[IO.Directory]::CreateDirectory($runtime)|Out-Null
[IO.File]::WriteAllText((Join-Path $runtime 'ygopro.exe'),'original-program')
[IO.File]::WriteAllText((Join-Path $runtime 'system.conf'),'original-config')
$zip=Join-Path $run 'candidate.zip';$manifest=Join-Path $run 'release-files.json'
function BuildZip([string]$Path,[bool]$Bad=$false){
 $archive=[IO.Compression.ZipFile]::Open($Path,[IO.Compression.ZipArchiveMode]::Create)
 $items=@()
 try{foreach($name in @('ygopro-undo.exe','WindBot/WindBot-undo.exe')){
  $bytes=[Text.Encoding]::UTF8.GetBytes('fixture-'+$name);$sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  $member=$archive.CreateEntry($(if($Bad -and $name -eq 'ygopro-undo.exe'){'../outside.exe'}else{$name}));$stream=$member.Open();try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
  $items+=[PSCustomObject]@{source=$name;destination=$name;sha256=$hash;license='fixture'}
 }}finally{$archive.Dispose()};return ,$items
}
$items=BuildZip $zip
[IO.File]::WriteAllText($manifest,([ordered]@{schemaVersion=1;files=$items}|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
$script=Join-Path $repo 'tests/candidate_install_tests.ps1';$common=@{Zip=$zip;Manifest=$manifest;RuntimeRoot=$runtime}
$checks=0
function Reject([scriptblock]$Action,[string]$Label){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};if(-not $failed){throw "Expected rejection: $Label"};$script:checks++;Write-Output "PASS reject $Label"}
function Check([bool]$Value,[string]$Label){if(-not $Value){throw "FAIL $Label"};$script:checks++;Write-Output "PASS $Label"}
function Fresh{return 'fixture-'+[guid]::NewGuid().ToString('N')}
# Reject a later unknown target before creating the earlier absent target.
[IO.Directory]::CreateDirectory((Join-Path $runtime 'WindBot'))|Out-Null
[IO.File]::WriteAllText((Join-Path $runtime 'WindBot/WindBot-undo.exe'),'unknown')
Reject {& $script @common -OutputName (Fresh)} 'unknown preexisting target'
Check (-not (Test-Path (Join-Path $runtime 'ygopro-undo.exe'))) 'preflight prevents partial install'
[IO.File]::Delete((Join-Path $runtime 'WindBot/WindBot-undo.exe'))
$badZip=Join-Path $run 'bad.zip';$null=BuildZip $badZip $true
Reject {& $script -Zip $badZip -Manifest $manifest -RuntimeRoot $runtime -OutputName (Fresh)} 'ZIP traversal member'
$badManifest=Join-Path $run 'bad-manifest.json';$bad=[IO.File]::ReadAllText($manifest).Replace($items[0].sha256,('0'*64));[IO.File]::WriteAllText($badManifest,$bad)
Reject {& $script -Zip $zip -Manifest $badManifest -RuntimeRoot $runtime -OutputName (Fresh)} 'ZIP hash mismatch'
$name=Fresh;$installed=& $script @common -OutputName $name
Check ((Test-Path (Join-Path $runtime 'ygopro-undo.exe')) -and $installed.ReceiptSha256.Length -eq 64) 'install returns trusted external receipt digest'
[IO.File]::WriteAllText((Join-Path $runtime 'system-undo.conf'),'new-personal-config')
Reject {& $script @common -OutputName $name -Mode Uninstall -ReceiptSha256 ('0'*64)} 'wrong receipt digest'
$alternateManifest=Join-Path $run 'same-content-other-manifest.json';[IO.File]::WriteAllText($alternateManifest,([IO.File]::ReadAllText($manifest)+" "))
Reject {& $script -Zip $zip -Manifest $alternateManifest -RuntimeRoot $runtime -OutputName $name -Mode Uninstall -ReceiptSha256 $installed.ReceiptSha256} 'changed manifest identity despite same entries'
$otherRuntime=Join-Path $run 'other-runtime';[IO.Directory]::CreateDirectory($otherRuntime)|Out-Null
Reject {& $script -Zip $zip -Manifest $manifest -RuntimeRoot $otherRuntime -OutputName $name -Mode Uninstall -ReceiptSha256 $installed.ReceiptSha256} 'receipt bound to different runtime'
$path=Join-Path $runtime 'WindBot/WindBot-undo.exe';$bytes=[IO.File]::ReadAllBytes($path);[IO.File]::WriteAllText($path,'changed')
Reject {& $script @common -OutputName $name -Mode Uninstall -ReceiptSha256 $installed.ReceiptSha256} 'modified installed file'
Check (Test-Path (Join-Path $runtime 'ygopro-undo.exe')) 'uninstall validates all files before first deletion'
[IO.File]::WriteAllBytes($path,$bytes)
[IO.File]::WriteAllText((Join-Path $runtime 'system.conf'),'changed-config')
Reject {& $script @common -OutputName $name -Mode Uninstall -ReceiptSha256 $installed.ReceiptSha256} 'changed original snapshot'
[IO.File]::WriteAllText((Join-Path $runtime 'system.conf'),'original-config')
$uninstalled=& $script @common -OutputName $name -Mode Uninstall -ReceiptSha256 $installed.ReceiptSha256
Check (-not (Test-Path (Join-Path $runtime 'ygopro-undo.exe')) -and -not (Test-Path $path)) 'exact uninstall removes only managed files'
Check (([IO.File]::ReadAllText((Join-Path $runtime 'ygopro.exe'))) -eq 'original-program' -and ([IO.File]::ReadAllText((Join-Path $runtime 'system-undo.conf'))) -eq 'new-personal-config') 'original executable and generated personal config survive'
Check (Test-Path (Join-Path $runtime 'WindBot') -PathType Container) 'directories retained'
$reinstalled=& $script @common -OutputName (Fresh)
Check (Test-Path (Join-Path $runtime 'ygopro-undo.exe')) 'same ZIP reinstalls using fresh receipt'
$linkRuntime=Join-Path $run 'link-runtime';$outside=Join-Path $run 'outside';[IO.Directory]::CreateDirectory($linkRuntime)|Out-Null;[IO.Directory]::CreateDirectory($outside)|Out-Null
New-Item -ItemType Junction -Path (Join-Path $linkRuntime 'WindBot') -Target $outside|Out-Null
Reject {& $script -Zip $zip -Manifest $manifest -RuntimeRoot $linkRuntime -OutputName (Fresh)} 'destination ancestor junction'
Check (-not (Test-Path (Join-Path $outside 'WindBot-undo.exe')) -and -not (Test-Path (Join-Path $linkRuntime 'ygopro-undo.exe'))) 'junction rejection performs no payload writes'
Write-Output "PASS $checks focused candidate install/uninstall/reinstall checks. Fixture: $run"