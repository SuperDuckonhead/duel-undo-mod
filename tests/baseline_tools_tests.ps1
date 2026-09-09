$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$measureScript = Join-Path $repoRoot 'tools/Measure-InstalledBaseline.ps1'
$lockScript = Join-Path $repoRoot 'tools/Test-SourceLock.ps1'
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('ygopro-baseline-tests-' + [guid]::NewGuid().ToString('N'))
$script:passed = 0
$script:failed = 0

function Invoke-ScriptProcess {
    param([string]$ScriptPath, [string[]]$Arguments)
    $hostExe = (Get-Process -Id $PID).Path
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $hostExe -NoProfile -File $ScriptPath @Arguments 2>&1 | Out-String
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

function New-TestPe {
    param([string]$Path, [uint16]$Machine)
    $directory = Split-Path -Parent $Path
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $bytes = New-Object byte[] 512
    $bytes[0] = 0x4d; $bytes[1] = 0x5a
    [BitConverter]::GetBytes([int32]128).CopyTo($bytes, 0x3c)
    $bytes[128] = 0x50; $bytes[129] = 0x45
    [BitConverter]::GetBytes($Machine).CopyTo($bytes, 132)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Write-LockFixture {
    param([string]$Path, [object[]]$Components)
    @{ components = $Components } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Path -Encoding UTF8
}

try {
    [IO.Directory]::CreateDirectory($tempRoot) | Out-Null

    Test-Case 'measurement emits deterministic metadata for all required binaries' {
        $runtime = Join-Path $tempRoot 'runtime-valid'
        New-TestPe (Join-Path $runtime 'ygopro.exe') 0x014c
        New-TestPe (Join-Path $runtime 'Bot.exe') 0x8664
        New-TestPe (Join-Path $runtime 'WindBot/WindBot.exe') 0x01c4
        $outFile = Join-Path $tempRoot 'nested/baseline.json'
        $result = Invoke-ScriptProcess $measureScript @('-RuntimeRoot', $runtime, '-OutFile', $outFile)
        Assert-True ($result.ExitCode -eq 0) "measurement failed: $($result.Output)"
        $jsonBytes = [IO.File]::ReadAllBytes($outFile)
        Assert-True (-not ($jsonBytes.Length -ge 3 -and $jsonBytes[0] -eq 0xef -and $jsonBytes[1] -eq 0xbb -and $jsonBytes[2] -eq 0xbf)) 'measurement JSON must not contain a UTF-8 BOM'
        Assert-True (-not ([IO.File]::ReadAllText($outFile).Contains("`r"))) 'measurement JSON must use stable LF line endings'
        $rows = Get-Content -LiteralPath $outFile -Raw | ConvertFrom-Json
        Assert-True ($rows.Count -eq 3) 'expected exactly three measurements'
        Assert-True (($rows.relativePath -join ',') -eq 'ygopro.exe,Bot.exe,WindBot/WindBot.exe') 'unexpected measurement order or paths'
        Assert-True ($rows[0].peMachine -eq '0x014C') 'x86 PE machine was not decoded'
        Assert-True ($rows[1].peMachine -eq '0x8664') 'x64 PE machine was not decoded'
        Assert-True ($rows[0].sha256 -match '^[0-9A-F]{64}$') 'SHA-256 is missing or malformed'
        Assert-True ($null -ne $rows[0].PSObject.Properties['fileVersion']) 'fileVersion property must be present'
    }

    Test-Case 'measurement rejects a truncated DOS header' {
        $runtime = Join-Path $tempRoot 'runtime-short'
        [IO.Directory]::CreateDirectory($runtime) | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $runtime 'ygopro.exe'), [byte[]](0x4d, 0x5a))
        New-TestPe (Join-Path $runtime 'Bot.exe') 0x014c
        New-TestPe (Join-Path $runtime 'WindBot/WindBot.exe') 0x014c
        $result = Invoke-ScriptProcess $measureScript @('-RuntimeRoot', $runtime, '-OutFile', (Join-Path $tempRoot 'short.json'))
        Assert-True ($result.ExitCode -ne 0) 'truncated executable was accepted'
    }

    Test-Case 'measurement rejects an invalid PE offset and signature' {
        $runtime = Join-Path $tempRoot 'runtime-invalid-pe'
        New-TestPe (Join-Path $runtime 'ygopro.exe') 0x014c
        New-TestPe (Join-Path $runtime 'Bot.exe') 0x014c
        New-TestPe (Join-Path $runtime 'WindBot/WindBot.exe') 0x014c
        $badPath = Join-Path $runtime 'ygopro.exe'
        $bytes = [IO.File]::ReadAllBytes($badPath)
        [BitConverter]::GetBytes([int32]510).CopyTo($bytes, 0x3c)
        [IO.File]::WriteAllBytes($badPath, $bytes)
        $badOffset = Invoke-ScriptProcess $measureScript @('-RuntimeRoot', $runtime, '-OutFile', (Join-Path $tempRoot 'offset.json'))
        Assert-True ($badOffset.ExitCode -ne 0) 'out-of-range PE offset was accepted'
        New-TestPe $badPath 0x014c
        $bytes = [IO.File]::ReadAllBytes($badPath); $bytes[128] = 0
        [IO.File]::WriteAllBytes($badPath, $bytes)
        $badSignature = Invoke-ScriptProcess $measureScript @('-RuntimeRoot', $runtime, '-OutFile', (Join-Path $tempRoot 'signature.json'))
        Assert-True ($badSignature.ExitCode -ne 0) 'invalid PE signature was accepted'
    }

    $validComponent = [ordered]@{
        name = 'client'; url = 'https://example.invalid/client.git'
        commit = '0123456789abcdef0123456789abcdef01234567'
        evidenceUrl = 'https://example.invalid/releases/1'; evidenceNote = 'Release manifest reference.'
        destination = 'client'
    }

    Test-Case 'source lock accepts complete entries and intentional nested destinations' {
        $path = Join-Path $tempRoot 'valid-lock.json'
        $nested = [ordered]@{} + $validComponent
        $nested.name = 'ocgcore'; $nested.destination = 'client/ocgcore'
        Write-LockFixture $path @($validComponent, $nested)
        $result = Invoke-ScriptProcess $lockScript @('-Path', $path)
        Assert-True ($result.ExitCode -eq 0) "valid lock rejected: $($result.Output)"
    }

    Test-Case 'source lock rejects malformed JSON, non-array components, and incomplete entries' {
        $malformed = Join-Path $tempRoot 'malformed.json'; '{bad json' | Set-Content -LiteralPath $malformed
        Assert-True ((Invoke-ScriptProcess $lockScript @('-Path', $malformed)).ExitCode -ne 0) 'malformed JSON was accepted'
        $notArray = Join-Path $tempRoot 'not-array.json'; '{"components":{"name":"client"}}' | Set-Content -LiteralPath $notArray
        Assert-True ((Invoke-ScriptProcess $lockScript @('-Path', $notArray)).ExitCode -ne 0) 'object-valued components was accepted'
        $incomplete = Join-Path $tempRoot 'incomplete.json'; Write-LockFixture $incomplete @(@{ name = 'client'; commit = 'master' })
        Assert-True ((Invoke-ScriptProcess $lockScript @('-Path', $incomplete)).ExitCode -ne 0) 'incomplete lock entry was accepted'
    }

    Test-Case 'source lock rejects invalid commits and URLs' {
        foreach ($commit in @('master', '0123456789ABCDEF0123456789ABCDEF01234567', '0123456789abcdef0123456789abcdef0123456g')) {
            $component = [ordered]@{} + $validComponent; $component.commit = $commit
            $path = Join-Path $tempRoot ('commit-' + [guid]::NewGuid() + '.json'); Write-LockFixture $path @($component)
            Assert-True ((Invoke-ScriptProcess $lockScript @('-Path', $path)).ExitCode -ne 0) "invalid commit accepted: $commit"
        }
        $component = [ordered]@{} + $validComponent; $component.url = 'not a URL'
        $path = Join-Path $tempRoot 'bad-url.json'; Write-LockFixture $path @($component)
        Assert-True ((Invoke-ScriptProcess $lockScript @('-Path', $path)).ExitCode -ne 0) 'invalid URL was accepted'
    }

    Test-Case 'source lock rejects absolute, drive-relative, traversal, and empty path segments' {
        foreach ($destination in @('C:\client', 'C:client', '\\server\share', '/client', '../client', 'client/../core', './client', 'client//core', 'client/')) {
            $component = [ordered]@{} + $validComponent; $component.destination = $destination
            $path = Join-Path $tempRoot ('path-' + [guid]::NewGuid() + '.json'); Write-LockFixture $path @($component)
            Assert-True ((Invoke-ScriptProcess $lockScript @('-Path', $path)).ExitCode -ne 0) "unsafe destination accepted: $destination"
        }
    }
    Test-Case 'source lock rejects Windows reserved device names and unusable segments' {
        $controlCharacterPath = 'client/bad' + [char]1 + 'name'
        foreach ($destination in @('NUL', 'client/con.txt', 'client/PRN', 'client/AUX.log', 'client/COM1', 'client/com9.dll', 'client/LPT1', 'client/lpt9.txt', 'client/trailing.', 'client/trailing ', $controlCharacterPath)) {
            $component = [ordered]@{} + $validComponent; $component.destination = $destination
            $path = Join-Path $tempRoot ('windows-path-' + [guid]::NewGuid() + '.json'); Write-LockFixture $path @($component)
            Assert-True ((Invoke-ScriptProcess $lockScript @('-Path', $path)).ExitCode -ne 0) "Windows-invalid destination accepted: $destination"
        }
    }
} finally {
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}

Write-Host "$script:passed passed, $script:failed failed"
if ($script:failed -ne 0) { exit 1 }
