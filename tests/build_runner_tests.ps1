$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runnerSource = Join-Path $repoRoot 'tools/Build.ps1'
$passed = 0
$failed = 0

function Assert-Equal($Expected, $Actual, [string]$Message) {
    if ($Expected -ne $Actual) { throw "$Message Expected '$Expected', got '$Actual'." }
}
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Invoke-Case([string]$Name, [scriptblock]$Body) {
    try { & $Body; $script:passed++; Write-Host "PASS $Name" }
    catch { $script:failed++; Write-Host "FAIL $Name\: $($_.Exception.Message)" }
}
function New-Fixture([object[]]$Commands) {
    $fixture = Join-Path ([IO.Path]::GetTempPath()) ("ygopro-build-runner-" + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory((Join-Path $fixture 'tools')) | Out-Null
    Copy-Item -LiteralPath $runnerSource -Destination (Join-Path $fixture 'tools/Build.ps1')
    @{ commands = $Commands } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $fixture 'build-profile.json') -Encoding UTF8
    @(
        'param([string]$Output, [int]$ExitCode = 0, [Parameter(ValueFromRemainingArguments=$true)][string[]]$Tokens)',
        '@{ workingDirectory = [Environment]::CurrentDirectory; tokens = @($Tokens) } | ConvertTo-Json -Compress | Add-Content -LiteralPath $Output -Encoding UTF8',
        'exit $ExitCode'
    ) | Set-Content -LiteralPath (Join-Path $fixture 'record.ps1') -Encoding UTF8
    return $fixture
}
function Run-Runner([string]$Fixture, [string]$Target, [string]$Configuration) {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Fixture 'tools/Build.ps1') -Target $Target -Configuration $Configuration 2>&1
        return @{ ExitCode = $LASTEXITCODE; Output = ($output -join "`n") }
    }
    finally { $ErrorActionPreference = $previousPreference }
}

Invoke-Case 'filters target and configuration and preserves argument boundaries' {
    $fixture = New-Fixture @(
        @{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File','record.ps1','run.jsonl','0','value with spaces'); targets = @('Tests'); configuration = 'Debug' },
        @{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File','record.ps1','run.jsonl','0','wrong configuration'); targets = @('Tests'); configuration = 'Release' },
        @{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File','record.ps1','run.jsonl','0','wrong target'); targets = @('Client'); configuration = 'Debug' }
    )
    try {
        $result = Run-Runner $fixture Tests Debug
        Assert-Equal 0 $result.ExitCode $result.Output
        $records = @(Get-Content -LiteralPath (Join-Path $fixture 'run.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        Assert-Equal 1 $records.Count 'Runner selected the wrong number of commands.'
        Assert-Equal 'value with spaces' $records[0].tokens[0] 'Runner changed an argument boundary.'
        Assert-Equal $fixture $records[0].workingDirectory 'Runner used the wrong working directory.'
    } finally { Remove-Item -LiteralPath $fixture -Recurse -Force }
}

Invoke-Case 'passes shell metacharacters as inert arguments' {
    $fixture = New-Fixture @(@{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File','record.ps1','run.jsonl','0','$(Set-Content injected.txt bad)'); targets = @('Tests'); configuration = 'Debug' })
    try {
        $result = Run-Runner $fixture Tests Debug
        Assert-Equal 0 $result.ExitCode $result.Output
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $fixture 'injected.txt'))) 'Runner evaluated shell code from an argument.'
        $record = Get-Content -Raw -LiteralPath (Join-Path $fixture 'run.jsonl') | ConvertFrom-Json
        Assert-Equal '$(Set-Content injected.txt bad)' $record.tokens[0] 'Runner changed the metacharacter argument.'
    } finally { Remove-Item -LiteralPath $fixture -Recurse -Force }
}

Invoke-Case 'rejects unknown target values' {
    $fixture = New-Fixture @()
    try { $result = Run-Runner $fixture Unknown Debug; Assert-True ($result.ExitCode -ne 0) 'Unknown target unexpectedly succeeded.' }
    finally { Remove-Item -LiteralPath $fixture -Recurse -Force }
}

Invoke-Case 'fails when no commands match' {
    $fixture = New-Fixture @(@{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-File','record.ps1','run.jsonl','0'); targets = @('Client'); configuration = 'Debug' })
    try { $result = Run-Runner $fixture Tests Debug; Assert-True ($result.ExitCode -ne 0) 'No-match run unexpectedly succeeded.'; Assert-True ($result.Output -match 'No matching build commands') 'No-match error was not reported.' }
    finally { Remove-Item -LiteralPath $fixture -Recurse -Force }
}

Invoke-Case 'propagates a child program failure' {
    $fixture = New-Fixture @(@{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File','record.ps1','run.jsonl','7'); targets = @('Tests'); configuration = 'Debug' })
    try { $result = Run-Runner $fixture Tests Debug; Assert-True ($result.ExitCode -ne 0) 'Child failure unexpectedly succeeded.'; Assert-True ($result.Output -match 'Build command failed') 'Child failure was not reported.' }
    finally { Remove-Item -LiteralPath $fixture -Recurse -Force }
}

Invoke-Case 'All selects every target for the requested configuration' {
    $fixture = New-Fixture @(
        @{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File','record.ps1','run.jsonl','0','client'); targets = @('Client'); configuration = 'Debug' },
        @{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File','record.ps1','run.jsonl','0','bot'); targets = @('Bot'); configuration = 'Debug' },
        @{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File','record.ps1','run.jsonl','0','release'); targets = @('Tests'); configuration = 'Release' }
    )
    try {
        $result = Run-Runner $fixture All Debug
        Assert-Equal 0 $result.ExitCode $result.Output
        $records = @(Get-Content -LiteralPath (Join-Path $fixture 'run.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        Assert-Equal 2 $records.Count 'All did not select every matching target.'
        Assert-Equal 'client' $records[0].tokens[0] 'All changed command order.'
        Assert-Equal 'bot' $records[1].tokens[0] 'All changed command order.'
    } finally { Remove-Item -LiteralPath $fixture -Recurse -Force }
}

Invoke-Case 'rejects unknown configuration values' {
    $fixture = New-Fixture @()
    try { $result = Run-Runner $fixture Tests Other; Assert-True ($result.ExitCode -ne 0) 'Unknown configuration unexpectedly succeeded.' }
    finally { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
Invoke-Case 'expands repository root placeholders inside arguments' {
    $fixture = New-Fixture @(@{ program = 'powershell.exe'; workingDirectory = '.'; arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File','record.ps1','run.jsonl','0','{repoRoot}/tool path'); targets = @('Tests'); configuration = 'Debug' })
    try {
        $result = Run-Runner $fixture Tests Debug
        Assert-Equal 0 $result.ExitCode $result.Output
        $record = Get-Content -Raw -LiteralPath (Join-Path $fixture 'run.jsonl') | ConvertFrom-Json
        Assert-Equal (($fixture -replace '\\','/') + '/tool path') $record.tokens[0] 'Runner did not expand the portable repository-root placeholder.'
    } finally { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
Write-Host "$passed passed, $failed failed"
if ($failed -ne 0) { exit 1 }
