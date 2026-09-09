param(
    [string]$Manifest='release-files.json',
    [string]$OutFile='out/packages/ygopro-undo-candidate.zip'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'PackageValidation.ps1')
$packages=Join-Path $repo 'out/packages'
$staging=Join-Path $repo 'out/staging'
$output=Get-PackageFullPath $OutFile $repo
Assert-PackagePath $output $packages
Assert-PackagePath $staging (Join-Path $repo 'out')
if([IO.Path]::GetExtension($output) -cne '.zip'){throw 'Package output must end in .zip'}
if(Test-Path -LiteralPath $output){throw 'Package output already exists; overwriting is not supported'}
# Validate every source, hash and destination before creating staging or copying.
$package=Read-PackageManifest $Manifest $repo
$stage=Join-Path $staging ([guid]::NewGuid().ToString('N'))
Assert-PackagePath $stage $staging
if(Test-Path -LiteralPath $stage){throw 'Staging GUID already exists'}
[IO.Directory]::CreateDirectory($stage) | Out-Null
$payload=Join-Path $stage 'files'
foreach($entry in $package.Files){
    Assert-PackagePath $entry.SourcePath $package.ReleaseRoot
    $target=Join-Path $payload $entry.destination
    Assert-PackagePath $target $stage
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    [IO.File]::Copy($entry.SourcePath,$target,$false)
    Assert-PackageHash $target $entry.sha256
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporary=Join-Path $stage 'candidate.zip'
Assert-PackagePath $temporary $stage
$stream=[IO.FileStream]::new($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try{
    $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true)
    try{
        foreach($entry in $package.Files){
            $source=Join-Path $payload $entry.destination
            Assert-PackagePath $source $stage
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$source,$entry.destination,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    }finally{$zip.Dispose()}
    $stream.Flush($true)
}finally{$stream.Dispose()}
Assert-PackageArchive $temporary $package.Files
Assert-PackagePath $output $packages
if(Test-Path -LiteralPath $output){throw 'Package output already exists; overwriting is not supported'}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output)) | Out-Null
# Move to an absent destination only; no force and no recursive staging cleanup.
[IO.File]::Move($temporary,$output)
[PSCustomObject]@{Package=$output;Sha256=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant();FileCount=$package.Files.Count;Staging=$stage;Files=@($package.Files | Select-Object destination,sha256,license)}
