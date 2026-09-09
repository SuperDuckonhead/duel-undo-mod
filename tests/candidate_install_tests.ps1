param(
 [Parameter(Mandatory)][string]$Zip,
 [Parameter(Mandatory)][string]$Manifest,
 [Parameter(Mandatory)][string]$RuntimeRoot,
 [Parameter(Mandatory)][string]$OutputName,
 [ValidateSet('Install','Uninstall')][string]$Mode='Install',
 [string]$ReceiptSha256=''
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repo 'tools/PackageValidation.ps1')
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
if($OutputName -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$'){throw 'OutputName must be a fresh simple receipt-directory name'}
$runtime=Get-PackageFullPath $RuntimeRoot $repo
Assert-PackagePath $runtime
if(-not (Test-Path -LiteralPath $runtime -PathType Container) -or $runtime.TrimEnd('\','/') -eq [IO.Path]::GetPathRoot($runtime).TrimEnd('\','/')){throw 'RuntimeRoot must be an existing non-volume-root directory'}
$output=Join-Path $repo ('out/candidate-install-tests/'+$OutputName)
$receiptPath=Join-Path $output 'receipt.json'
Assert-PackagePath $output (Join-Path $repo 'out')
if($Mode -eq 'Install' -and (Test-Path -LiteralPath $output)){throw 'Receipt output already exists; use a new OutputName for reinstall'}
if($Mode -eq 'Uninstall' -and $ReceiptSha256 -cnotmatch '^[a-fA-F0-9]{64}$'){throw 'Uninstall requires the independently retained ReceiptSha256'}
$zipPath=Get-PackageFullPath $Zip $repo;$manifestPath=Get-PackageFullPath $Manifest $repo
$leases=[Collections.Generic.List[IDisposable]]::new()
function Lease([string]$Path){Assert-PackagePath $Path;$f=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read);$leases.Add($f);return $f}
function HashStream($Stream){$sha=[Security.Cryptography.SHA256]::Create();try{$Stream.Position=0;$h=([BitConverter]::ToString($sha.ComputeHash($Stream))).Replace('-','').ToLowerInvariant();$Stream.Position=0;return $h}finally{$sha.Dispose()}}
function CreateFile([string]$Path,[byte[]]$Bytes){Assert-PackagePath $Path;$f=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);try{$f.Write($Bytes,0,$Bytes.Length);$f.Flush()}finally{$f.Dispose()}}
function OriginalSnapshot {
 @('ygopro.exe','Bot.exe','system.conf','load-once.conf','cards.cdb','strings.conf','lflist.conf','bot.conf','WindBot/WindBot.exe','WindBot/WindBot.exe.config','WindBot/x86/sqlite3.dll','WindBot/x64/sqlite3.dll','WindBot/bots.json') | ForEach-Object {
  $path=Join-Path $runtime $_;Assert-PackagePath $path $runtime
  $exists=Test-Path -LiteralPath $path
  if($exists -and -not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Original snapshot is not a regular file: $path"}
  [PSCustomObject]@{path=$_;exists=[bool]$exists;sha256=if($exists){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}else{''}}
 }
}
function CheckOriginals($Expected){
 $now=@(OriginalSnapshot)
 if(($now|ConvertTo-Json -Depth 4 -Compress) -cne ($Expected|ConvertTo-Json -Depth 4 -Compress)){throw 'Original program/config/resource snapshot changed'}
}
try {
 $zipLease=Lease $zipPath;$manifestLease=Lease $manifestPath
 $zipHash=HashStream $zipLease;$manifestHash=HashStream $manifestLease
 $data=Read-PackageJson $manifestPath
 Assert-PackageProperties $data @('schemaVersion','files')
 if(($data.schemaVersion -isnot [int] -and $data.schemaVersion -isnot [long]) -or $data.schemaVersion -ne 1 -or $data.files -isnot [array] -or $data.files.Count -lt 1 -or $data.files.Count -gt 256){throw 'Invalid external release manifest'}
 $archive=[IO.Compression.ZipArchive]::new($zipLease,[IO.Compression.ZipArchiveMode]::Read,$true);$leases.Add($archive)
 $destinations=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);$sources=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 $total=0L
 $files=@(foreach($entry in $data.files){
  Assert-PackageProperties $entry @('source','destination','sha256','license')
  foreach($field in @('source','destination','sha256','license')){if($entry.$field -isnot [string]){throw 'Manifest fields must be strings'}}
  Assert-PackageDestination $entry.destination;Assert-PackageRelative $entry.source
  if(-not $destinations.Add($entry.destination) -or -not $sources.Add($entry.source)){throw 'Duplicate manifest source/destination'}
  if($entry.source -cne $entry.destination -and -not $entry.source.EndsWith('/'+$entry.destination,[StringComparison]::Ordinal)){throw 'Invalid package source suffix'}
  if($entry.sha256 -cnotmatch '^[a-fA-F0-9]{64}$' -or [string]::IsNullOrWhiteSpace($entry.license) -or $entry.license.Length -gt 512 -or $entry.license -match '[\x00-\x1f]'){throw 'Invalid manifest hash/license'}
  $members=@($archive.Entries|Where-Object {$_.FullName -ceq $entry.destination})
  if($members.Count -ne 1 -or $members[0].Length -gt 128MB){throw 'Missing/duplicate/oversize ZIP entry'}
  $total+=$members[0].Length;if($total -gt 256MB){throw 'ZIP payload exceeds 256 MiB'}
  [PSCustomObject]@{destination=$entry.destination;sha256=$entry.sha256.ToLowerInvariant();Length=$members[0].Length}
 })
 # Shared validator checks every member, exact count/name/size and content hash.
 Assert-PackageArchive $zipPath $files
 $originals=@(OriginalSnapshot)
 $owned=@($files|ForEach-Object {[PSCustomObject]@{destination=$_.destination;sha256=$_.sha256}})
 if($Mode -eq 'Uninstall'){
  $receiptLease=Lease $receiptPath
  $receipt=Read-PackageJson $receiptPath $ReceiptSha256
  Assert-PackageProperties $receipt @('schemaVersion','purpose','runtimeRoot','zipSha256','manifestSha256','files','originals')
  if($receipt.schemaVersion -ne 1 -or $receipt.purpose -cne 'candidate-install-test' -or $receipt.runtimeRoot -cne $runtime -or $receipt.zipSha256 -cne $zipHash -or $receipt.manifestSha256 -cne $manifestHash){throw 'Receipt does not bind this ZIP/manifest/runtime'}
  if(($receipt.files|ConvertTo-Json -Depth 4 -Compress) -cne ($owned|ConvertTo-Json -Depth 4 -Compress)){throw 'Receipt file list differs from verified ZIP'}
  CheckOriginals $receipt.originals
  foreach($file in $files){$path=Join-Path $runtime $file.destination;Assert-PackagePath $path $runtime;Assert-PackageHash $path $file.sha256}
  Assert-PackagePath (Join-Path $output 'uninstalled.json') $output
  if(Test-Path -LiteralPath (Join-Path $output 'uninstalled.json')){throw 'This receipt was already uninstalled'}
  # Every input has passed before the first delete. Delete only exact listed
  # files; retain all directories, generated configs, decks and other data.
  foreach($file in $files){$path=Join-Path $runtime $file.destination;Assert-PackagePath $path $runtime;Assert-PackageHash $path $file.sha256;[IO.File]::Delete($path)}
  CheckOriginals $receipt.originals
  CreateFile (Join-Path $output 'uninstalled.json') ([Text.Encoding]::UTF8.GetBytes('{"receiptSha256":"'+$ReceiptSha256.ToLowerInvariant()+'","originalSnapshotUnchanged":true}'))
  [PSCustomObject]@{Mode=$Mode;RuntimeRoot=$runtime;Receipt=$receiptPath;ReceiptSha256=$ReceiptSha256;Files=$files.Count;OriginalSnapshotUnchanged=$true}
 }else{
  # Preflight all destinations and capture all payload bytes before any write.
  $payloads=@{}
  foreach($file in $files){
   $path=Join-Path $runtime $file.destination;Assert-PackagePath $path $runtime
   if(Test-Path -LiteralPath $path){throw "Existing destination is never overwritten: $path"}
   $stream=$archive.GetEntry($file.destination).Open();$memory=[IO.MemoryStream]::new()
   try{$stream.CopyTo($memory);$payloads[$file.destination]=$memory.ToArray()}finally{$stream.Dispose();$memory.Dispose()}
  }
  Assert-PackagePath $output (Join-Path $repo 'out');[IO.Directory]::CreateDirectory($output)|Out-Null
  foreach($file in $files){
   $path=Join-Path $runtime $file.destination;Assert-PackagePath $path $runtime
   [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null
   CreateFile $path $payloads[$file.destination]
  }
  CheckOriginals $originals
  $receipt=[ordered]@{schemaVersion=1;purpose='candidate-install-test';runtimeRoot=$runtime;zipSha256=$zipHash;manifestSha256=$manifestHash;files=$owned;originals=$originals}
  CreateFile $receiptPath ([Text.Encoding]::UTF8.GetBytes(($receipt|ConvertTo-Json -Depth 6)))
  [PSCustomObject]@{Mode=$Mode;RuntimeRoot=$runtime;Receipt=$receiptPath;ReceiptSha256=(Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant();Files=$files.Count;OriginalSnapshotUnchanged=$true}
 }
}finally{for($i=$leases.Count-1;$i -ge 0;$i--){$leases[$i].Dispose()}}