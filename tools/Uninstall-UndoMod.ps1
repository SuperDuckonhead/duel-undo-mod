#requires -Version 5.1
[CmdletBinding()]
param([string]$RuntimeRoot,[switch]$Preview,[switch]$ConfirmRemoval,[switch]$Interactive)
$ErrorActionPreference='Stop'

# This standalone utility deliberately never imports code from the installation.
# Installed manifests describe ownership, not a digital signature. The fixed
# destination allowlist remains authoritative even when a manifest is edited.
function Assert-UninstallPath([string]$Path,[string]$Root='') {
 $full=[IO.Path]::GetFullPath($Path)
 if($full.Substring([IO.Path]::GetPathRoot($full).Length).Contains(':')){throw '拒绝备用数据流路径 (ADS)。'}
 if($Root){$base=[IO.Path]::GetFullPath($Root).TrimEnd([char[]]@('\','/'));if($full -ine $base -and -not $full.StartsWith($base+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw "路径超出游戏目录：$full"}}
 $cursor=$full
 while($cursor){
  $entry=$null
  try{$entry=Get-Item -LiteralPath $cursor -Force -ErrorAction Stop}catch [System.Management.Automation.ItemNotFoundException]{}
  if($entry -and ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "拒绝链接或重解析点 (reparse)：$cursor"}
  if($entry -and $cursor -ine $full -and -not $entry.PSIsContainer){throw "上级路径不是目录：$cursor"}
  $parent=[IO.Path]::GetDirectoryName($cursor);if($parent -eq $cursor){break};$cursor=$parent
 }
}
function Assert-UninstallRelative([string]$Path) {
 if([string]::IsNullOrWhiteSpace($Path) -or $Path.Length -gt 240 -or $Path.Contains('\') -or [IO.Path]::IsPathRooted($Path)){throw "无效的清单相对路径：$Path"}
 foreach($part in $Path.Split('/')){if($part -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or $part.EndsWith('.') -or $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])([.]|$)'){throw "无效的清单路径：$Path"}}
}
function Test-UninstallDestination([string]$Path) {
 $fixed=@('ygopro-undo.exe','WindBot/WindBot-undo.exe','WindBot/WindBot-undo.exe.config','WindBot/undo-deps/x86/sqlite3.dll','WindBot/undo-deps/x64/sqlite3.dll','undo-mod/BUILD.md','undo-mod/INSTALL.md','undo-mod/COMPATIBILITY.md','undo-mod/RELEASE-NOTES.md','undo-mod/THIRD-PARTY.md','undo-mod/Uninstall-UndoMod.ps1','Uninstall-UndoMod.cmd')
 return ($fixed -ccontains $Path -or $Path -cmatch '^undo-mod/licenses/[A-Za-z0-9][A-Za-z0-9._-]{0,63}[.]txt$')
}
function Assert-UninstallProperties($Value,[string[]]$Expected) {
 if($null -eq $Value -or $Value -isnot [PSCustomObject]){throw '清单需要 JSON 对象。'}
 $names=@($Value.PSObject.Properties.Name)
 if($names.Count -ne $Expected.Count){throw '清单字段不完整或包含未知字段。'}
 foreach($name in $names){if($Expected -cnotcontains $name){throw "清单字段无效：$name"}}
}
function Assert-UninstallHashValue($Hash){if($Hash -isnot [string] -or $Hash -cnotmatch '^[a-fA-F0-9]{64}$'){throw '无效的 SHA256 校验值。'}}
function Get-UninstallStreamHash($Stream){
 $Stream.Position=0;$sha=[Security.Cryptography.SHA256]::Create()
 try{return ([BitConverter]::ToString($sha.ComputeHash($Stream))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose();$Stream.Position=0}
}
function Initialize-UninstallNative {
 if('UndoModFileRemoval' -as [type]){return}
 Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
public static class UndoModFileRemoval {
    [StructLayout(LayoutKind.Sequential)]
    private struct AttributeTag { public uint Attributes; public uint ReparseTag; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Disposition { [MarshalAs(UnmanagedType.U1)] public bool Delete; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
    private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetFileInformationByHandleEx(SafeFileHandle handle, int infoClass,
        out AttributeTag info, uint size);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
    private static extern uint GetFinalPathNameByHandleW(SafeFileHandle handle,
        StringBuilder path, uint length, uint flags);
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetFileInformationByHandle(SafeFileHandle handle, int infoClass,
        ref Disposition info, uint size);
    public static FileStream Open(string path) {
        // GENERIC_READ | DELETE, no sharing, OPEN_EXISTING, OPEN_REPARSE_POINT.
        // DELETE rights are acquired for the entire batch before any removal.
        SafeFileHandle handle=CreateFileW(path, 0x80010000, 0, IntPtr.Zero, 3, 0x00200000, IntPtr.Zero);
        if(handle.IsInvalid) { int error=Marshal.GetLastWin32Error(); handle.Dispose(); throw new Win32Exception(error); }
        try {
            AttributeTag info;
            if(!GetFileInformationByHandleEx(handle,9,out info,8)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if((info.Attributes & (0x10 | 0x400)) != 0) throw new IOException("Removal requires a regular file without a reparse point.");
            var finalPath=new StringBuilder(32768);
            uint count=GetFinalPathNameByHandleW(handle,finalPath,(uint)finalPath.Capacity,0);
            if(count==0 || count>=finalPath.Capacity) throw new Win32Exception(Marshal.GetLastWin32Error());
            string resolved=finalPath.ToString();
            if(resolved.StartsWith(@"\\?\UNC\",StringComparison.OrdinalIgnoreCase)) resolved=@"\\"+resolved.Substring(8);
            else if(resolved.StartsWith(@"\\?\",StringComparison.Ordinal)) resolved=resolved.Substring(4);
            if(!String.Equals(Path.GetFullPath(path),resolved,StringComparison.OrdinalIgnoreCase))
                throw new IOException("Opened removal handle resolves to a different path.");
            return new FileStream(handle,FileAccess.Read,4096,false);
        } catch { handle.Dispose(); throw; }
    }
    public static void MarkForDeletion(FileStream stream) {
        // Set disposition on the SAME verified/backed-up open file; never close
        // and then delete by path, which could target an unverified replacement.
        var info=new Disposition { Delete=true };
        if(!SetFileInformationByHandle(stream.SafeFileHandle,4,ref info,1))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }
}
'@ | Out-Null
}
function Read-UninstallJson([string]$Path) {
 Assert-UninstallPath $Path
 $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 try{
  if($stream.Length -gt 4MB){throw '清单超过 4 MiB 大小限制。'}
  $hash=Get-UninstallStreamHash $stream
  $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$true,4096,$true)
  try{$text=$reader.ReadToEnd()}finally{$reader.Dispose()}
 }finally{$stream.Dispose()}
 # PowerShell 7.5+ otherwise converts ISO strings to DateTime automatically;
 # retain the JSON types required by the schema, as Windows PowerShell 5.1 does.
 $jsonArguments=@{InputObject=$text;ErrorAction='Stop'}
 if((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')){$jsonArguments.DateKind='String'}
 $value=ConvertFrom-Json @jsonArguments
 # ConvertFrom-Json silently accepts some duplicate keys; reject every object
 # collision explicitly, including escaped keys and case collisions.
 $stack=[Collections.Generic.Stack[object]]::new()
 foreach($token in [regex]::Matches($text,'"(?:[^"\\]|\\.)*"|[{}\[\]:,]|[^\s{}\[\]:,]+')){
  $part=$token.Value
  if($part -eq '{'){$stack.Push([PSCustomObject]@{object=$true;key=$true;names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)})}
  elseif($part -eq '['){$stack.Push([PSCustomObject]@{object=$false})}
  elseif($part -eq '}' -or $part -eq ']'){[void]$stack.Pop()}
  elseif($stack.Count -and $stack.Peek().object){$frame=$stack.Peek();if($part -eq ','){$frame.key=$true}elseif($part.StartsWith('"') -and $frame.key){$name=ConvertFrom-Json -InputObject ('['+$part+']');if(-not $frame.names.Add([string]$name)){throw "清单有重复 JSON 字段：$name"};$frame.key=$false}}
 }
 return [PSCustomObject]@{Value=$value;Hash=$hash}
}
function Read-UninstallManifest([string]$Path) {
 $snapshot=Read-UninstallJson $Path;$record=$snapshot.Value
 Assert-UninstallProperties $record @('schemaVersion','producer','sourceCommit','sourceTreeSha256','sourceDigestScope','sourceDigestExcludes','sourcesLockSha256','buildProfileSha256','build','artifacts','integrity')
 if(($record.schemaVersion -isnot [int] -and $record.schemaVersion -isnot [long]) -or $record.schemaVersion -ne 1 -or $record.producer -cne 'tools/Prepare-Release.ps1' -or $record.sourceCommit -isnot [string] -or $record.sourceCommit -cnotmatch '^[a-f0-9]{40,64}$'){throw '无法识别安装构建清单。'}
 foreach($field in @('sourceTreeSha256','sourcesLockSha256','buildProfileSha256')){Assert-UninstallHashValue $record.$field}
 if($record.sourceDigestScope -cne 'git-tracked-regular-files' -or $record.sourceDigestExcludes -isnot [array] -or $record.sourceDigestExcludes.Count -ne 1 -or $record.sourceDigestExcludes[0] -cne 'release-files.json' -or $record.integrity -isnot [string]){throw '构建清单的来源信息无效。'}
 Assert-UninstallProperties $record.build @('script','target','configuration','freshProducts','startedUtc','completedUtc')
 if($record.build.script -cne 'tools/Build.ps1' -or $record.build.target -cne 'All' -or $record.build.configuration -cne 'Release' -or $record.build.freshProducts -isnot [bool] -or -not $record.build.freshProducts){throw '构建清单的构建信息无效。'}
 foreach($field in @('startedUtc','completedUtc')){$parsed=[DateTimeOffset]::MinValue;if($record.build.$field -isnot [string] -or -not [DateTimeOffset]::TryParse($record.build.$field,[ref]$parsed)){throw '构建时间无效。'}}
 if($record.artifacts -isnot [array] -or $record.artifacts.Count -lt 5 -or $record.artifacts.Count -gt 256){throw '构建清单的文件数量无效。'}
 $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($artifact in $record.artifacts){
  Assert-UninstallProperties $artifact @('input','destination','sha256')
  if($artifact.input -isnot [string] -or $artifact.destination -isnot [string]){throw '清单路径必须是字符串。'}
  Assert-UninstallRelative $artifact.input;Assert-UninstallRelative $artifact.destination;Assert-UninstallHashValue $artifact.sha256
  if(-not (Test-UninstallDestination $artifact.destination)){throw "清单请求删除非 Mod 文件：$($artifact.destination)"}
  if(-not $seen.Add($artifact.destination)){throw "清单包含重复路径：$($artifact.destination)"}
 }
 foreach($required in @('ygopro-undo.exe','WindBot/WindBot-undo.exe','WindBot/WindBot-undo.exe.config','WindBot/undo-deps/x86/sqlite3.dll','WindBot/undo-deps/x64/sqlite3.dll')){if(-not $seen.Contains($required)){throw "构建清单缺少核心文件：$required"}}
 return $snapshot
}
function Read-UninstallLocalFix([string]$Path,[string]$Root,$Manifest) {
 $snapshot=Read-UninstallJson $Path;$fix=$snapshot.Value
 Assert-UninstallProperties $fix @('schemaVersion','kind','scope','installedAt','sourceCommit','workingTreeChanges','sources','baseCandidateCommit','runtimeRoot','client','rollback','preservedFiles','buildLog','evidence','note')
 if(($fix.schemaVersion -isnot [int] -and $fix.schemaVersion -isnot [long]) -or $fix.schemaVersion -ne 1 -or $fix.kind -cne 'local-incremental-development-fix' -or $fix.baseCandidateCommit -cne $Manifest.Value.sourceCommit -or $fix.sourceCommit -isnot [string] -or $fix.sourceCommit -cnotmatch '^[a-f0-9]{40,64}$' -or $fix.workingTreeChanges -isnot [bool]){throw '本地修复记录与基础构建不匹配。'}
 foreach($field in @('scope','installedAt','runtimeRoot','buildLog','evidence','note')){if($fix.$field -isnot [string] -or [string]::IsNullOrWhiteSpace($fix.$field)){throw "本地修复记录字段无效：$field"}}
 $installed=[DateTimeOffset]::MinValue;if(-not [DateTimeOffset]::TryParse($fix.installedAt,[ref]$installed)){throw '本地修复安装时间无效。'}
 if(-not [IO.Path]::IsPathRooted($fix.runtimeRoot) -or [IO.Path]::GetFullPath($fix.runtimeRoot).TrimEnd([char[]]@('\','/')) -ine $Root){throw '本地修复记录属于其他游戏目录。'}
 Assert-UninstallProperties $fix.client @('sha256','path');Assert-UninstallHashValue $fix.client.sha256
 if($fix.client.path -isnot [string] -or -not [IO.Path]::IsPathRooted($fix.client.path) -or [IO.Path]::GetFullPath($fix.client.path) -ine (Join-Path $Root 'ygopro-undo.exe')){throw '本地修复记录只能指向本目录的 ygopro-undo.exe。'}
 Assert-UninstallProperties $fix.rollback @('previousOverrideRecord','backup','sha256','note');Assert-UninstallHashValue $fix.rollback.sha256
 foreach($field in @('previousOverrideRecord','backup','note')){if($fix.rollback.$field -isnot [string]){throw '本地修复回退信息无效。'}}
 if($fix.sources -isnot [array] -or $fix.sources.Count -lt 1 -or $fix.sources.Count -gt 256 -or $fix.preservedFiles -isnot [array] -or $fix.preservedFiles.Count -lt 1 -or $fix.preservedFiles.Count -gt 256){throw '本地修复记录文件列表无效。'}
 $bound=$false
 foreach($list in @('sources','preservedFiles')){
  $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach($entry in $fix.$list){
   Assert-UninstallProperties $entry @('path','sha256');Assert-UninstallHashValue $entry.sha256
   if($entry.path -isnot [string]){throw '本地修复文件路径无效。'}
   $relative=$entry.path.Replace('\','/');Assert-UninstallRelative $relative
   if(-not $seen.Add($relative)){throw '本地修复记录包含重复路径。'}
   if($list -eq 'preservedFiles' -and $relative -ceq 'undo-mod/build-manifest.json'){if($entry.sha256 -ine $Manifest.Hash){throw '本地修复记录未绑定当前构建清单的准确 SHA256。'};$bound=$true}
  }
 }
 if(-not $bound){throw '本地修复记录缺少基础构建清单 SHA256。'}
 # rollback/sources/preservedFiles are metadata only. Never read, copy or delete
 # a path supplied there; only the already-fixed client hash is overridden.
 return $snapshot
}
function Assert-UninstallProcesses([string]$Root) {
 foreach($process in @(Get-Process -Name 'ygopro-undo','WindBot-undo','ygopro','Bot','WindBot' -ErrorAction SilentlyContinue)){
  $path=$null;try{$path=$process.Path}catch{}
  if(-not $path -or $path.StartsWith($Root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw "请先自行关闭游戏和 AI，再运行卸载。仍在运行：$($process.ProcessName) (PID $($process.Id))。不会自动结束进程。"}
 }
}
function Get-UninstallOwnedTree([string]$Root) {
 $items=[Collections.Generic.List[string]]::new()
 foreach($name in @('undo-mod','WindBot/undo-deps')){
  $path=Join-Path $Root $name;Assert-UninstallPath $path $Root
  if(-not (Test-Path -LiteralPath $path)){continue}
  if(-not (Test-Path -LiteralPath $path -PathType Container)){throw "Mod 路径不是目录：$path"}
  $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($path)
  while($pending.Count){
   foreach($entry in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){
    Assert-UninstallPath $entry.FullName $Root
    if($entry.PSIsContainer){$pending.Push($entry.FullName)}else{$items.Add($entry.FullName.Substring($Root.Length+1).Replace('\','/'))}
    if($items.Count+$pending.Count -gt 4096){throw 'Mod 目录包含过多未知文件，已停止卸载。'}
   }
  }
 }
 return $items.ToArray()
}
function New-UninstallResult([string]$Status,$Planned,$Removed,$Preserved,[string]$BackupPath='') {
 [PSCustomObject]@{Status=$Status;RuntimeRoot=$script:uninstallRoot;Planned=@($Planned);Removed=@($Removed);Preserved=@($Preserved);BackupPath=$BackupPath}
}
function Invoke-UndoUninstall {
 if([string]::IsNullOrWhiteSpace($RuntimeRoot)){
  if([IO.Path]::GetFileName($PSScriptRoot) -ine 'undo-mod'){throw '独立使用时请用 -RuntimeRoot 指定游戏目录；安装后可双击游戏目录内的 Uninstall-UndoMod.cmd。'}
  $root=[IO.Path]::GetDirectoryName($PSScriptRoot)
 }else{$root=[IO.Path]::GetFullPath($RuntimeRoot)}
 $root=$root.TrimEnd([char[]]@('\','/'));Assert-UninstallPath $root
 if(-not (Test-Path -LiteralPath $root -PathType Container)){throw '游戏目录不存在。'}
 $script:uninstallRoot=$root
 Write-Host "撤回 Mod 卸载工具`n游戏目录：$root"
 Write-Host '保留原版游戏、共享 DLL、卡片与卡组等资源，以及 system-undo.conf 和 undo-logs。'
 $manifestPath=Join-Path $root 'undo-mod/build-manifest.json';Assert-UninstallPath $manifestPath $root
 $ownedTree=@(Get-UninstallOwnedTree $root)
 if(-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)){
  $remaining=@($ownedTree)+@(foreach($relative in @('ygopro-undo.exe','WindBot/WindBot-undo.exe','WindBot/WindBot-undo.exe.config','Uninstall-UndoMod.cmd')){$path=Join-Path $root $relative;Assert-UninstallPath $path $root;if(Test-Path -LiteralPath $path){$relative}})
  if($remaining.Count){Write-Warning '没有可验证的构建清单；以下文件全部保留，未执行卸载。';$remaining|ForEach-Object {Write-Host "  保留：$_"};return (New-UninstallResult 'Partial' @() @() $remaining)}
  Write-Host '未发现已安装的撤回 Mod 文件，无需卸载。';return (New-UninstallResult 'AlreadyAbsent' @() @() @())
 }
 $manifest=Read-UninstallManifest $manifestPath
 $localFixPath=Join-Path $root 'undo-mod/local-fix.json';Assert-UninstallPath $localFixPath $root
 $localFix=$null;if(Test-Path -LiteralPath $localFixPath){$localFix=Read-UninstallLocalFix $localFixPath $root $manifest}
 $known=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 [void]$known.Add('undo-mod/build-manifest.json');if($localFix){[void]$known.Add('undo-mod/local-fix.json')}
 $candidates=[Collections.Generic.List[object]]::new();$preserved=[Collections.Generic.List[string]]::new()
 Assert-UninstallProcesses $root
 foreach($artifact in $manifest.Value.artifacts){
  $relative=$artifact.destination;[void]$known.Add($relative);$path=Join-Path $root $relative;Assert-UninstallPath $path $root
  if(-not (Test-Path -LiteralPath $path)){continue}
  if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "待卸载路径不是普通文件：$relative"}
  if((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReadOnly){throw "待卸载文件为只读，未删除任何文件：$relative"}
  # Exclusive read preflight detects existing readers/writers before any backup
  # or removal. The full batch is locked and checked again after confirmation.
  $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
  try{$actual=Get-UninstallStreamHash $stream}finally{$stream.Dispose()}
  $expected=$artifact.sha256;if($relative -ceq 'ygopro-undo.exe' -and $localFix){$expected=$localFix.Value.client.sha256}
  if($actual -ine $expected){$preserved.Add($relative);continue}
  $candidates.Add([PSCustomObject]@{Relative=$relative;Path=$path;Hash=$actual})
 }
 foreach($relative in $ownedTree){if(-not $known.Contains($relative)){$preserved.Add($relative)}}
 foreach($relative in @('Uninstall-UndoMod.cmd','undo-mod/Uninstall-UndoMod.ps1')){if(-not $known.Contains($relative) -and (Test-Path -LiteralPath (Join-Path $root $relative)) -and -not $preserved.Contains($relative)){$preserved.Add($relative)}}
 if($preserved.Count){
  # Keep a usable entry point for retrying an incomplete uninstall. The presence
  # of these retained tools never creates a partial result by itself.
  foreach($entry in @($candidates.ToArray())){if(@('Uninstall-UndoMod.cmd','undo-mod/Uninstall-UndoMod.ps1') -ccontains $entry.Relative){[void]$candidates.Remove($entry);$preserved.Add($entry.Relative)}}
 }
 if(-not $preserved.Count){
  if($localFix){$candidates.Add([PSCustomObject]@{Relative='undo-mod/local-fix.json';Path=$localFixPath;Hash=$localFix.Hash})}
  # Self-excluding manifest: remove only its fully validated snapshot, last.
  $candidates.Add([PSCustomObject]@{Relative='undo-mod/build-manifest.json';Path=$manifestPath;Hash=$manifest.Hash})
 }else{Write-Warning '部分文件已更改或不属于已验证的 Mod，以下文件及安装清单将保留。';foreach($relative in $preserved){Write-Host "  保留：$relative"}}
 $planned=@($candidates|ForEach-Object {$_.Relative})
 Write-Host "`n将先备份再删除的准确文件列表（$($planned.Count) 个）："
 foreach($relative in $planned){Write-Host "  $(Join-Path $root $relative)"}
 if($Preview){Write-Host '仅预览：未创建备份，未删除任何文件。';return (New-UninstallResult 'Preview' $planned @() $preserved.ToArray())}
 if(-not $planned.Count){Write-Warning '没有可安全删除的剩余文件；未完成卸载。';return (New-UninstallResult 'Partial' @() @() $preserved.ToArray())}
 if(-not $ConfirmRemoval){$answer=Read-Host '确认备份并删除以上文件？输入 YES 确认，直接回车取消';if($answer -cne 'YES'){Write-Host '已取消，未更改任何文件。';return (New-UninstallResult 'Cancelled' $planned @() $preserved.ToArray())}}
 Assert-UninstallProcesses $root
 $backupBase=Join-Path $root 'undo-mod-backups';Assert-UninstallPath $backupBase $root
 if((Test-Path -LiteralPath $backupBase) -and -not (Test-Path -LiteralPath $backupBase -PathType Container)){throw '备份目录被其他文件占用；未删除任何文件。'}
 $backup=Join-Path $backupBase ((Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
 Assert-UninstallPath $backup $root
 Initialize-UninstallNative
 $handles=[Collections.Generic.List[object]]::new();$removed=[Collections.Generic.List[string]]::new()
 try{
  # Bind metadata too on partial removal, to catch a concurrent change to the
  # authority between preview and confirmation without losing the retry record.
  $lockEntries=@($candidates.ToArray())
  if($preserved.Count){$lockEntries+=@([PSCustomObject]@{Relative='undo-mod/build-manifest.json';Path=$manifestPath;Hash=$manifest.Hash});if($localFix){$lockEntries+=@([PSCustomObject]@{Relative='undo-mod/local-fix.json';Path=$localFixPath;Hash=$localFix.Hash})}}
  foreach($entry in $lockEntries){
   Assert-UninstallPath $entry.Path $root
   if((Get-Item -LiteralPath $entry.Path -Force).Attributes -band [IO.FileAttributes]::ReadOnly){throw "文件为只读；未删除任何文件：$($entry.Relative)"}
   $stream=[UndoModFileRemoval]::Open($entry.Path)
   $handles.Add([PSCustomObject]@{Entry=$entry;Stream=$stream})
   if((Get-UninstallStreamHash $stream) -ine $entry.Hash){throw "确认后文件已变化；未删除任何文件：$($entry.Relative)"}
  }
  # No directory is created until all process/file/metadata checks have passed.
  foreach($entry in $candidates){
   $destination=Join-Path $backup ('files/'+$entry.Relative);Assert-UninstallPath $destination $root
   [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))|Out-Null;Assert-UninstallPath $destination $root
   $source=@($handles|Where-Object {$_.Entry.Relative -ceq $entry.Relative})[0].Stream;$source.Position=0
   $output=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
   try{$source.CopyTo($output);$output.Flush($true)}finally{$output.Dispose()}
   $verify=[IO.File]::Open($destination,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
   try{if((Get-UninstallStreamHash $verify) -ine $entry.Hash){throw '备份校验失败；未删除任何文件。'}}finally{$verify.Dispose()}
  }
  $receiptPath=Join-Path $backup 'uninstall-receipt.json';Assert-UninstallPath $receiptPath $root
  $receipt=[ordered]@{schemaVersion=1;kind='undo-mod-uninstall-backup';runtimeRoot=$root;createdUtc=[DateTime]::UtcNow.ToString('o');files=@($candidates|ForEach-Object {[ordered]@{destination=$_.Relative;sha256=$_.Hash}});preserved=$preserved.ToArray();note='Exact original payload is under files/. Close the game before manually restoring individual files to the recorded runtimeRoot. This is a recovery backup, not a release or signature.'}
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($receipt|ConvertTo-Json -Depth 8));$output=[IO.File]::Open($receiptPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$output.Write($bytes,0,$bytes.Length);$output.Flush($true)}finally{$output.Dispose()}
  # Keep every other file locked until its own final check and deletion. There
  # is no recursive delete, and metadata is always the final removal candidate.
  foreach($entry in $candidates){
   Assert-UninstallPath $entry.Path $root
   $held=@($handles|Where-Object {$_.Entry.Relative -ceq $entry.Relative})[0]
   if((Get-UninstallStreamHash $held.Stream) -ine $entry.Hash){throw "删除前文件已变化：$($entry.Relative)"}
   [UndoModFileRemoval]::MarkForDeletion($held.Stream)
   $held.Stream.Dispose();$held.Stream=$null;$removed.Add($entry.Relative)
  }
 }catch{
  if($removed.Count){throw "卸载仅部分完成（已移除 $($removed.Count) 个文件），请勿当作成功。恢复备份：$backup。原因：$($_.Exception.Message)"}
  throw "卸载已停止，未删除任何文件。原因：$($_.Exception.Message)"
 }finally{foreach($held in $handles){if($held.Stream){$held.Stream.Dispose()}}}
 if($preserved.Count){Write-Warning "仅移除已验证文件，卸载尚未完成。保留文件见上方列表；备份：$backup";return (New-UninstallResult 'Partial' $planned $removed.ToArray() $preserved.ToArray() $backup)}
 Write-Host "撤回 Mod 文件已卸载。配置和日志已保留。恢复备份：$backup"
 return (New-UninstallResult 'Complete' $planned $removed.ToArray() @() $backup)
}
if($Interactive){
 try{$result=Invoke-UndoUninstall;if($result.Status -eq 'Partial'){exit 2};exit 0}
 catch{Write-Host "卸载已停止：$($_.Exception.Message)" -ForegroundColor Yellow;Write-Host '请先关闭游戏和 AI，保留现有文件与备份，再重新运行卸载工具。';exit 1}
}else{Invoke-UndoUninstall}
