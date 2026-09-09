param([switch]$Build)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'PackageValidation.ps1')
$release=Join-Path $repo 'out/release'
$manifestPath=Join-Path $repo 'release-files.json'
$recordPath=Join-Path $release 'undo-mod/build-manifest.json'
$utf8=[Text.UTF8Encoding]::new($false)
function Hash-Bytes([byte[]]$Bytes){
 $sha=[Security.Cryptography.SHA256]::Create()
 try{return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
}
function Read-ReleaseBytes([string]$Path){
 Assert-PackagePath $Path $repo
 $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 try{
  if($stream.Length -gt 128MB){throw "Release input exceeds 128 MiB: $Path"}
  $memory=[IO.MemoryStream]::new()
  try{$stream.CopyTo($memory);return ,$memory.ToArray()}finally{$memory.Dispose()}
 }finally{$stream.Dispose()}
}
function Source-Snapshot {
 $commit=(& git -C $repo rev-parse --verify HEAD)
 if($LASTEXITCODE -or $commit -notmatch '^[0-9a-f]{40,64}$'){throw 'Cannot resolve source commit'}
 # This generated file is excluded both from cleanliness and the source digest.
 $dirty=@(& git -C $repo status --porcelain --untracked-files=all -- . ':(exclude)release-files.json')
 if($LASTEXITCODE -or $dirty.Count){throw 'Release preparation requires clean source (except generated release-files.json)'}
 $names=@(& git -C $repo -c core.quotepath=false ls-files -- . ':(exclude)release-files.json')
 if($LASTEXITCODE -or -not $names.Count){throw 'Cannot enumerate tracked source'}
 [Array]::Sort($names,[StringComparer]::Ordinal)
 $text=[Text.StringBuilder]::new()
 foreach($name in $names){
  if($name -match '[\x00-\x1f]'){throw 'Unsupported source filename'}
  $path=Get-PackageFullPath $name $repo;Assert-PackagePath $path $repo
  if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Tracked source is not a regular file: $name"}
  [void]$text.Append($name).Append([char]0).Append((Hash-Bytes (Read-ReleaseBytes $path))).Append([char]10)
 }
 [PSCustomObject]@{commit=$commit;tree=(Hash-Bytes $utf8.GetBytes($text.ToString()));lock=(Hash-Bytes (Read-ReleaseBytes (Join-Path $repo 'sources.lock.json')));profile=(Hash-Bytes (Read-ReleaseBytes (Join-Path $repo 'build-profile.json')))}
}
function Release-Plan {
 $map=[ordered]@{
  'ygopro-undo.exe'='out/client/Release/ygopro-undo.exe'
  'WindBot/WindBot-undo.exe'='out/bot/Release/WindBot-undo.exe'
  'WindBot/WindBot-undo.exe.config'='out/bot/Release/WindBot-undo.exe.config'
  'WindBot/undo-deps/x86/sqlite3.dll'='out/bot/Release/undo-deps/x86/sqlite3.dll'
  'WindBot/undo-deps/x64/sqlite3.dll'='out/bot/Release/undo-deps/x64/sqlite3.dll'
 }
 foreach($name in @('BUILD','INSTALL','COMPATIBILITY','RELEASE-NOTES','THIRD-PARTY')){$map.Add("undo-mod/$name.md","docs/$name.md")}
 $licenses=Join-Path $repo 'docs/licenses';Assert-PackagePath $licenses $repo
 if(-not (Test-Path -LiteralPath $licenses -PathType Container)){throw 'Missing docs/licenses directory'}
 $texts=@(Get-ChildItem -LiteralPath $licenses -Force | Sort-Object Name)
 if(-not $texts.Count -or $texts.Count -gt 240){throw 'Invalid license notice count'}
 foreach($entry in $texts){
  Assert-PackagePath $entry.FullName $licenses
  if($entry.PSIsContainer){throw 'License notices must be flat .txt files'}
  $map.Add('undo-mod/licenses/'+$entry.Name,'docs/licenses/'+$entry.Name)
 }
 foreach($destination in $map.Keys){
  Assert-PackageDestination $destination
  $source=Join-Path $repo $map[$destination];Assert-PackagePath $source $repo
  $target=Join-Path $release $destination;Assert-PackagePath $target $release
  # Documentation and license files must exist even before the build starts.
  if($map[$destination].StartsWith('docs/') -and -not (Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing release documentation: $source"}
  [PSCustomObject]@{input=$map[$destination];destination=$destination;path=$source;target=$target;license='See undo-mod/THIRD-PARTY.md and undo-mod/licenses for the applicable license notices.'}
 }
}
function Assert-ReleaseTree($Allowed){
 if(-not (Test-Path -LiteralPath $release)){return}
 Assert-PackagePath $release (Join-Path $repo 'out')
 if(-not (Test-Path -LiteralPath $release -PathType Container)){throw 'Release root must be an empty directory'}
 $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($release)
 while($pending.Count){
  foreach($entry in Get-ChildItem -LiteralPath $pending.Pop() -Force){
   Assert-PackagePath $entry.FullName $release
   $relative=$entry.FullName.Substring($release.Length+1).Replace('\','/')
   if($entry.PSIsContainer){
    if(-not @($Allowed | Where-Object {$_.StartsWith($relative+'/',[StringComparison]::Ordinal)}).Count){throw "Unknown release directory: $relative"}
    $pending.Push($entry.FullName)
   }elseif($Allowed -cnotcontains $relative){throw "Unknown release file: $relative"}
  }
 }
}
function Write-NewReleaseFile([string]$Path,[byte[]]$Bytes){
 Assert-PackagePath $Path $release
 [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))|Out-Null
 Assert-PackagePath $Path $release
 $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try{$stream.Write($Bytes,0,$Bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}
Assert-PackagePath $repo
Assert-PackagePath $release (Join-Path $repo 'out')
Assert-PackagePath $manifestPath $repo
if((Test-Path -LiteralPath $manifestPath) -and -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)){throw 'Generated root manifest is not a regular file'}
$source=Source-Snapshot
$plan=@(Release-Plan)
if(-not $Build){
 if(-not (Test-Path -LiteralPath $recordPath -PathType Leaf)){throw 'No build provenance. Use -Build in a fresh checkout; old binaries cannot be attested retroactively.'}
 $package=Read-PackageManifest $manifestPath $repo
 $record=Read-PackageJson $recordPath
 Assert-PackageProperties $record @('schemaVersion','producer','sourceCommit','sourceTreeSha256','sourceDigestScope','sourceDigestExcludes','sourcesLockSha256','buildProfileSha256','build','artifacts','integrity')
 if($record.schemaVersion -ne 1 -or $record.producer -cne 'tools/Prepare-Release.ps1' -or $record.sourceCommit -cne $source.commit -or $record.sourceTreeSha256 -cne $source.tree -or $record.sourcesLockSha256 -cne $source.lock -or $record.buildProfileSha256 -cne $source.profile){throw 'Build provenance does not match current source'}
 if($record.sourceDigestScope -cne 'git-tracked-regular-files'){throw 'Invalid source digest scope'}
 if(@($record.sourceDigestExcludes).Count -ne 1 -or $record.sourceDigestExcludes[0] -cne 'release-files.json'){throw 'Invalid source digest exclusions'}
 Assert-PackageProperties $record.build @('script','target','configuration','freshProducts','startedUtc','completedUtc')
 if($record.build.script -cne 'tools/Build.ps1' -or $record.build.target -cne 'All' -or $record.build.configuration -cne 'Release' -or $record.build.freshProducts -ne $true){throw 'Invalid recorded release build'}
 if($record.artifacts -isnot [array] -or $record.artifacts.Count -ne $plan.Count -or $package.Files.Count -ne $plan.Count+1){throw 'Release provenance file count mismatch'}
 for($i=0;$i -lt $plan.Count;++$i){
  $entry=$plan[$i];$recorded=$record.artifacts[$i]
  Assert-PackageProperties $recorded @('input','destination','sha256')
  if($recorded.input -cne $entry.input -or $recorded.destination -cne $entry.destination -or $recorded.sha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'Invalid recorded release artifact'}
  Assert-PackageHash $entry.path $recorded.sha256
  $packed=@($package.Files | Where-Object {$_.source -ceq $entry.destination -and $_.destination -ceq $entry.destination -and $_.sha256 -ceq $recorded.sha256})
  if($packed.Count -ne 1){throw 'Prepared file differs from build provenance'}
 }
 $self=@($package.Files | Where-Object {$_.source -ceq 'undo-mod/build-manifest.json' -and $_.destination -ceq 'undo-mod/build-manifest.json'})
 if($self.Count -ne 1){throw 'Build manifest must be externally hash covered'}
 Assert-ReleaseTree @($package.Files.destination)
 [PSCustomObject]@{ReleaseRoot=$release;Manifest=$manifestPath;SourceCommit=$source.commit;FileCount=$package.Files.Count;ReadOnly=$true}
 return
}
# Do not clean or overwrite previous products: require a genuinely fresh build.
foreach($name in @('client/bin','client/obj','client/build','bot/bin','bot/obj','bot-tests/bin','bot-tests/obj','out/client','out/bot','out/bot-tests','out/tests')){
 $path=Join-Path $repo $name;Assert-PackagePath $path $repo
 if(Test-Path -LiteralPath $path){throw "A fresh checkout/build is required; existing build path: $name"}
}
if(Test-Path -LiteralPath $release){
 if(-not (Test-Path -LiteralPath $release -PathType Container) -or @(Get-ChildItem -LiteralPath $release -Force).Count){throw 'out/release must be absent or empty; unknown data will not be overwritten'}
}
$started=[DateTime]::UtcNow.ToString('o')
# The existing builder is the only operation allowed to write outside release/manifest.
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Build.ps1') -Target All -Configuration Release
if($LASTEXITCODE){throw "Release build failed with exit code $LASTEXITCODE"}
$completed=[DateTime]::UtcNow.ToString('o')
$after=Source-Snapshot
if($after.commit -cne $source.commit -or $after.tree -cne $source.tree -or $after.lock -cne $source.lock -or $after.profile -cne $source.profile){throw 'Source changed during release build'}
# Snapshot every payload before any staging write. Copies use these same hashed bytes.
$total=0L;$artifacts=@();$payload=@()
foreach($entry in $plan){
 $bytes=Read-ReleaseBytes $entry.path;$total+=$bytes.Length
 if($total -gt 256MB){throw 'Release payload exceeds 256 MiB'}
 $hash=Hash-Bytes $bytes
 $artifacts+=@([ordered]@{input=$entry.input;destination=$entry.destination;sha256=$hash})
 $payload+=@([PSCustomObject]@{destination=$entry.destination;bytes=$bytes;sha256=$hash;license=$entry.license})
}
$record=[ordered]@{
 schemaVersion=1;producer='tools/Prepare-Release.ps1';sourceCommit=$source.commit
 sourceTreeSha256=$source.tree;sourceDigestScope='git-tracked-regular-files';sourceDigestExcludes=@('release-files.json')
 sourcesLockSha256=$source.lock;buildProfileSha256=$source.profile
 build=[ordered]@{script='tools/Build.ps1';target='All';configuration='Release';freshProducts=$true;startedUtc=$started;completedUtc=$completed}
 artifacts=$artifacts
 integrity='This manifest excludes its own hash; the external release-files.json hashes its exact bytes. Local provenance is not a digital signature.'
}
$recordBytes=$utf8.GetBytes(($record | ConvertTo-Json -Depth 8))
$payload+=@([PSCustomObject]@{destination='undo-mod/build-manifest.json';bytes=$recordBytes;sha256=(Hash-Bytes $recordBytes);license='See undo-mod/THIRD-PARTY.md.'})
$entries=@($payload | ForEach-Object {[ordered]@{source=$_.destination;destination=$_.destination;sha256=$_.sha256;license=$_.license}})
$rootBytes=$utf8.GetBytes(([ordered]@{schemaVersion=1;files=$entries} | ConvertTo-Json -Depth 8))
$finalSource=Source-Snapshot
if($finalSource.commit -cne $source.commit -or $finalSource.tree -cne $source.tree){throw 'Source changed while capturing release payload'}
# Recheck all destinations as a batch before the first write, including the generated manifest.
Assert-PackagePath $manifestPath $repo
foreach($entry in $payload){
 Assert-PackageDestination $entry.destination
 $path=Join-Path $release $entry.destination;Assert-PackagePath $path $release
 if(Test-Path -LiteralPath $path){throw "Release destination already exists: $path"}
}
Assert-ReleaseTree @($payload.destination)
foreach($entry in $payload){Write-NewReleaseFile (Join-Path $release $entry.destination) $entry.bytes}
Assert-PackagePath $manifestPath $repo
[IO.File]::WriteAllBytes($manifestPath,$rootBytes)
$package=Read-PackageManifest $manifestPath $repo
[PSCustomObject]@{ReleaseRoot=$release;Manifest=$manifestPath;SourceCommit=$source.commit;FileCount=$package.Files.Count;ReadOnly=$false}
