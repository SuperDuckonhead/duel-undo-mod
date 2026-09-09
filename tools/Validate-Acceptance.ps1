param(
    [string]$MatrixPath = 'tests/acceptance.csv',
    [string]$SpecPath = 'openspec/changes/duel-undo-mod/specs/duel-undo/spec.md',
    [switch]$SchemaOnly
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function ResolveInput([string]$Relative) {
    $path = [IO.Path]::GetFullPath((Join-Path $repoRoot $Relative))
    if (-not $path.StartsWith($repoRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Acceptance input must stay inside the development checkout: $Relative"
    }
    return $path
}
$matrix = ResolveInput $MatrixPath
$spec = ResolveInput $SpecPath
$expected = @{}
$requirement = $null
foreach ($line in [IO.File]::ReadAllLines($spec)) {
    if ($line -match '^### Requirement: (.+)$') { $requirement = $Matches[1] }
    if ($line -match '^#### Scenario: (.+)$') {
        if (-not $requirement) { throw 'Scenario precedes its requirement' }
        $key = $requirement + [char]0x1f + $Matches[1]
        if ($expected.ContainsKey($key)) { throw "Duplicate specification scenario: $($Matches[1])" }
        $expected[$key] = $false
    }
}
$rows = @(Import-Csv -LiteralPath $matrix)
if (-not $rows.Count -or -not $expected.Count) { throw 'Empty acceptance matrix or specification' }
$columns = @('requirement','scenario','mode','testCommandOrSteps','evidenceFile','result')
if (($rows[0].PSObject.Properties.Name -join '|') -ne ($columns -join '|')) {
    throw 'Unexpected acceptance matrix columns'
}
$pending = @()
foreach ($row in $rows) {
    $key = $row.requirement + [char]0x1f + $row.scenario
    if (-not $expected.ContainsKey($key)) { throw "Unknown scenario: $($row.scenario)" }
    if ($expected[$key]) { throw "Duplicate matrix scenario: $($row.scenario)" }
    $expected[$key] = $true
    if ($row.result -notin @('pending','pass','fail')) { throw "Invalid result: $($row.result)" }
    if (-not $row.mode -or -not $row.testCommandOrSteps) { throw "Missing acceptance procedure: $($row.scenario)" }
    if ($row.result -ne 'pending') {
        if (-not $row.evidenceFile) { throw "Evidence missing: $($row.scenario)" }
        if (-not (Test-Path -LiteralPath (ResolveInput $row.evidenceFile) -PathType Leaf)) {
            throw "Evidence file missing: $($row.evidenceFile)"
        }
    }
    if ($row.result -ne 'pass') { $pending += $row.scenario }
}
if ($expected.Values -contains $false) { throw 'Specification scenarios are missing from the matrix' }
if (-not $SchemaOnly -and $pending.Count) {
    throw "Acceptance is incomplete: $($pending.Count) of $($rows.Count) scenarios are pending or failed."
}
if ($SchemaOnly) {
    Write-Host "Acceptance matrix structure valid: $($rows.Count) scenarios; $($pending.Count) pending/failed. This is not release acceptance."
} else {
    Write-Host "Acceptance recorded for all $($rows.Count) scenarios; evidence files exist. Review evidence before release."
}
