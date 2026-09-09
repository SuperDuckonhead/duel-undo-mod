$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repo 'tools/PackageValidation.ps1')
$base=Join-Path $repo ('out/tests/prepare-release-'+[guid]::NewGuid().ToString('N'))
Assert-PackagePath $base (Join-Path $repo 'out')
[IO.Directory]::CreateDirectory($base)|Out-Null
$script:checks=0
function Check($value,$message){if(-not $value){throw $message};++$script:checks}
function Write-Fixture($path,$text){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null;[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))}
function Hash($path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
function Reject($action,$pattern){try{& $action|Out-Null;return $false}catch{return $_.Exception.Message -like $pattern}}
function Fixture($name,$duringBuild=''){
 $root=Join-Path $base $name;[IO.Directory]::CreateDirectory((Join-Path $root 'tools'))|Out-Null
 foreach($tool in @('Prepare-Release.ps1','PackageValidation.ps1')){[IO.File]::Copy((Join-Path $repo ('tools/'+$tool)),(Join-Path $root ('tools/'+$tool)),$false)}
 Write-Fixture (Join-Path $root '.gitignore') "out/
client/bin/
client/obj/
client/build/
bot/obj/
bot/bin/
bot-tests/obj/"
 Write-Fixture (Join-Path $root 'README.md') 'fixed fixture source'
 Write-Fixture (Join-Path $root 'sources.lock.json') '{"schemaVersion":1,"components":[]}'
 Write-Fixture (Join-Path $root 'build-profile.json') '{"commands":[{"targets":["All"],"configuration":"Release"}]}'
 Write-Fixture (Join-Path $root 'release-files.json') '{"oldGeneratedManifest":true}'
 foreach($doc in @('BUILD','INSTALL','COMPATIBILITY','RELEASE-NOTES','THIRD-PARTY')){Write-Fixture (Join-Path $root ('docs/'+$doc+'.md')) ('fixture '+$doc)}
 Write-Fixture (Join-Path $root 'docs/licenses/MIT.txt') 'fixture license text'
 $stub=@'
param([string]$Target,[string]$Configuration)
$ErrorActionPreference='Stop'
if($Target -ne 'All' -or $Configuration -ne 'Release'){throw 'Wrong build arguments'}
$root=Split-Path $PSScriptRoot
foreach($name in @('out/client/Release/ygopro-undo.exe','out/bot/Release/WindBot-undo.exe','out/bot/Release/WindBot-undo.exe.config','out/bot/Release/undo-deps/x86/sqlite3.dll','out/bot/Release/undo-deps/x64/sqlite3.dll')){
 $path=Join-Path $root $name;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null;[IO.File]::WriteAllText($path,('controlled build fixture '+$name))
}
[IO.File]::WriteAllText((Join-Path $root 'out/built.txt'),'All Release')
'@
 Write-Fixture (Join-Path $root 'tools/Build.ps1') ($stub+[char]10+$duringBuild)
 & git -C $root init --quiet
 & git -C $root config core.autocrlf false
 & git -C $root add .
 & git -C $root -c user.name=ReleaseFixture -c user.email=fixture@example.invalid commit --quiet -m 'Controlled release preparation fixture'
 if($LASTEXITCODE){throw 'Fixture Git initialization failed'}
 return $root
}
$ok=Fixture 'valid'
$prepare=Join-Path $ok 'tools/Prepare-Release.ps1'
Check (Reject {& $prepare} '*-Build*') 'default must not attest an unproven build'
Check (-not (Test-Path (Join-Path $ok 'out/release'))) 'default rejection does not create output'
$result=& $prepare -Build
$manifest=Read-PackageManifest (Join-Path $ok 'release-files.json') $ok
Check ($manifest.Files.Count -eq 12) 'exact five artifacts, five docs, license and build manifest'
Check ($manifest.Files.source -notcontains 'WindBot/WindBot.exe') 'ordinary bot executable absent'
$record=Read-PackageJson (Join-Path $ok 'out/release/undo-mod/build-manifest.json')
Check ($record.sourceCommit -eq (& git -C $ok rev-parse HEAD)) 'manifest binds actual fixture commit'
Check ($record.build.target -eq 'All' -and $record.build.configuration -eq 'Release') 'build arguments retained'
Check ($record.artifacts.Count -eq 11) 'build manifest excludes its own digest'
$before=(Get-Item (Join-Path $ok 'release-files.json')).LastWriteTimeUtc.Ticks
& $prepare|Out-Null
Check ((Get-Item (Join-Path $ok 'release-files.json')).LastWriteTimeUtc.Ticks -eq $before) 'default verification is read-only'
Write-Fixture (Join-Path $ok 'out/bot/Release/WindBot-undo.exe') 'stale or changed build artifact'
Check (Reject {& $prepare} '*hash*') 'default detects changed build artifact'
$stale=Fixture 'stale';Write-Fixture (Join-Path $stale 'out/client/Release/ygopro-undo.exe') 'old executable'
Check (Reject {& (Join-Path $stale 'tools/Prepare-Release.ps1') -Build} '*fresh*') 'fresh build refuses old products'
Check (-not (Test-Path (Join-Path $stale 'out/built.txt'))) 'old product refusal precedes build'
$dirty=Fixture 'dirty';Write-Fixture (Join-Path $dirty 'README.md') 'uncommitted change'
Check (Reject {& (Join-Path $dirty 'tools/Prepare-Release.ps1') -Build} '*clean*') 'dirty tracked source rejected'
$changed=Fixture 'during-build' '[IO.File]::AppendAllText((Join-Path $root "README.md"),"changed during build")'
$rootHash=Hash (Join-Path $changed 'release-files.json')
Check (Reject {& (Join-Path $changed 'tools/Prepare-Release.ps1') -Build} '*clean*') 'source modification during build rejected'
Check (-not (Test-Path (Join-Path $changed 'out/release')) -and (Hash (Join-Path $changed 'release-files.json')) -eq $rootHash) 'failed provenance leaves stage and root manifest untouched'
$collision=Fixture 'collision';Write-Fixture (Join-Path $collision 'out/release/ygopro-undo.exe') 'unmanaged original'
$sentinel=Hash (Join-Path $collision 'out/release/ygopro-undo.exe')
Check (Reject {& (Join-Path $collision 'tools/Prepare-Release.ps1') -Build} '*empty*') 'unknown release data rejected before build'
Check ((Hash (Join-Path $collision 'out/release/ygopro-undo.exe')) -eq $sentinel -and -not (Test-Path (Join-Path $collision 'out/built.txt'))) 'unknown target remains unchanged'
$linked=Fixture 'linked';$outside=Join-Path $base 'outside';[IO.Directory]::CreateDirectory($outside)|Out-Null
New-Item -ItemType Junction -Path (Join-Path $linked 'out') -Target $outside|Out-Null
Check (Reject {& (Join-Path $linked 'tools/Prepare-Release.ps1') -Build} '*reparse*') 'output ancestor junction rejected'
Check (@(Get-ChildItem -LiteralPath $outside -Force).Count -eq 0) 'junction destination stays untouched'

$late=Fixture 'late-stage' '[IO.Directory]::CreateDirectory((Join-Path $root "out/release"))|Out-Null;[IO.File]::WriteAllText((Join-Path $root "out/release/unknown.txt"),"keep")'
$lateHash=Hash (Join-Path $late 'release-files.json')
Check (Reject {& (Join-Path $late 'tools/Prepare-Release.ps1') -Build} '*Unknown release file*') 'late build staging contamination rejected'
Check (-not (Test-Path (Join-Path $late 'out/release/ygopro-undo.exe')) -and (Hash (Join-Path $late 'release-files.json')) -eq $lateHash) 'whole final preflight precedes first copy'
$manifestLink=Fixture 'manifest-link'
Remove-Item -LiteralPath (Join-Path $manifestLink 'release-files.json')
New-Item -ItemType Junction -Path (Join-Path $manifestLink 'release-files.json') -Target $outside|Out-Null
Check (Reject {& (Join-Path $manifestLink 'tools/Prepare-Release.ps1') -Build} '*reparse*') 'root manifest junction rejected before build'
Check (-not (Test-Path (Join-Path $manifestLink 'out/built.txt'))) 'manifest link preflight is read-only'
$staged=Fixture 'staged-tamper';$stagedScript=Join-Path $staged 'tools/Prepare-Release.ps1'
& $stagedScript -Build|Out-Null
Write-Fixture (Join-Path $staged 'out/release/ygopro-undo.exe') 'tampered staged payload'
Check (Reject {& $stagedScript} '*hash*') 'default rejects staged payload hash mismatch'
$newCommit=Fixture 'changed-commit';$newScript=Join-Path $newCommit 'tools/Prepare-Release.ps1'
& $newScript -Build|Out-Null
Write-Fixture (Join-Path $newCommit 'README.md') 'new committed source with old binaries'
& git -C $newCommit add README.md
& git -C $newCommit -c user.name=ReleaseFixture -c user.email=fixture@example.invalid commit --quiet -m 'New source, deliberately stale output'
Check (Reject {& $newScript} '*provenance*') 'clean newer commit cannot reuse old binary provenance'
$badName=Fixture 'bad-license'
Write-Fixture (Join-Path $badName 'docs/licenses/README.md') 'not an allowed release license path'
& git -C $badName add docs/licenses/README.md
& git -C $badName -c user.name=ReleaseFixture -c user.email=fixture@example.invalid commit --quiet -m 'Invalid license destination fixture'
Check (Reject {& (Join-Path $badName 'tools/Prepare-Release.ps1') -Build} '*not an allowed mod file*') 'all license names pass existing whitelist'
Check (-not (Test-Path (Join-Path $badName 'out/built.txt'))) 'bad license whitelist rejected before build'

Write-Host "Prepare-Release focused tests passed: $script:checks checks; fixture builds only. $base"
