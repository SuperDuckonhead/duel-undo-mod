[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Path)

$ErrorActionPreference = 'Stop'

function Assert-AbsoluteHttpUrl {
    param([string]$Value, [string]$Field, [string]$ComponentName)
    $uri = $null
    if (-not [Uri]::TryCreate($Value, [UriKind]::Absolute, [ref]$uri) -or
        ($uri.Scheme -ne 'http' -and $uri.Scheme -ne 'https')) {
        throw "Component '$ComponentName' has invalid $Field; an absolute HTTP(S) URL is required"
    }
}

function Assert-SafeDestination {
    param([string]$Destination, [string]$ComponentName)

    if ([IO.Path]::IsPathRooted($Destination) -or $Destination -match '^[A-Za-z]:') {
        throw "Component '$ComponentName' has an absolute or drive-relative destination"
    }
    if ($Destination -match '\\' -or $Destination.StartsWith('/') -or $Destination.EndsWith('/')) {
        throw "Component '$ComponentName' has a malformed destination"
    }
    $segments = $Destination -split '/'
    if ($segments.Count -eq 0 -or @($segments | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' }).Count -ne 0) {
        throw "Component '$ComponentName' has an unsafe destination"
    }
    foreach ($segment in $segments) {
        if ($segment.EndsWith('.') -or $segment.EndsWith(' ')) {
            throw "Component '$ComponentName' has a destination segment ending in a dot or space"
        }
        $deviceBaseName = ($segment -split '\.', 2)[0]
        if ($deviceBaseName -imatch '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]|CONIN\$|CONOUT\$)$') {
            throw "Component '$ComponentName' uses a reserved Windows device name"
        }
        if ($segment.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
            throw "Component '$ComponentName' has invalid destination characters"
        }
    }
}

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Source lock does not exist: $Path" }
try {
    $lock = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
} catch {
    throw "Invalid source lock JSON: $($_.Exception.Message)"
}
if ($null -eq $lock -or $lock -is [System.Array]) { throw 'Source lock root must be an object' }
if ($null -eq $lock.components -or -not ($lock.components -is [System.Array]) -or $lock.components.Count -eq 0) {
    throw 'Source lock components must be a non-empty array'
}

$destinations = @{}
foreach ($item in $lock.components) {
    if ($null -eq $item -or $item -isnot [psobject] -or $item -is [string]) { throw 'Each component must be an object' }
    foreach ($key in @('name', 'url', 'commit', 'evidenceUrl', 'evidenceNote', 'destination')) {
        $property = $item.PSObject.Properties[$key]
        if ($null -eq $property -or $property.Value -isnot [string] -or [string]::IsNullOrWhiteSpace($property.Value)) {
            throw "Component entry has missing or invalid $key"
        }
    }

    if ($item.commit -cnotmatch '^[0-9a-f]{40}$') { throw "Component '$($item.name)' requires a full lowercase 40-character Git SHA" }
    Assert-AbsoluteHttpUrl -Value $item.url -Field 'url' -ComponentName $item.name
    Assert-AbsoluteHttpUrl -Value $item.evidenceUrl -Field 'evidenceUrl' -ComponentName $item.name
    Assert-SafeDestination -Destination $item.destination -ComponentName $item.name

    $destinationKey = $item.destination.ToLowerInvariant()
    if ($destinations.ContainsKey($destinationKey)) { throw "Duplicate destination: $($item.destination)" }
    $destinations[$destinationKey] = $true
}

Write-Output "Validated $($lock.components.Count) source lock component(s)."
