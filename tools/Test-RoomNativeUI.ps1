param(
    [string]$RuntimeRoot = 'F:/MyCardLibrary/ygopro',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('confirm','fade','resize','clock','early-status','all')][string]$Scenario = 'all'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'PackageValidation.ps1')
$out = Join-Path $root 'out'; Assert-PackagePath $out $root
$id = [guid]::NewGuid().ToString('N')
$testDir = Join-Path $out ('room-native-ui-'+$id); Assert-PackagePath $testDir $out
[IO.Directory]::CreateDirectory($testDir) | Out-Null
$profile = Get-Content -LiteralPath (Join-Path $root 'build-profile.json') -Raw | ConvertFrom-Json
$compiler = Join-Path $root $profile.compiler.path
$build = Join-Path $root 'client/build'
$makefile = Get-Content -LiteralPath (Join-Path $build 'YGOPro.make') -Raw
$section = [regex]::Match($makefile, ('(?s)ifeq \(\$\(config\),'+$Configuration.ToLowerInvariant()+'\)(.*?)(?:else ifeq|endif)')).Groups[1].Value
if (-not $section) { throw 'Build the selected native client configuration first.' }
$defs = [regex]::Match($section,'(?m)^DEFINES \+= (.*)$').Groups[1].Value.Trim() -split ' '
$includes = [regex]::Match($makefile,'(?m)^INCLUDES \+= (.*)$').Groups[1].Value.Trim() -split ' '
$libs = [regex]::Match($section,'(?m)^LIBS \+= (.*)$').Groups[1].Value.Trim() -split ' '
$objects = @([regex]::Matches($makefile,'(?m)^OBJECTS \+= \$\(OBJDIR\)/(.*\.o)') |
    ForEach-Object { $_.Groups[1].Value.Trim() } |
    Where-Object { $_ -notin @('gframe.o','duelclient.o','room_config.o') } |
    ForEach-Object { Join-Path $root "client/obj/$Configuration/YGOPro/$_" })
$before = @(foreach ($file in $objects) { [pscustomobject]@{path=$file;sha256=(Get-FileHash -LiteralPath $file).Hash} })
$exe = Join-Path $testDir 'room_native_ui_tests.exe'
Push-Location $build
try {
    # The test includes the current transport, so compile its configuration
    # dependency privately as well. Shared UI objects remain untouched.
    $localConfig = Join-Path $testDir 'room_config.o'
    & $compiler '-std=c++17' '-fno-rtti' '-g' @defs @includes '-I../gframe' '-c' '../gframe/undo/room_config.cpp' '-o' $localConfig *> (Join-Path $testDir 'config-build.log')
    if ($LASTEXITCODE) { throw "Native UI configuration compile failed: $testDir/config-build.log" }
    $objects += @($localConfig)
    & $compiler '-std=c++17' '-fno-rtti' '-static' '-g' @defs @includes '-I../gframe' '-I../../tests' (Join-Path $root 'tests/room_native_ui_tests.cpp') @objects @libs '-o' $exe *> (Join-Path $testDir 'build.log')
    if ($LASTEXITCODE) { throw "Native UI harness compile failed: $testDir/build.log" }
} finally { Pop-Location }
foreach ($file in $before) { if ((Get-FileHash -LiteralPath $file.path).Hash -ne $file.sha256) { throw 'Native objects changed during harness link' } }
[IO.File]::WriteAllText((Join-Path $testDir 'objects.json'), ($before | ConvertTo-Json -Depth 3), [Text.UTF8Encoding]::new($false))
$binding = [ordered]@{
    scenario = $Scenario
    executableSha256 = (Get-FileHash -LiteralPath $exe).Hash
    privateConfigurationSha256 = (Get-FileHash -LiteralPath $localConfig).Hash
    testSourceSha256 = (Get-FileHash -LiteralPath (Join-Path $root 'tests/room_native_ui_tests.cpp')).Hash
    transportSourceSha256 = (Get-FileHash -LiteralPath (Join-Path $root 'client/gframe/duelclient.cpp')).Hash
    configurationSourceSha256 = (Get-FileHash -LiteralPath (Join-Path $root 'client/gframe/undo/room_config.cpp')).Hash
    driverSourceSha256 = (Get-FileHash -LiteralPath $PSCommandPath).Hash
}
[IO.File]::WriteAllText((Join-Path $testDir 'binding.json'), ($binding | ConvertTo-Json -Depth 3), [Text.UTF8Encoding]::new($false))
$scenarios = if ($Scenario -eq 'all') { @('confirm','fade','resize','clock','early-status') } else { @($Scenario) }
$failures = @()
foreach ($item in $scenarios) {
    $stageName = 'room-native-ui-runtime-'+$item+'-'+$id
    & (Join-Path $PSScriptRoot 'Prepare-SmokeRuntime.ps1') -RuntimeRoot $RuntimeRoot -OutputName $stageName | Out-Null
    $stage = Join-Path $out $stageName
    $child = Start-Process -FilePath $exe -ArgumentList $item -WorkingDirectory $stage -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDir "$item.log") -RedirectStandardError (Join-Path $testDir "$item-error.log")
    $null = $child.Handle
    try {
        if (-not $child.WaitForExit(25000)) { $failures += "$item timeout" }
        elseif ($child.ExitCode -ne 0) { $failures += "$item exit=$($child.ExitCode)" }
        Get-Content -LiteralPath (Join-Path $testDir "$item.log"),(Join-Path $testDir "$item-error.log")
    } finally {
        if (-not $child.HasExited) { $child.Kill(); [void]$child.WaitForExit(10000) }
        $child.Dispose()
    }
}
Write-Host "Native Room UI evidence: $testDir"
if ($failures.Count) { throw ($failures -join '; ') }
Write-Host 'PASS native Room UI and real MainLoop gates'
