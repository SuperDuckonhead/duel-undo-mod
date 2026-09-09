param(
    [string]$Manifest='release-files.json',
    [Parameter(Mandatory=$true)][string]$RuntimeRoot,
    [string]$TrustedReceipt='',
    [string]$TrustedReceiptSha256=''
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'PackageValidation.ps1')
$package=Read-PackageManifest $Manifest $repo
$runtime=Get-PackageFullPath $RuntimeRoot $repo
Assert-PackagePath $runtime
if(-not (Test-Path -LiteralPath $runtime -PathType Container)){throw 'Runtime root must be an existing directory; layout checking never creates it'}
# The digest must come from a trusted previous release, not from hashing an
# arbitrary mutable receipt in RuntimeRoot and trusting that self-assertion.
$owned=Read-PackageReceipt $TrustedReceipt $TrustedReceiptSha256 $repo
$files=@(foreach($entry in $package.Files){
    $target=Join-Path $runtime $entry.destination
    Assert-PackagePath $target $runtime
    $disposition='new'
    if(Test-Path -LiteralPath $target){
        if(-not (Test-Path -LiteralPath $target -PathType Leaf)){throw "Existing package destination is not a file: $target"}
        $hash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
        if($null -eq $owned -or -not $owned.ContainsKey($entry.destination) -or $hash -cne $owned[$entry.destination]){throw "Existing destination lacks verified managed ownership: $target"}
        $disposition=if($hash -ceq $entry.sha256){'managed-identical'}else{'managed-update'}
    }
    [PSCustomObject]@{Destination=$entry.destination;Sha256=$entry.sha256;License=$entry.license;Disposition=$disposition}
})
[PSCustomObject]@{RuntimeRoot=$runtime;Manifest=$package.Manifest;ReadOnly=$true;Files=$files}
