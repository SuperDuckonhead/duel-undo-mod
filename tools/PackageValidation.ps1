# Shared, side-effect-free validation for the layout checker and ZIP builder.
function Get-PackageFullPath([string]$Path,[string]$Base) {
    if([string]::IsNullOrWhiteSpace($Path)){throw 'Empty package path'}
    if($Path.Contains(':') -and $Path -notmatch '^[A-Za-z]:[\\/][^:]*$'){throw 'Alternate data streams are not package paths'}
    if([IO.Path]::IsPathRooted($Path)){return [IO.Path]::GetFullPath($Path)}
    return [IO.Path]::GetFullPath((Join-Path $Base $Path))
}
function Assert-PackagePath([string]$Path,[string]$Root='') {
    $full=[IO.Path]::GetFullPath($Path)
    if($full.Substring([IO.Path]::GetPathRoot($full).Length).Contains(':')){throw 'Alternate data streams are not package paths'}
    if($Root){
        $base=[IO.Path]::GetFullPath($Root).TrimEnd([char[]]@('\','/'))
        if($full -ne $base -and -not $full.StartsWith($base+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw "Package path escapes its fixed root: $full"}
    }
    $cursor=$full
    while($cursor){
        $entry=$null
        try{$entry=Get-Item -LiteralPath $cursor -Force -ErrorAction Stop}catch [System.Management.Automation.ItemNotFoundException]{}
        if($entry -and ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Package path contains a reparse point: $cursor"}
        if($entry -and $cursor -ne $full -and -not $entry.PSIsContainer){throw "Package path ancestor is not a directory: $cursor"}
        $parent=[IO.Path]::GetDirectoryName($cursor)
        if($parent -eq $cursor){break}
        $cursor=$parent
    }
}
function Assert-PackageRelative([string]$Path) {
    if([string]::IsNullOrWhiteSpace($Path) -or $Path.Length -gt 240 -or $Path.Contains('\') -or [IO.Path]::IsPathRooted($Path)){throw "Invalid relative package path: $Path"}
    foreach($part in $Path.Split('/')){
        if($part -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or $part.EndsWith('.') -or $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])([.]|$)'){throw "Invalid relative package component: $Path"}
    }
}
function Assert-PackageDestination([string]$Path) {
    Assert-PackageRelative $Path
    $fixed=@('ygopro-undo.exe','WindBot/WindBot-undo.exe','WindBot/WindBot-undo.exe.config','WindBot/undo-deps/x86/sqlite3.dll','WindBot/undo-deps/x64/sqlite3.dll',
        'Uninstall-UndoMod.cmd','undo-mod/Uninstall-UndoMod.ps1',
        'undo-mod/BUILD.md','undo-mod/INSTALL.md','undo-mod/COMPATIBILITY.md','undo-mod/RELEASE-NOTES.md','undo-mod/THIRD-PARTY.md','undo-mod/build-manifest.json','undo-mod/release-files.json')
    if($fixed -cnotcontains $Path -and $Path -cnotmatch '^undo-mod/licenses/[A-Za-z0-9][A-Za-z0-9._-]{0,63}[.]txt$'){throw "Destination is not an allowed mod file: $Path"}
}
function Assert-PackageProperties($Value,[string[]]$Expected) {
    if($null -eq $Value -or $Value -isnot [PSCustomObject]){throw 'Expected a JSON object'}
    $names=@($Value.PSObject.Properties.Name)
    if($names.Count -ne $Expected.Count){throw 'Unexpected JSON object fields'}
    foreach($name in $names){if($Expected -cnotcontains $name){throw "Unexpected JSON object field: $name"}}
}
function Read-PackageJson([string]$Path,[string]$TrustedSha256='') {
    Assert-PackagePath $Path
    # One read-only handle binds the verified receipt bytes to the parsed object.
    # FileShare.Read denies concurrent writers/deletion while the snapshot is read.
    $inputStream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
        if($inputStream.Length -gt 4MB){throw 'Invalid package JSON file size/type'}
        if($TrustedSha256){
            $sha=[Security.Cryptography.SHA256]::Create()
            try{$actual=([BitConverter]::ToString($sha.ComputeHash($inputStream))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
            if($actual -ine $TrustedSha256){throw 'Trusted receipt hash mismatch'}
            $inputStream.Position=0
        }
        $reader=[IO.StreamReader]::new($inputStream,[Text.Encoding]::UTF8,$true,4096,$true)
        try{$text=$reader.ReadToEnd()}finally{$reader.Dispose()}
    }finally{$inputStream.Dispose()}
    $value=$text | ConvertFrom-Json -ErrorAction Stop
    # ConvertFrom-Json can silently keep the final duplicate key. Scan the valid
    # JSON tokens too, preserving case-insensitive Windows field collisions.
    $stack=[Collections.Generic.Stack[object]]::new()
    foreach($token in [regex]::Matches($text,'"(?:[^"\\]|\\.)*"|[{}\[\]:,]|[^\s{}\[\]:,]+')){
        $part=$token.Value
        if($part -eq '{'){$stack.Push([PSCustomObject]@{object=$true;key=$true;names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)})}
        elseif($part -eq '['){$stack.Push([PSCustomObject]@{object=$false})}
        elseif($part -eq '}' -or $part -eq ']'){[void]$stack.Pop()}
        elseif($stack.Count -and $stack.Peek().object){
            $frame=$stack.Peek()
            if($part -eq ','){$frame.key=$true}
            elseif($part.StartsWith('"') -and $frame.key){
                $name=ConvertFrom-Json -InputObject ('['+$part+']')
                if(-not $frame.names.Add([string]$name)){throw "Duplicate JSON object field: $name"}
                $frame.key=$false
            }
        }
    }
    return $value
}
function Assert-PackageHash([string]$Path,[string]$Expected) {
    $actual=(Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
    if($actual -cne $Expected.ToLowerInvariant()){throw "Package artifact hash mismatch: $Path"}
}
function Read-PackageManifest([string]$Manifest,[string]$RepoRoot) {
    $path=Get-PackageFullPath $Manifest $RepoRoot
    $data=Read-PackageJson $path
    Assert-PackageProperties $data @('schemaVersion','files')
    if(($data.schemaVersion -isnot [int] -and $data.schemaVersion -isnot [long]) -or $data.schemaVersion -ne 1 -or $data.files -isnot [array] -or $data.files.Count -lt 1 -or $data.files.Count -gt 256){throw 'Invalid package manifest schema/version/file count'}
    $release=Join-Path $RepoRoot 'out/release'
    Assert-PackagePath $release (Join-Path $RepoRoot 'out')
    $destinations=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $sources=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $entries=@(foreach($entry in $data.files){
        Assert-PackageProperties $entry @('source','destination','sha256','license')
        foreach($field in @('source','destination','sha256','license')){if($entry.$field -isnot [string]){throw "Package entry field must be a string: $field"}}
        Assert-PackageDestination $entry.destination
        Assert-PackageRelative $entry.source
        if(-not $destinations.Add($entry.destination) -or -not $sources.Add($entry.source)){throw 'Duplicate package source/destination (including case collision)'}
        if($entry.source -cne $entry.destination -and -not $entry.source.EndsWith('/'+$entry.destination,[StringComparison]::Ordinal)){throw 'Package source must preserve the allowed destination suffix'}
        if($entry.sha256 -cnotmatch '^[a-fA-F0-9]{64}$' -or [string]::IsNullOrWhiteSpace($entry.license) -or $entry.license.Length -gt 512 -or $entry.license -match '[\x00-\x1f]'){throw 'Invalid package sha256/license'}
        $source=Join-Path $release $entry.source
        Assert-PackagePath $source $release
        if(-not (Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing package source file: $source"}
        Assert-PackageHash $source $entry.sha256
        [PSCustomObject]@{source=$entry.source;destination=$entry.destination;sha256=$entry.sha256.ToLowerInvariant();license=$entry.license;SourcePath=$source;Length=(Get-Item -LiteralPath $source).Length}
    })
    return [PSCustomObject]@{Manifest=$path;ReleaseRoot=$release;Files=$entries}
}
function Read-PackageReceipt([string]$Path,[string]$TrustedSha256,[string]$RepoRoot) {
    if(-not $Path -and -not $TrustedSha256){return $null}
    if(-not $Path -or $TrustedSha256 -cnotmatch '^[a-fA-F0-9]{64}$'){throw 'Trusted receipt and its independently trusted SHA-256 are required together'}
    $full=Get-PackageFullPath $Path $RepoRoot
    Assert-PackagePath $full
    $receipt=Read-PackageJson $full $TrustedSha256
    Assert-PackageProperties $receipt @('schemaVersion','modId','files')
    if(($receipt.schemaVersion -isnot [int] -and $receipt.schemaVersion -isnot [long]) -or $receipt.schemaVersion -ne 1 -or $receipt.modId -cne 'ygopro-undo' -or $receipt.files -isnot [array] -or $receipt.files.Count -lt 1 -or $receipt.files.Count -gt 256){throw 'Invalid managed mod receipt'}
    $owned=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($file in $receipt.files){
        Assert-PackageProperties $file @('destination','sha256')
        if($file.destination -isnot [string] -or $file.sha256 -isnot [string] -or $file.sha256 -cnotmatch '^[a-fA-F0-9]{64}$'){throw 'Invalid managed receipt file'}
        Assert-PackageDestination $file.destination
        if($owned.ContainsKey($file.destination)){throw 'Duplicate managed receipt destination'}
        $owned.Add($file.destination,$file.sha256.ToLowerInvariant())
    }
    return ,$owned
}
function Assert-PackageArchive([string]$Path,$Files) {
    $archive=[IO.Compression.ZipFile]::OpenRead($Path)
    try{
        if($archive.Entries.Count -ne $Files.Count){throw 'ZIP entry count differs from manifest'}
        $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach($member in $archive.Entries){
            if(-not $seen.Add($member.FullName)){throw 'Duplicate ZIP entry'}
            $expected=@($Files | Where-Object {$_.destination -ceq $member.FullName})
            if($expected.Count -ne 1 -or $member.Length -ne $expected[0].Length){throw 'ZIP entry differs from manifest'}
            $stream=$member.Open();$sha=[Security.Cryptography.SHA256]::Create()
            try{$digest=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose();$stream.Dispose()}
            if($digest -cne $expected[0].sha256){throw 'ZIP content hash differs from manifest'}
        }
    }finally{$archive.Dispose()}
}
