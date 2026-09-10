$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repo 'tools/PackageValidation.ps1')
$base=Join-Path $repo ('out/tests/uninstall-'+[guid]::NewGuid().ToString('N'))
Assert-PackagePath $base (Join-Path $repo 'out')
[IO.Directory]::CreateDirectory($base)|Out-Null
$uninstaller=Join-Path $repo 'tools/Uninstall-UndoMod.ps1'
$script:checks=0
function Check([bool]$value,[string]$message){if(-not $value){throw $message};++$script:checks}
function Put([string]$path,[string]$text){Assert-PackagePath $path $base;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null;[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))}
function Hash([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Json([string]$path,$value){Put $path ($value|ConvertTo-Json -Depth 12)}
function Reject([scriptblock]$action){try{& $action|Out-Null;return $false}catch{return $true}}
$protected=@('ygopro.exe','Bot.exe','WindBot/WindBot.exe','sqlite3.dll','WindBot/SQLite.Interop.dll','cards.cdb','system.conf','system-undo.conf','undo-logs/keep.log','pics/card.jpg','deck/private.ydk','script/c1.lua','expansions/cards.cdb','single/practice.lua')
$payload=@('ygopro-undo.exe','WindBot/WindBot-undo.exe','WindBot/WindBot-undo.exe.config','WindBot/undo-deps/x86/sqlite3.dll','WindBot/undo-deps/x64/sqlite3.dll','undo-mod/INSTALL.md','undo-mod/licenses/MIT.txt')
function Fixture([string]$name,[switch]$Packaged){
 $root=Join-Path $base $name
 foreach($relative in $protected){Put (Join-Path $root $relative) ('protected '+$relative)}
 $entries=@(foreach($relative in $payload){$path=Join-Path $root $relative;Put $path ('mod '+$relative);[ordered]@{input=('fixture/'+$relative);destination=$relative;sha256=(Hash $path)}})
 if($Packaged){
  foreach($pair in @(@('tools/Uninstall-UndoMod.ps1','undo-mod/Uninstall-UndoMod.ps1'),@('tools/Uninstall-UndoMod.cmd','Uninstall-UndoMod.cmd'))){$target=Join-Path $root $pair[1];[IO.File]::Copy((Join-Path $repo $pair[0]),$target,$false);$entries+=@([ordered]@{input=$pair[0];destination=$pair[1];sha256=(Hash $target)})}
 }
 $manifest=[ordered]@{schemaVersion=1;producer='tools/Prepare-Release.ps1';sourceCommit=('a'*40);sourceTreeSha256=('b'*64);sourceDigestScope='git-tracked-regular-files';sourceDigestExcludes=@('release-files.json');sourcesLockSha256=('c'*64);buildProfileSha256=('d'*64);build=[ordered]@{script='tools/Build.ps1';target='All';configuration='Release';freshProducts=$true;startedUtc='2026-09-09T00:00:00Z';completedUtc='2026-09-09T00:01:00Z'};artifacts=$entries;integrity='This manifest excludes its own hash; the external release-files.json hashes its exact bytes. Local provenance is not a digital signature.'}
 Json (Join-Path $root 'undo-mod/build-manifest.json') $manifest
 return [PSCustomObject]@{Root=$root;Manifest=$manifest}
}
function Check-Protected($fixture){foreach($relative in $protected){Check ([IO.File]::ReadAllText((Join-Path $fixture.Root $relative)) -ceq ('protected '+$relative)) ('protected fixture bytes: '+$relative)}}
function Check-Payload($fixture){foreach($relative in $payload){Check (Test-Path -LiteralPath (Join-Path $fixture.Root $relative) -PathType Leaf) ('payload preserved: '+$relative)}}
function Invoke-Uninstall($fixture,[switch]$Preview){& $uninstaller -RuntimeRoot $fixture.Root -ConfirmRemoval -Preview:$Preview}

# A missing command is a clear initial red result, before any implementation exists.
Check (Test-Path -LiteralPath $uninstaller -PathType Leaf) 'Uninstall command must exist before removal can be tested'
$preview=Fixture 'preview'
$result=Invoke-Uninstall $preview -Preview
Check ($result.Status -eq 'Preview' -and $result.Removed.Count -eq 0) 'preview never reports actual removal'
Check-Payload $preview;Check-Protected $preview
Check (-not (Test-Path -LiteralPath (Join-Path $preview.Root 'undo-mod-backups'))) 'preview creates no backup or other output'
Check ($result.Planned -contains 'ygopro-undo.exe' -and $result.Planned -notcontains 'system-undo.conf') 'preview gives exact allowed removal paths'

$cancel=Fixture 'cancel'
$env:UNDO_TEST_SCRIPT=$uninstaller;$env:UNDO_TEST_ROOT=$cancel.Root
$cancelOutput=@('' | & powershell.exe -NoProfile -ExecutionPolicy Bypass -Command '& $env:UNDO_TEST_SCRIPT -RuntimeRoot $env:UNDO_TEST_ROOT | ConvertTo-Json -Depth 5' 2>&1)
Check-Payload $cancel;Check-Protected $cancel
Check (-not (Test-Path -LiteralPath (Join-Path $cancel.Root 'undo-mod-backups'))) 'default answer cancels without writes'
Check (($cancelOutput -join "`n") -match 'Cancelled') 'blank response is reported as cancellation'

$ok=Fixture '卸载 测试 空格' -Packaged
$installedScript=Join-Path $ok.Root 'undo-mod/Uninstall-UndoMod.ps1'
$before=@{};foreach($relative in @($ok.Manifest.artifacts.destination)+@('undo-mod/build-manifest.json')){$before[$relative]=Hash (Join-Path $ok.Root $relative)}
$result=& $installedScript -ConfirmRemoval
Check ($result.Status -eq 'Complete') 'installed script infers the game root and reports complete removal'
foreach($relative in $before.Keys){Check (-not (Test-Path -LiteralPath (Join-Path $ok.Root $relative))) ('owned file removed: '+$relative);Check ((Hash (Join-Path $result.BackupPath ('files/'+$relative))) -ceq $before[$relative]) ('backup exact bytes: '+$relative)}
Check-Protected $ok
Check (Test-Path -LiteralPath (Join-Path $result.BackupPath 'uninstall-receipt.json') -PathType Leaf) 'recovery backup includes removed file receipt'
$again=Invoke-Uninstall $ok
Check ($again.Status -eq 'AlreadyAbsent' -and $again.Removed.Count -eq 0) 'completed removal can be checked again safely'

# The exact production removal handle must deny writers and replacements until
# its verified file is marked for deletion, without reopening the path.
Check ($null -ne ('UndoModFileRemoval' -as [type])) 'removal uses a native handle carrying DELETE access'
$racePath=Join-Path $base 'contended verified file.bin';Put $racePath 'exact verified payload'
$replacement=Join-Path $base 'replacement.bin';Put $replacement 'unverified replacement'
$removalHandle=[UndoModFileRemoval]::Open($racePath)
try{
 Check (Reject {[IO.File]::WriteAllText($racePath,'changed bytes')}) 'verified deletion handle blocks a competing writer'
 Check (Reject {[IO.File]::Delete($racePath)}) 'verified deletion handle blocks unlink and replacement'
 Check (Reject {[IO.File]::Replace($replacement,$racePath,$null)}) 'verified deletion handle blocks atomic replacement'
 $reader=[IO.StreamReader]::new($removalHandle,[Text.Encoding]::UTF8,$true,1024,$true)
 try{Check ($reader.ReadToEnd() -ceq 'exact verified payload') 'same deletion handle retains exact backed-up bytes'}finally{$reader.Dispose()}
 [UndoModFileRemoval]::MarkForDeletion($removalHandle)
}finally{$removalHandle.Dispose()}
Check (-not (Test-Path -LiteralPath $racePath) -and [IO.File]::ReadAllText($replacement) -ceq 'unverified replacement') 'handle-bound removal deletes only verified file and keeps attempted replacement untouched'
$unmarked=Join-Path $base 'unmarked verified file.bin';Put $unmarked 'backup failure leaves this'
$removalHandle=[UndoModFileRemoval]::Open($unmarked);$removalHandle.Dispose()
Check ([IO.File]::ReadAllText($unmarked) -ceq 'backup failure leaves this') 'closing an unmarked preflight handle never deletes a file'

$launched=Fixture '双击 路径 with apostrophe''s' -Packaged
$env:UNDO_TEST_LAUNCHER=Join-Path $launched.Root 'Uninstall-UndoMod.cmd'
$ErrorActionPreference='Continue'
$launchOutput=@(@('YES','') | & cmd.exe /d /s /c '""%UNDO_TEST_LAUNCHER%""' 2>&1)
$launchCode=$LASTEXITCODE
$ErrorActionPreference='Stop'
Put (Join-Path $base 'launcher-output.txt') ($launchOutput -join "`n")
Check (-not (Test-Path -LiteralPath $env:UNDO_TEST_LAUNCHER) -and -not (Test-Path -LiteralPath (Join-Path $launched.Root 'undo-mod/Uninstall-UndoMod.ps1'))) 'actual CMD launcher removes its installed copy and script after confirmation'
Check ($launchCode -eq 0 -and ($launchOutput -join "`n") -notmatch 'batch file cannot be found') 'self-removing CMD launcher exits successfully'
Check (($launchOutput -join "`n") -notmatch 'RuntimeRoot\s*:|FullyQualifiedErrorId|CategoryInfo\s*:') 'interactive launcher presents friendly status without PowerShell object or exception internals'
Check-Protected $launched

$partial=Fixture 'modified and unknown'
Put (Join-Path $partial.Root 'ygopro-undo.exe') 'user modified mod'
Put (Join-Path $partial.Root 'undo-mod/notes.txt') 'user note'
$result=Invoke-Uninstall $partial
Check ($result.Status -eq 'Partial' -and $result.Preserved -contains 'ygopro-undo.exe' -and $result.Preserved -contains 'undo-mod/notes.txt') 'changed and unknown files produce an explicit partial result'
Check ([IO.File]::ReadAllText((Join-Path $partial.Root 'ygopro-undo.exe')) -ceq 'user modified mod') 'changed client is not removed'
Check ([IO.File]::ReadAllText((Join-Path $partial.Root 'undo-mod/notes.txt')) -ceq 'user note') 'unknown file is not removed'
Check (Test-Path -LiteralPath (Join-Path $partial.Root 'undo-mod/build-manifest.json')) 'partial removal keeps ownership metadata for retry'
Check (-not (Test-Path -LiteralPath (Join-Path $partial.Root 'WindBot/WindBot-undo.exe'))) 'partial removal can remove other independently verified payload'
Check ((Invoke-Uninstall $partial).Status -eq 'Partial') 'repeat partial removal still reports unresolved files'
Check-Protected $partial

$retry=Fixture 'packaged partial keeps tools' -Packaged
Put (Join-Path $retry.Root 'undo-mod/notes.txt') 'keep me'
$result=Invoke-Uninstall $retry
Check ($result.Status -eq 'Partial' -and (Test-Path -LiteralPath (Join-Path $retry.Root 'Uninstall-UndoMod.cmd')) -and (Test-Path -LiteralPath (Join-Path $retry.Root 'undo-mod/Uninstall-UndoMod.ps1'))) 'partial removal retains usable packaged uninstall tools for a later retry'

$env:UNDO_TEST_LAUNCHER=Join-Path $retry.Root 'Uninstall-UndoMod.cmd'
$ErrorActionPreference='Continue'
$retryOutput=@(@('YES','') | & cmd.exe /d /s /c '""%UNDO_TEST_LAUNCHER%""' 2>&1);$retryCode=$LASTEXITCODE
$ErrorActionPreference='Stop'
Check ($retryCode -ne 0 -and ($retryOutput -join "`n") -notmatch 'RuntimeRoot\s*:|FullyQualifiedErrorId') 'interactive partial removal has nonzero exit and friendly output'

$locked=Fixture 'locked'
$handle=[IO.File]::Open((Join-Path $locked.Root 'WindBot/WindBot-undo.exe'),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try{Check (Reject {Invoke-Uninstall $locked}) 'locked file aborts whole removal before mutation'}finally{$handle.Dispose()}
Check-Payload $locked;Check-Protected $locked
Check (-not (Test-Path -LiteralPath (Join-Path $locked.Root 'undo-mod-backups'))) 'lock failure creates no backup'

$lockedLauncher=Fixture 'locked launcher' -Packaged
$env:UNDO_TEST_LAUNCHER=Join-Path $lockedLauncher.Root 'Uninstall-UndoMod.cmd'
$handle=[IO.File]::Open((Join-Path $lockedLauncher.Root 'WindBot/WindBot-undo.exe'),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try{
 $ErrorActionPreference='Continue'
 $failureOutput=@(@('','') | & cmd.exe /d /s /c '""%UNDO_TEST_LAUNCHER%""' 2>&1);$failureCode=$LASTEXITCODE
 $ErrorActionPreference='Stop'
 Check ($failureCode -ne 0 -and ($failureOutput -join "`n") -notmatch 'RuntimeRoot\s*:|FullyQualifiedErrorId|CategoryInfo\s*:') 'interactive lock failure has nonzero exit and no raw exception stack'
 Check-Payload $lockedLauncher
}finally{$handle.Dispose()}

$readonly=Fixture 'readonly'
$readOnlyPath=Join-Path $readonly.Root 'WindBot/WindBot-undo.exe'
[IO.File]::SetAttributes($readOnlyPath,[IO.FileAttributes]::ReadOnly)
try{Check (Reject {Invoke-Uninstall $readonly}) 'read-only file aborts all deletions'}finally{[IO.File]::SetAttributes($readOnlyPath,[IO.FileAttributes]::Normal)}
Check-Payload $readonly

$backupBlocked=Fixture 'backup blocked'
Put (Join-Path $backupBlocked.Root 'undo-mod-backups') 'user file blocks backup'
Check (Reject {Invoke-Uninstall $backupBlocked}) 'backup failure prevents deletion'
Check-Payload $backupBlocked

foreach($bad in @('../ygopro.exe','ygopro.exe','Bot.exe','system-undo.conf','cards.cdb','script/c1.lua','single/practice.lua','WindBot/WindBot.exe','ygopro-undo.exe:stream','YGOPRO-UNDO.EXE','undo-mod/licenses/../INSTALL.md')){
 $fixture=Fixture ('bad-'+[guid]::NewGuid().ToString('N'))
 $fixture.Manifest.artifacts[0].destination=$bad
 Json (Join-Path $fixture.Root 'undo-mod/build-manifest.json') $fixture.Manifest
 Check (Reject {Invoke-Uninstall $fixture}) ('invalid or protected destination rejected: '+$bad)
 Check-Payload $fixture;Check-Protected $fixture
}
$duplicate=Fixture 'duplicate';$duplicate.Manifest.artifacts+=@($duplicate.Manifest.artifacts[0]);Json (Join-Path $duplicate.Root 'undo-mod/build-manifest.json') $duplicate.Manifest
Check (Reject {Invoke-Uninstall $duplicate}) 'duplicate manifest destination rejected';Check-Payload $duplicate
$duplicateKey=Fixture 'duplicate key';$manifestPath=Join-Path $duplicateKey.Root 'undo-mod/build-manifest.json'
Put $manifestPath ([IO.File]::ReadAllText($manifestPath).Replace('"schemaVersion":  1','"schemaVersion":  1, "schemaVersion":  1').Replace('"schemaVersion": 1','"schemaVersion": 1, "schemaVersion": 1'))
Check (Reject {Invoke-Uninstall $duplicateKey}) 'duplicate JSON field rejected';Check-Payload $duplicateKey
$invalid=Fixture 'invalid schema';$invalid.Manifest.schemaVersion=2;Json (Join-Path $invalid.Root 'undo-mod/build-manifest.json') $invalid.Manifest
Check (Reject {Invoke-Uninstall $invalid}) 'unsupported manifest schema rejected';Check-Payload $invalid

$outside=Join-Path $base 'junction target';Put (Join-Path $outside 'keep.txt') 'outside sentinel'
$linked=Fixture 'linked'
# Move the fixture-only owned directory aside, then replace it with a junction.
$owned=[IO.Path]::GetFullPath((Join-Path $linked.Root 'WindBot/undo-deps'));$moved=[IO.Path]::GetFullPath((Join-Path $linked.Root 'moved-private-deps'))
Assert-PackagePath $owned $base;Assert-PackagePath $moved $base
Move-Item -LiteralPath $owned -Destination $moved
New-Item -ItemType Junction -Path $owned -Target $outside|Out-Null
Check (Reject {Invoke-Uninstall $linked}) 'payload reparse ancestor rejected before deleting anything'
Check (Test-Path -LiteralPath (Join-Path $linked.Root 'ygopro-undo.exe')) 'earlier valid payload preserved on late reparse failure'
$backupLink=Fixture 'backup junction'
New-Item -ItemType Junction -Path (Join-Path $backupLink.Root 'undo-mod-backups') -Target $outside|Out-Null
Check (Reject {Invoke-Uninstall $backupLink}) 'backup reparse directory rejected';Check-Payload $backupLink
Check (@(Get-ChildItem -LiteralPath $outside -Force).Count -eq 1 -and [IO.File]::ReadAllText((Join-Path $outside 'keep.txt')) -ceq 'outside sentinel') 'reparse targets receive no writes'

function Add-LocalFix($fixture,[switch]$WrongClient){
 Put (Join-Path $fixture.Root 'ygopro-undo.exe') 'local client fix'
 $record=[ordered]@{schemaVersion=1;kind='local-incremental-development-fix';scope='fixture only';installedAt='2026-09-11T02:57:32+08:00';sourceCommit=('e'*40);workingTreeChanges=$true;sources=@([ordered]@{path='client/gframe/game.cpp';sha256=('f'*64)});baseCandidateCommit=$fixture.Manifest.sourceCommit;runtimeRoot=$fixture.Root;client=[ordered]@{sha256=(Hash (Join-Path $fixture.Root 'ygopro-undo.exe'));path=(Join-Path $fixture.Root 'ygopro-undo.exe')};rollback=[ordered]@{previousOverrideRecord=(Join-Path $fixture.Root 'does-not-grant-authority.json');backup=(Join-Path $fixture.Root 'does-not-grant-authority.exe');sha256=('1'*64);note='metadata only'};preservedFiles=@([ordered]@{path='undo-mod\build-manifest.json';sha256=(Hash (Join-Path $fixture.Root 'undo-mod/build-manifest.json'))});buildLog='metadata only';evidence='metadata only';note='metadata only'}
 if($WrongClient){$record.client.path=Join-Path $fixture.Root 'ygopro.exe'}
 Json (Join-Path $fixture.Root 'undo-mod/local-fix.json') $record
}
$legacy=Fixture 'legacy override';Add-LocalFix $legacy
$result=Invoke-Uninstall $legacy
Check ($result.Status -eq 'Complete' -and -not (Test-Path -LiteralPath (Join-Path $legacy.Root 'ygopro-undo.exe'))) 'validated legacy override permits removal of locally fixed client'
Check (Test-Path -LiteralPath (Join-Path $result.BackupPath 'files/undo-mod/local-fix.json')) 'legacy record is backed up with exact removed payload'
Check-Protected $legacy
$invalidFix=Fixture 'invalid override';Add-LocalFix $invalidFix -WrongClient
Check (Reject {Invoke-Uninstall $invalidFix}) 'local override cannot redirect removal to original client';Check-Payload $invalidFix;Check-Protected $invalidFix
$unboundFix=Fixture 'unbound override';Add-LocalFix $unboundFix
Put (Join-Path $unboundFix.Root 'undo-mod/build-manifest.json') (([IO.File]::ReadAllText((Join-Path $unboundFix.Root 'undo-mod/build-manifest.json')))+' ')
Check (Reject {Invoke-Uninstall $unboundFix}) 'legacy record must bind exact base manifest bytes';Check-Payload $unboundFix

$running=Fixture 'running original'
$exe=Join-Path $running.Root 'ygopro.exe'
[IO.File]::Delete($exe)
# Use the installed .NET Framework compiler for a runnable fixture even when
# this suite runs under PowerShell 7, whose Add-Type cannot emit executable apps.
$fixtureCompiler=Join-Path $base 'compile-process.ps1'
Put $fixtureCompiler @'
param([string]$OutputPath)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition 'public class UndoUninstallFixtureProcess { public static void Main() { System.Threading.Thread.Sleep(60000); } }' -OutputAssembly $OutputPath -OutputType ConsoleApplication
'@
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $fixtureCompiler -OutputPath $exe
Check ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $exe -PathType Leaf)) 'controlled process fixture compiled successfully'
$process=Start-Process -FilePath $exe -WindowStyle Hidden -PassThru
try{Check (Reject {Invoke-Uninstall $running}) 'running game blocks all deletions even when mod files are unlocked';Check-Payload $running}finally{if(-not $process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose()}
Check (-not (Test-Path -LiteralPath (Join-Path $running.Root 'undo-mod-backups'))) 'running process preflight creates no backup'

Write-Host "Uninstaller tests passed: $script:checks checks. Controlled fixtures remain at $base"
