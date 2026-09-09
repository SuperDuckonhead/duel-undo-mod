# Shared by the real benchmark runner and its filesystem/provenance regressions.
function Assert-MeasurementOutputPath([string]$Path,[string]$Root) {
    $full=[IO.Path]::GetFullPath($Path)
    $allowed=[IO.Path]::GetFullPath($Root).TrimEnd([char[]]@('\','/'))
    if($full -ne $allowed -and -not $full.StartsWith($allowed+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw "Measurement output escapes its root: $full"
    }
    # Walk every existing component, including Root and the requested leaf.
    # Get-Item exposes a junction itself even when its destination is absent.
    $cursor=$full
    while($cursor) {
        $entry=$null
        try { $entry=Get-Item -LiteralPath $cursor -Force -ErrorAction Stop }
        catch [System.Management.Automation.ItemNotFoundException] { }
        if($entry -and ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Measurement output contains a reparse point: $cursor"
        }
        $parent=[IO.Path]::GetDirectoryName($cursor)
        if($parent -eq $cursor){break}
        $cursor=$parent
    }
}
function Assert-MeasurementOutputTree([string]$Path,[string]$Root) {
    Assert-MeasurementOutputPath $Path $Root
    if(-not (Test-Path -LiteralPath $Path -PathType Container)){return}
    $pending=[Collections.Generic.Stack[string]]::new();$pending.Push([IO.Path]::GetFullPath($Path))
    while($pending.Count) {
        foreach($entry in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Measurement build contains a reparse point: $($entry.FullName)"}
            if($entry.PSIsContainer){$pending.Push($entry.FullName)}
        }
    }
}
function Get-MeasurementManifest([string[]]$Directories,[string[]]$Files,[string]$Root) {
    $base=[IO.Path]::GetFullPath($Root).TrimEnd([char[]]@('\','/'))
    $paths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($file in $Files){Assert-MeasurementOutputPath $file $base;[void]$paths.Add([IO.Path]::GetFullPath($file))}
    $pending=[Collections.Generic.Stack[string]]::new()
    foreach($directory in $Directories){Assert-MeasurementOutputPath $directory $base;$pending.Push([IO.Path]::GetFullPath($directory))}
    while($pending.Count){
        $dir=$pending.Pop()
        $entry=Get-Item -LiteralPath $dir -Force
        if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Measurement input is a reparse point: $dir"}
        foreach($child in Get-ChildItem -LiteralPath $dir -Force){
            if($child.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Measurement input is a reparse point: $($child.FullName)"}
            if($child.PSIsContainer){$pending.Push($child.FullName)}else{[void]$paths.Add($child.FullName)}
        }
    }
    $records=@($paths | Sort-Object | ForEach-Object {
        $entry=Get-Item -LiteralPath $_ -Force
        if($entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Invalid measurement input: $_"}
        if(-not $_.StartsWith($base+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw "Untracked measurement input outside checkout: $_"}
        [PSCustomObject]@{path=$_.Substring($base.Length+1).Replace('\','/');bytes=$entry.Length;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    $canonical=($records | ForEach-Object {$_.path+' '+$_.bytes+' '+$_.sha256}) -join "`n"
    $sha=[Security.Cryptography.SHA256]::Create()
    try{$digest=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical)))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
    return [PSCustomObject]@{sha256=$digest;files=$records}
}
