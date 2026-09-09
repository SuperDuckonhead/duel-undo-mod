param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixture=Join-Path $repo ('out/tests/PatchCheckoutRegression/'+[guid]::NewGuid().ToString('N'))
$origin=Join-Path $fixture 'origin'
[IO.Directory]::CreateDirectory($origin)|Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
function Git([string]$Root,[string[]]$Arguments) {
    $lines=@(& git.exe -C $Root @Arguments 2>&1)
    if($LASTEXITCODE -ne 0){throw "Fixture git failed: $($Arguments -join ' ')`n$($lines -join "`n")"}
}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
Git $origin @('init','-q')
Git $origin @('config','core.autocrlf','false')
$groups=@(
    @{name='lua-undo';helper='Apply-LuaUndoPatch.ps1';parameter='LuaRoot';vendor='client/lua'},
    @{name='irrlicht-undo';helper='Apply-IrrlichtUndoPatch.ps1';parameter='IrrlichtRoot';vendor='client/irrlicht'}
)
$originalHashes=@{}
foreach($group in $groups){
    $patchDir=Join-Path $origin ('tools/patches/'+$group.name)
    [IO.Directory]::CreateDirectory($patchDir)|Out-Null
    Copy-Item -LiteralPath (Join-Path $repo ('tools/'+$group.helper)) -Destination (Join-Path $origin ('tools/'+$group.helper))
    Copy-Item -LiteralPath (Join-Path $repo ('tools/patches/'+$group.name+'/manifest.json')) -Destination (Join-Path $patchDir 'manifest.json')
    $group.entries=Get-Content -LiteralPath (Join-Path $patchDir 'manifest.json') -Raw|ConvertFrom-Json
    foreach($entry in $group.entries){Copy-Item -LiteralPath (Join-Path $repo ('tools/patches/'+$group.name+'/'+$entry.patch)) -Destination (Join-Path $patchDir $entry.patch)}
}
if(Test-Path -LiteralPath (Join-Path $repo '.gitattributes')){Copy-Item -LiteralPath (Join-Path $repo '.gitattributes') -Destination (Join-Path $origin '.gitattributes')}
Git $origin @('add','.')
Git $origin @('-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit','-qm','Patch checkout regression fixture')
# Reconstruct pristine sources only in the isolated fixture. The real source
# may already have the patch; pinned before/after hashes guard both cases.
foreach($group in $groups){
    $pristine='out/pristine/'+$group.name
    $group.pristine=$pristine
    foreach($entry in $group.entries){
        $source=Join-Path $repo ($group.vendor+'/'+$entry.file)
        $before=Hash $source;$originalHashes[$source]=$before
        if($before -ne $entry.before -and $before -ne $entry.after){throw "Unexpected original source: $source"}
        $copy=Join-Path $origin ($pristine+'/'+$entry.file)
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($copy))|Out-Null
        Copy-Item -LiteralPath $source -Destination $copy
        if($before -eq $entry.after){Git $origin @('-c','core.autocrlf=false','apply','--reverse',"--directory=$pristine",'--',('tools/patches/'+$group.name+'/'+$entry.patch))}
        if((Hash $copy) -ne $entry.before){throw "Pristine reconstruction mismatch: $copy"}
    }
}
$clone=Join-Path $fixture 'autocrlf-true'
Git $fixture @('clone','-q','--config','core.autocrlf=true',$origin,$clone)
$checks=0
foreach($location in @('default','out')){
    foreach($group in $groups){
        $target=if($location -eq 'default'){$group.vendor}else{'out/custom/'+$group.name}
        foreach($entry in $group.entries){
            $copy=Join-Path $clone ($target+'/'+$entry.file)
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($copy))|Out-Null
            Copy-Item -LiteralPath (Join-Path $origin ($group.pristine+'/'+$entry.file)) -Destination $copy
        }
        for($pass=1;$pass -le 2;$pass++){
            $args=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $clone ('tools/'+$group.helper)),('-'+$group.parameter),$target)
            $lines=@(& powershell.exe @args 2>&1)
            if($LASTEXITCODE -ne 0){throw "Actual helper failed ($location/$($group.name)/pass$pass):`n$($lines -join "`n")"}
            foreach($entry in $group.entries){if((Hash (Join-Path $clone ($target+'/'+$entry.file))) -ne $entry.after){throw "After hash mismatch: $target/$($entry.file)"};$checks++}
        }
        Write-Output "PASS $location/$($group.name): actual apply and idempotence, pinned output hashes"
    }
}
foreach($group in $groups){foreach($entry in $group.entries){
    $relative='tools/patches/'+$group.name+'/'+$entry.patch
    if((Hash (Join-Path $clone $relative)) -ne (Hash (Join-Path $origin $relative))){throw "Patch checkout changed blob bytes: $relative"}
    $checks++
}}
foreach($source in $originalHashes.Keys){if((Hash $source) -ne $originalHashes[$source]){throw "Original source changed: $source"};$checks++}
Write-Output "PASS $checks hash checks; core.autocrlf=true; default/custom out targets; exact Lua LF and Irrlicht mixed patch bytes. Fixture: $fixture"