param([Parameter(Mandatory=$true)][string]$VerifiedResults)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repo 'tools/PackageValidation.ps1')
$fixed=Join-Path $repo 'out/tests/W3List'
$verified=Get-PackageFullPath $VerifiedResults $repo
Assert-PackagePath $verified $fixed
$driver=Join-Path $repo 'tools/Test-AiListPreflight.ps1'
& $driver -CheckOnly -ResultsDirectory $verified
$root=Join-Path $fixed ('coverage-guards/'+[guid]::NewGuid().ToString('N'))
Assert-PackagePath $root $fixed
[IO.Directory]::CreateDirectory($root)|Out-Null
function Fixture([string]$Name) {
 $path=Join-Path $root $Name
 [IO.Directory]::CreateDirectory($path)|Out-Null
 foreach($file in @('results.csv','provenance.txt')){Copy-Item -LiteralPath (Join-Path $verified $file) -Destination (Join-Path $path $file)}
 return $path
}
function Rehash([string]$Path) {
 $proof=Join-Path $Path 'provenance.txt'
 $text=[IO.File]::ReadAllText($proof)
 $hash=(Get-FileHash -LiteralPath (Join-Path $Path 'results.csv') -Algorithm SHA256).Hash.ToLowerInvariant()
 [IO.File]::WriteAllText($proof,([regex]::Replace($text,'(?m)^resultsSha256=[a-f0-9]+','resultsSha256='+$hash)),[Text.UTF8Encoding]::new($false))
}
function Reject([string]$Path,[string]$Message) {
 try{& $driver -CheckOnly -ResultsDirectory $Path;throw 'Unexpected coverage acceptance'}catch{
  if($_.Exception.Message -notlike $Message){throw}
  Write-Output ('PASS '+[IO.Path]::GetFileName($Path)+': '+$_.Exception.Message)
 }
}
$path=Fixture 'missing-row'
$rows=@(Import-Csv -LiteralPath (Join-Path $path 'results.csv') -Encoding UTF8)
$rows[0..($rows.Count-2)] | Export-Csv -LiteralPath (Join-Path $path 'results.csv') -NoTypeInformation -Encoding UTF8
Rehash $path
Reject $path '*row count differs*'
$path=Fixture 'duplicate-identity'
$rows=@(Import-Csv -LiteralPath (Join-Path $path 'results.csv') -Encoding UTF8)
$rows[1]=$rows[0]
$rows | Export-Csv -LiteralPath (Join-Path $path 'results.csv') -NoTypeInformation -Encoding UTF8
Rehash $path
Reject $path '*identity mismatch*'
$path=Fixture 'false-full-match'
$rows=@(Import-Csv -LiteralPath (Join-Path $path 'results.csv') -Encoding UTF8)
$rows[0].fullMatch='pass'
$rows | Export-Csv -LiteralPath (Join-Path $path 'results.csv') -NoTypeInformation -Encoding UTF8
Rehash $path
Reject $path '*failed/unverified row*'
$path=Fixture 'changed-bytes'
[IO.File]::AppendAllText((Join-Path $path 'results.csv'),[Environment]::NewLine)
Reject $path '*result bytes changed*'
$path=Fixture 'missing-completion'
$proof=Join-Path $path 'provenance.txt'
[IO.File]::WriteAllText($proof,([IO.File]::ReadAllText($proof).Replace('nativeRunExit=0','nativeRunExit=1')),[Text.UTF8Encoding]::new($false))
Reject $path '*lacks completed list*'
Write-Output ('5 coverage rejection fixtures passed; retained at '+$root)