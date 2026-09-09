[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RuntimeRoot,
    [Parameter(Mandatory = $true)][string]$OutFile
)

$ErrorActionPreference = 'Stop'

function Get-PeMachine {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    if ($bytes.Length -lt 64) { throw "Invalid PE file (truncated DOS header): $LiteralPath" }
    if ($bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw "Invalid PE file (missing MZ signature): $LiteralPath" }

    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 0 -or $peOffset -gt ($bytes.Length - 6)) {
        throw "Invalid PE file (PE header offset out of range): $LiteralPath"
    }
    if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "Invalid PE file (missing PE signature): $LiteralPath"
    }

    '0x{0:X4}' -f [BitConverter]::ToUInt16($bytes, $peOffset + 4)
}

if (-not (Test-Path -LiteralPath $RuntimeRoot -PathType Container)) {
    throw "Runtime root does not exist or is not a directory: $RuntimeRoot"
}

$rows = foreach ($relativePath in @('ygopro.exe', 'Bot.exe', 'WindBot/WindBot.exe')) {
    $nativeRelativePath = $relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $binaryPath = Join-Path $RuntimeRoot $nativeRelativePath
    if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
        throw "Required runtime binary is missing: $relativePath"
    }

    [ordered]@{
        relativePath = $relativePath
        fileVersion  = (Get-Item -LiteralPath $binaryPath).VersionInfo.FileVersion
        sha256       = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash
        peMachine    = Get-PeMachine -LiteralPath $binaryPath
    }
}

$outDirectory = Split-Path -Parent ([IO.Path]::GetFullPath($OutFile))
if (-not [string]::IsNullOrEmpty($outDirectory)) {
    [IO.Directory]::CreateDirectory($outDirectory) | Out-Null
}
$json = ConvertTo-Json -InputObject @($rows) -Depth 5
$json = $json.Replace("`r`n", "`n")
[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutFile), $json + "`n", (New-Object Text.UTF8Encoding($false)))
