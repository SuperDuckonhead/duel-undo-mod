param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stage = Join-Path $root 'out/r1-tests/中文 安装'
$bot = Join-Path $stage 'WindBot'
[IO.Directory]::CreateDirectory($bot) | Out-Null
$source = Join-Path $root "out/bot/$Configuration"
foreach($name in @('WindBot-undo.exe','WindBot-undo.exe.config','undo-deps/x86/sqlite3.dll','undo-deps/x64/sqlite3.dll')) {
    $dest = Join-Path $bot $name
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination $dest -Force
}
$stdout = Join-Path $stage 'stdout.log'; $stderr = Join-Path $stage 'stderr.log'
$child = Start-Process -FilePath (Join-Path $bot 'WindBot-undo.exe') -ArgumentList 'Port=1' -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$childHandle = $child.Handle
if(!$child.WaitForExit(10000)) { Stop-Process -Id $child.Id -Force; throw 'Missing database did not fail promptly' }
$errors = [IO.File]::ReadAllText($stderr)
$expected = Join-Path $stage 'cards.cdb'
if(!$errors.Contains($expected)) { throw "Missing database error must name actual executable-root path '$expected': $errors" }
if($child.ExitCode -eq 0) { throw 'Missing database exit must fail' }
Write-Output 'PASS adapted bot starts from other cwd and Chinese installation path, missing database names actual path'
# A fresh directory contains only adapted artifacts and private DLLs; the worker
# reads existing AI resources directly, as in the W1 suite, without copying them.
Copy-Item -LiteralPath (Join-Path $root 'out/bot-tests/UndoTests.exe') -Destination (Join-Path $bot 'UndoTests.exe') -Force
$workerOut = Join-Path $stage 'worker.stdout.log'; $workerErr = Join-Path $stage 'worker.stderr.log'
$worker = Start-Process -FilePath (Join-Path $bot 'UndoTests.exe') -ArgumentList '--suite all' -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru -RedirectStandardOutput $workerOut -RedirectStandardError $workerErr
$workerHandle = $worker.Handle
if(!$worker.WaitForExit(30000)) { Stop-Process -Id $worker.Id -Force; throw 'Isolated Chinese bot worker tests timed out' }
if($worker.ExitCode -ne 0) { throw "Isolated Chinese bot worker tests failed: $([IO.File]::ReadAllText($workerErr))" }
Write-Output 'PASS all W1 suites plus actual loaded private SQLite module from Chinese adapted-only directory and unrelated cwd'