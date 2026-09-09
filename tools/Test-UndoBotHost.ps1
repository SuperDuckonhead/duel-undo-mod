param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$RuntimeRoot = 'F:/MyCardLibrary/ygopro',
    [string]$BotExecutable = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$portableRoot = $repoRoot.Replace('\','/')
if(!$BotExecutable) { $BotExecutable = "$portableRoot/out/bot/$Configuration/WindBot-undo.exe" }
if(!(Test-Path -LiteralPath $BotExecutable -PathType Leaf)) { throw 'Build the adapted bot first (tools/Build-BotTests.ps1).' }
$cmake = Join-Path $repoRoot '.cache/tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe'
$ctest = Join-Path $repoRoot '.cache/tools/cmake-4.4.3-windows-x86_64/bin/ctest.exe'
$toolchain = "$portableRoot/.cache/tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$build = Join-Path $repoRoot "out/tests/N2BotHost/$Configuration"
$logs = Join-Path $repoRoot "out/n2-bot-host/$Configuration"
[IO.Directory]::CreateDirectory($logs) | Out-Null
Push-Location $repoRoot
try {
    & $cmake -S . -B $build -G 'MinGW Makefiles' "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_CXX_COMPILER=$toolchain/clang++.exe" "-DCMAKE_MAKE_PROGRAM=$toolchain/mingw32-make.exe" "-DUNDO_BOT_TEST_EXE=$BotExecutable" "-DUNDO_TEST_RUNTIME_ROOT=$RuntimeRoot" *> (Join-Path $logs 'configure.log')
    if($LASTEXITCODE) { throw "Configure failed: $logs/configure.log" }
    & $cmake --build $build --target undo_bot_host_tests -j2 *> (Join-Path $logs 'build.log')
    if($LASTEXITCODE) { throw "Build failed: $logs/build.log" }
    $previousPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $ctest --test-dir $build -R '^undo_bot_host_tests$' --output-on-failure *> (Join-Path $logs 'tests.log')
        $testResult=$LASTEXITCODE
    } finally { $ErrorActionPreference=$previousPreference }
    Get-Content (Join-Path $logs 'tests.log')
    if($testResult) { throw "Actual private AI host test failed: $logs/tests.log" }
    Write-Host "Actual private AI host tests passed ($Configuration). Logs: $logs"
} finally { Pop-Location }
