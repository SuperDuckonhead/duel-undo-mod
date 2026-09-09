param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$RuntimeRoot = 'F:/MyCardLibrary/ygopro/WindBot',
    [string]$Database = 'F:/MyCardLibrary/ygopro/cards.cdb'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$portableRoot = $repoRoot.Replace('\','/')
$cmake = Join-Path $repoRoot '.cache/tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe'
$ctest = Join-Path $repoRoot '.cache/tools/cmake-4.4.3-windows-x86_64/bin/ctest.exe'
$toolchain = "$portableRoot/.cache/tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$build = Join-Path $repoRoot "out/tests/N2Bot/$Configuration"
$logs = Join-Path $repoRoot "out/n2-bot/$Configuration"
[IO.Directory]::CreateDirectory($logs) | Out-Null
$oldRuntime = $env:WIND_BOT_RUNTIME
$oldDatabase = $env:WIND_BOT_DATABASE
Push-Location $repoRoot
try {
    $env:WIND_BOT_RUNTIME = [IO.Path]::GetFullPath($RuntimeRoot)
    $env:WIND_BOT_DATABASE = [IO.Path]::GetFullPath($Database)
    & powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build-BotTests.ps1 -Configuration $Configuration *> (Join-Path $logs 'build-bot.log')
    if($LASTEXITCODE) { throw "Bot build failed; see $logs/build-bot.log" }
    & out/bot-tests/UndoTests.exe --suite all *> (Join-Path $logs 'bot-tests.log')
    Get-Content (Join-Path $logs 'bot-tests.log')
    if($LASTEXITCODE) { throw "Managed bot tests failed; see $logs/bot-tests.log" }
    & $cmake -S . -B $build -G 'MinGW Makefiles' "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_CXX_COMPILER=$toolchain/clang++.exe" "-DCMAKE_MAKE_PROGRAM=$toolchain/mingw32-make.exe" "-DUNDO_BOT_TEST_EXE=$portableRoot/out/bot/$Configuration/WindBot-undo.exe" "-DUNDO_BOT_TEST_RUNTIME=$env:WIND_BOT_RUNTIME" *> (Join-Path $logs 'configure-native.log')
    if($LASTEXITCODE) { throw "Native configure failed; see $logs/configure-native.log" }
    & $cmake --build $build --target bot_transaction_tests host_bot_seat_tests -j2 *> (Join-Path $logs 'build-native.log')
    if($LASTEXITCODE) { throw "Native build failed; see $logs/build-native.log" }
    & $ctest --test-dir $build -R '^(bot_transaction_tests|host_bot_seat_tests)$' --output-on-failure *> (Join-Path $logs 'native-tests.log')
    Get-Content (Join-Path $logs 'native-tests.log')
    if($LASTEXITCODE) { throw "Native bot transactions failed; see $logs/native-tests.log" }
    Write-Host "N2 bot support verification passed ($Configuration); logs: $logs"
} finally {
    Pop-Location
    $env:WIND_BOT_RUNTIME = $oldRuntime
    $env:WIND_BOT_DATABASE = $oldDatabase
}