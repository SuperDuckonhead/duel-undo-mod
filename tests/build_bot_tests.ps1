$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $repoRoot 'tools/Build-Bot.ps1'
$script:passed = 0
$script:failed = 0

function Invoke-ScriptProcess {
    param([string[]]$Arguments)
    $hostExe = (Get-Process -Id $PID).Path
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $hostExe -NoProfile -File $buildScript @Arguments 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function Test-Case {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body
        $script:passed++
        Write-Host "PASS: $Name"
    } catch {
        $script:failed++
        Write-Host "FAIL: $Name`n$($_.Exception.Message)"
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

Test-Case 'missing portable SDK fails before invoking MSBuild' {
    $result = Invoke-ScriptProcess @('-DotNetPath', '.cache/missing/dotnet.exe')
    Assert-True ($result.ExitCode -ne 0) 'missing SDK was accepted'
    Assert-True ($result.Output -match 'Portable .NET SDK not found') "unexpected failure: $($result.Output)"
}

Test-Case 'output directory cannot escape repository out directory' {
    $result = Invoke-ScriptProcess @('-OutputDirectory', '../outside')
    Assert-True ($result.ExitCode -ne 0) 'unsafe output directory was accepted'
    Assert-True ($result.Output -match 'OutputDirectory must be inside') "unexpected failure: $($result.Output)"
}

Test-Case 'native tool failure is returned as a build failure' {
    $result = Invoke-ScriptProcess @('-DotNetPath', 'C:/Windows/System32/where.exe')
    Assert-True ($result.ExitCode -ne 0) 'native tool failure was ignored'
    Assert-True ($result.Output -match 'Bot build failed') "unexpected failure: $($result.Output)"
}

Test-Case 'release build handles spaces and writes both executables under out' {
    $relativeOutput = 'out/tests/bot build'
    $absoluteOutput = Join-Path $repoRoot $relativeOutput
    if (Test-Path -LiteralPath $absoluteOutput) {
        Remove-Item -LiteralPath $absoluteOutput -Recurse -Force
    }
    $result = Invoke-ScriptProcess @('-Configuration', 'Release', '-OutputDirectory', $relativeOutput)
    Assert-True ($result.ExitCode -eq 0) "build failed: $($result.Output)"
    Assert-True (Test-Path -LiteralPath (Join-Path $absoluteOutput 'WindBot.exe')) 'WindBot.exe was not emitted'
    Assert-True (Test-Path -LiteralPath (Join-Path $absoluteOutput 'Bot.exe')) 'Bot.exe was not emitted'
    Assert-True (Test-Path -LiteralPath (Join-Path $absoluteOutput 'WindBot.exe.config')) 'WindBot configuration was not emitted'
    Assert-True (Test-Path -LiteralPath (Join-Path $absoluteOutput 'x86/sqlite3.dll')) 'x86 sqlite dependency was not emitted'
    Assert-True (Test-Path -LiteralPath (Join-Path $absoluteOutput 'x64/sqlite3.dll')) 'x64 sqlite dependency was not emitted'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $absoluteOutput 'Decks'))) 'excluded runtime Decks were emitted'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $absoluteOutput 'Dialogs'))) 'excluded runtime Dialogs were emitted'
}

Write-Host "Result: $script:passed passed, $script:failed failed"
if ($script:failed -gt 0) { exit 1 }
