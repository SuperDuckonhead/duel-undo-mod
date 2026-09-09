$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$checker=Join-Path $root 'tools/Validate-Acceptance.ps1'
$stage=Join-Path $root 'out/acceptance-checks'
[IO.Directory]::CreateDirectory($stage) | Out-Null
$original=@(Import-Csv -LiteralPath (Join-Path $root 'tests/acceptance.csv'))
$script:checks=0
function Check([string]$Name,[object[]]$Rows,[bool]$Expected,[bool]$Schema=$false) {
    $relative='out/acceptance-checks/'+$Name+'.csv'
    $Rows | Export-Csv -LiteralPath (Join-Path $root $relative) -NoTypeInformation -Encoding UTF8
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$checker,'-MatrixPath',$relative)
    if($Schema){$arguments+='-SchemaOnly'}
    $saved=$ErrorActionPreference
    try{$ErrorActionPreference='Continue'; & powershell.exe @arguments > (Join-Path $stage ($Name+'.log')) 2>&1;$ok=$LASTEXITCODE -eq 0}finally{$ErrorActionPreference=$saved}
    if($ok -ne $Expected){throw "Unexpected validation outcome: $Name"}
    ++$script:checks
}
function CopyRows {return @($original | ForEach-Object {$_.PSObject.Copy()})}
Check 'schema-valid' (CopyRows) $true $true
# A fixture deliberately has pending rows even if the eventual real matrix passes.
$rows=CopyRows;$rows[0].result='pending';Check 'pending-blocks-release' $rows $false
$rows=CopyRows;$rows[0].result='banana';Check 'invalid-result' $rows $false $true
$rows=CopyRows;$rows[0].scenario='unknown-fixture';Check 'unknown-scenario' $rows $false $true
$rows=CopyRows;Check 'duplicate-scenario' ($rows+@($rows[0].PSObject.Copy())) $false $true
$rows=CopyRows;Check 'missing-scenario' @($rows | Select-Object -Skip 1) $false $true
$rows=CopyRows;$rows[0].result='pass';$rows[0].evidenceFile='';Check 'missing-evidence' $rows $false $true
$rows=CopyRows;$rows[0].result='pass';$rows[0].evidenceFile='out/acceptance-checks/does-not-exist-proof.md';Check 'absent-evidence' $rows $false $true
$rows=CopyRows;$rows[0].mode='';Check 'missing-mode' $rows $false $true
$rows=CopyRows;$rows[0].testCommandOrSteps='';Check 'missing-procedure' $rows $false $true
$rows=CopyRows;$rows[0].result='pass';$rows[0].evidenceFile='../outside.md';Check 'escaping-evidence' $rows $false $true
$rows=CopyRows;$rows=$rows | Select-Object scenario,requirement,mode,testCommandOrSteps,evidenceFile,result;Check 'wrong-columns' $rows $false $true
Write-Host "Acceptance validator: $script:checks checks passed; fixture matrices were never promoted to real acceptance."
