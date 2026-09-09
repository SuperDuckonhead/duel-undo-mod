param(
    [Parameter(Mandatory)][string]$CandidateRoot,
    [string]$RuntimeRoot='F:/MyCardLibrary/ygopro'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repo 'tools/PackageValidation.ps1')
$candidate=[IO.Path]::GetFullPath($CandidateRoot)
$runtime=[IO.Path]::GetFullPath($RuntimeRoot)
$out=Join-Path $repo 'out'
Assert-PackagePath $out $repo
Assert-PackagePath (Join-Path $candidate 'ygopro-undo.exe')
if(-not (Test-Path -LiteralPath (Join-Path $candidate 'ygopro-undo.exe') -PathType Leaf)){throw 'Candidate executable missing'}
if(-not (Test-Path -LiteralPath (Join-Path $runtime 'ygopro.exe') -PathType Leaf)){throw 'Original executable missing'}
# Root files and complete installed deck/WindBot trees cover original programs,
# configuration, databases, bot decks/dialogs and native bot dependencies.
# Deliberately do not traverse the separate dev checkout/build directories.
function OriginalSnapshot {
    $paths=[Collections.Generic.List[string]]::new()
    foreach($file in Get-ChildItem -LiteralPath $runtime -File -Force){$paths.Add($file.FullName)}
    foreach($name in 'deck','WindBot'){
        $tree=Join-Path $runtime $name
        Assert-PackagePath $tree
        foreach($file in Get-ChildItem -LiteralPath $tree -Recurse -File -Force){Assert-PackagePath $file.FullName;$paths.Add($file.FullName)}
    }
    $rows=@(foreach($path in $paths|Sort-Object -Unique){[pscustomobject]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}})
    return ,$rows
}
$candidateHash=(Get-FileHash -LiteralPath (Join-Path $candidate 'ygopro-undo.exe')).Hash.ToLowerInvariant()
$before=OriginalSnapshot
$id=[guid]::NewGuid().ToString('N')
$stage=Join-Path $out ('candidate-process-'+$id)
$missing=Join-Path $out ('candidate-process-missing-'+$id)
function PrepareStage([string]$Target,[bool]$Database){
    Assert-PackagePath $Target $out
    if(Test-Path -LiteralPath $Target){throw "Stage already exists: $Target"}
    [IO.Directory]::CreateDirectory($Target)|Out-Null
    foreach($name in 'pics','script','expansions','pack','fonts','textures','sound','single'){
        $source=Join-Path $runtime $name
        if(-not (Test-Path -LiteralPath $source -PathType Container)){throw "Missing shared directory $name"}
        New-Item -ItemType Junction -Path (Join-Path $Target $name) -Target $source|Out-Null
    }
    foreach($name in 'strings.conf','lflist.conf','bot.conf'){
        Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination (Join-Path $Target $name)
    }
    if($Database){Copy-Item -LiteralPath (Join-Path $runtime 'cards.cdb') -Destination (Join-Path $Target 'cards.cdb')}
    foreach($name in 'deck','replay','WindBot'){[IO.Directory]::CreateDirectory((Join-Path $Target $name))|Out-Null}
    $config="use_d3d = 0`nantialias = 0`nnickname = Candidate Process Test`nwindow_width = 1024`nwindow_height = 640`nenable_sound = 0`nenable_music = 0`nenable_bot_mode = 0`nbot_room_public = 0`n"
    [IO.File]::WriteAllText((Join-Path $Target 'system.conf'),$config,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $Target '.candidate-process-owned'),$id,[Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $candidate 'ygopro-undo.exe') -Destination (Join-Path $Target 'ygopro-undo.exe')
    if((Get-FileHash -LiteralPath (Join-Path $Target 'ygopro-undo.exe')).Hash.ToLowerInvariant() -ne $candidateHash){throw 'Candidate stage copy SHA mismatch'}
}
PrepareStage $stage $true
$log=Join-Path $stage 'candidate-process.log'
function Log([string]$Message){$line=[DateTime]::UtcNow.ToString('o')+' '+$Message;Add-Content -LiteralPath $log -Value $line -Encoding utf8;Write-Host $line}
if(-not ('CandidateOwnedWindows' -as [type])){
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class CandidateOwnedWindows {
    delegate bool EnumProc(IntPtr window,IntPtr param);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr param);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int size);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window,StringBuilder name,int size);
    [DllImport("user32.dll",SetLastError=true)] static extern bool PostMessage(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
    [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr window,uint message,IntPtr wparam,IntPtr lparam,uint flags,uint timeout,out IntPtr result);
    public static IntPtr Ready(uint process) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((window,param)=>{uint owner;GetWindowThreadProcessId(window,out owner);if(owner!=process)return true;
            var name=new StringBuilder(256);GetClassName(window,name,name.Capacity);if(name.ToString()!="CIrrDeviceWin32")return true;
            name.Clear();GetWindowText(window,name,name.Capacity);if(name.ToString()!="YGOPro")return true;
            IntPtr result;if(SendMessageTimeout(window,0,IntPtr.Zero,IntPtr.Zero,2,500,out result)!=IntPtr.Zero)found=window;
            return true;},IntPtr.Zero);
        return found;
    }
    public static bool Close(uint process,IntPtr window){uint owner;GetWindowThreadProcessId(window,out owner);return owner==process && PostMessage(window,0x10,IntPtr.Zero,IntPtr.Zero);}
}
"@
}
$owned=[Collections.Generic.List[Diagnostics.Process]]::new()
$results=[Collections.Generic.List[object]]::new()
function Launch([string]$Directory,[string]$Name,[string]$Label){
    $child=Start-Process -FilePath (Join-Path $Directory $Name) -WorkingDirectory $Directory -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $Directory ($Label+'.stdout.log')) -RedirectStandardError (Join-Path $Directory ($Label+'.stderr.log'))
    $null=$child.Handle # retain the actual process handle, not only a reusable PID
    $owned.Add($child)
    Log "LAUNCH $Label pid=$($child.Id)"
    return $child
}
function Ready([Diagnostics.Process]$Child,[bool]$Mod){
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt 45){
        if($Child.HasExited){throw "PID $($Child.Id) exited before initialization: $($Child.ExitCode)"}
        $window=[CandidateOwnedWindows]::Ready([uint32]$Child.Id)
        if($window -ne [IntPtr]::Zero -and (-not $Mod -or (Test-Path -LiteralPath (Join-Path $stage 'system-undo.conf') -PathType Leaf))){return $window}
        Start-Sleep -Milliseconds 100
    }
    throw "PID $($Child.Id) initialization timeout"
}
function CloseNormal([Diagnostics.Process]$Child,[IntPtr]$Window,[string]$Label){
    if(-not [CandidateOwnedWindows]::Close([uint32]$Child.Id,$Window)){throw "Owned window shutdown request failed: $Label"}
    if(-not $Child.WaitForExit(15000) -or $Child.ExitCode -ne 0){throw "Normal shutdown failed: $Label"}
    $results.Add([pscustomobject]@{case=$Label;pid=$Child.Id;exitCode=$Child.ExitCode;normalClose=$true})
    Log "PASS $Label normal exit0 pid=$($Child.Id)"
}
$failure=$null
try {
    $original=Join-Path $runtime 'ygopro.exe'
    Copy-Item -LiteralPath $original -Destination (Join-Path $stage 'ygopro.exe')
    $originalHash=(Get-FileHash -LiteralPath $original).Hash
    if((Get-FileHash -LiteralPath (Join-Path $stage 'ygopro.exe')).Hash -ne $originalHash){throw 'Original byte copy mismatch'}
    $child=Launch $stage 'ygopro.exe' 'original'
    CloseNormal $child (Ready $child $false) 'original'
    $legacyHash=(Get-FileHash -LiteralPath (Join-Path $stage 'system.conf')).Hash
    $child=Launch $stage 'ygopro-undo.exe' 'candidate'
    CloseNormal $child (Ready $child $true) 'candidate'
    $expectedConfig=[IO.File]::ReadAllText((Join-Path $stage 'system-undo.conf'))
    $expectedConfigSha=(Get-FileHash -LiteralPath (Join-Path $stage 'system-undo.conf')).Hash.ToLowerInvariant()
    foreach($key in 'use_d3d','antialias','enable_sound','enable_music','enable_bot_mode'){
        if($expectedConfig -notmatch ('(?m)^'+$key+' = 0\r?$')){throw "Sequential config lost controlled value $key"}
    }
    $first=Launch $stage 'ygopro-undo.exe' 'concurrent-a'
    $second=Launch $stage 'ygopro-undo.exe' 'concurrent-b'
    $firstWindow=Ready $first $true;$secondWindow=Ready $second $true
    if($first.HasExited -or $second.HasExited){throw 'Two initialized candidate processes did not coexist'}
    Log "PASS simultaneous initialized candidate PIDs=$($first.Id),$($second.Id)"
    # Queue both normal closes before waiting: exercise concurrent config saves.
    if(-not [CandidateOwnedWindows]::Close([uint32]$first.Id,$firstWindow) -or -not [CandidateOwnedWindows]::Close([uint32]$second.Id,$secondWindow)){throw 'Concurrent normal close request failed'}
    foreach($child in $first,$second){if(-not $child.WaitForExit(15000) -or $child.ExitCode -ne 0){throw 'Concurrent normal exit failed'};$results.Add([pscustomobject]@{case='concurrent';pid=$child.Id;exitCode=$child.ExitCode;normalClose=$true})}
    $config=[IO.File]::ReadAllText((Join-Path $stage 'system-undo.conf'))
    if($config -cne $expectedConfig -or (Get-FileHash -LiteralPath (Join-Path $stage 'system-undo.conf')).Hash.ToLowerInvariant() -ne $expectedConfigSha){throw 'Concurrent config is not byte-identical to the complete sequential save'}
    if(-not $config.StartsWith('# YGOPro undo configuration') -or $config.Contains([char]0)){throw 'Candidate configuration is incomplete'}
    foreach($line in $config -split '\r?\n'){if($line -and -not $line.StartsWith('#') -and $line -notmatch '^[^=\s]+ = [^\r\n]*$'){throw "Malformed saved config: $line"}}
    if(@(Get-ChildItem -LiteralPath $stage -File -Filter 'system-undo.conf.*.tmp').Count){throw 'Candidate configuration left temporary files'}
    $child=Launch $stage 'ygopro-undo.exe' 'relaunch'
    CloseNormal $child (Ready $child $true) 'relaunch'
    if((Get-FileHash -LiteralPath (Join-Path $stage 'system.conf')).Hash -ne $legacyHash){throw 'Candidate modified controlled original configuration'}
    PrepareStage $missing $false
    $child=Launch $missing 'ygopro-undo.exe' 'missing-database'
    if(-not $child.WaitForExit(15000)){throw 'Missing-database exit timed out; no dialog interaction attempted'}
    if($child.ExitCode -ne 1){throw "Unexpected missing-database exit: $($child.ExitCode)"}
    $errorFiles=@(Get-ChildItem -LiteralPath (Join-Path $missing 'undo-logs') -File -Filter ('client-'+$child.Id+'-*.log'))
    if($errorFiles.Count -ne 1){throw 'Missing process-specific database error log'}
    $errorText=[IO.File]::ReadAllText($errorFiles[0].FullName)
    $expected=(Join-Path $missing 'cards.cdb').Replace('\','/')
    if(-not $errorText.Contains('Failed to load card database:') -or -not $errorText.Replace('\','/').Contains($expected)){throw 'Missing-database error did not identify isolated path'}
    $results.Add([pscustomobject]@{case='missing-database';pid=$child.Id;exitCode=$child.ExitCode;errorLog=$errorFiles[0].FullName})
    Log "PASS missing cards.cdb actual exit1 with isolated absolute path; no MessageBox action"
} catch {$failure=$_;Log ('FAIL '+$_.Exception.Message)} finally {
    foreach($child in $owned){if(-not $child.HasExited){Log "CLEANUP terminate owned PID=$($child.Id) after failed test";$child.Kill();[void]$child.WaitForExit(10000)};$child.Dispose()}
    $after=OriginalSnapshot
    if(($before|ConvertTo-Json -Depth 3 -Compress) -cne ($after|ConvertTo-Json -Depth 3 -Compress)){throw 'Original installation files changed during acceptance'}
    Log "PASS original files unchanged count=$($before.Count)"
    if((Get-FileHash -LiteralPath (Join-Path $candidate 'ygopro-undo.exe')).Hash.ToLowerInvariant() -ne $candidateHash){throw 'Candidate executable changed during test'}
    $record=[ordered]@{schemaVersion=1;candidateRoot=$candidate;candidateSha256=$candidateHash;stage=$stage;missingStage=$missing;originalFiles=$before;sequentialConfigSha256=$expectedConfigSha;cases=$results;passed=($null -eq $failure)}
    [IO.File]::WriteAllText((Join-Path $stage 'result.json'),($record|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
}
if($failure){throw $failure}
Log 'PASS actual original/candidate coexistence, concurrent candidate config and missing-database process acceptance'