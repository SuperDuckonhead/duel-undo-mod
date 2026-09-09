param([string]$RuntimeRoot = 'F:/MyCardLibrary/ygopro', [string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$original = @{}
foreach($name in @('ygopro.exe','Bot.exe','system.conf','load-once.conf','WindBot/WindBot.exe','WindBot/x86/sqlite3.dll','WindBot/x64/sqlite3.dll')) {
    $p = Join-Path $RuntimeRoot $name
    if(Test-Path -LiteralPath $p -PathType Leaf) { $original[$p] = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }
}
$stageName = 'r1-runtime-' + [Guid]::NewGuid().ToString('N')
& (Join-Path $PSScriptRoot 'Prepare-SmokeRuntime.ps1') -RuntimeRoot $RuntimeRoot -OutputName $stageName | Out-Null
$stage = Join-Path $repo "out/$stageName"
$alias = Join-Path $repo ('out/中文运行-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Junction -Path $alias -Target $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $repo "out/client/$Configuration/ygopro-undo.exe") -Destination (Join-Path $stage 'ygopro-undo.exe')
$config = Join-Path $stage 'system.conf'; $before = (Get-FileHash -LiteralPath $config).Hash
$once = Join-Path $stage 'load-once.conf'; [IO.File]::WriteAllText($once, '# controlled original sentinel')
$child = Start-Process -FilePath (Join-Path $alias 'ygopro-undo.exe') -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru
try {
    $newConfig = Join-Path $stage 'system-undo.conf'
    $ready = [Diagnostics.Stopwatch]::StartNew()
    while(!(Test-Path -LiteralPath $newConfig) -and !$child.HasExited -and $ready.Elapsed.TotalSeconds -lt 30) { Start-Sleep -Milliseconds 100; $child.Refresh() }
    if(!(Test-Path -LiteralPath $newConfig)) { throw 'Actual client did not initialize shared resources from executable root' }
    $child.Refresh()
    if(!$child.CloseMainWindow()) { throw 'Could not request normal actual-client shutdown' }
    if(!$child.WaitForExit(15000)) { throw 'Actual client did not exit normally' }
    if($child.ExitCode -ne 0) { throw "Actual client exit $($child.ExitCode)" }
    $shortcutPath = Join-Path $stage 'undo-shortcut.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = Join-Path $alias 'ygopro-undo.exe'
    $shortcut.WorkingDirectory = $env:TEMP
    $shortcut.Save()
    $previousConfigWrite = (Get-Item -LiteralPath $newConfig).LastWriteTimeUtc
    $child = Start-Process -FilePath $shortcutPath -WindowStyle Hidden -PassThru
    $shortcutHandle = $child.Handle
    $ready.Restart()
    while((Get-Item -LiteralPath $newConfig).LastWriteTimeUtc -eq $previousConfigWrite -and !$child.HasExited -and $ready.Elapsed.TotalSeconds -lt 30) { Start-Sleep -Milliseconds 100; $child.Refresh() }
    if((Get-Item -LiteralPath $newConfig).LastWriteTimeUtc -eq $previousConfigWrite) { throw 'Shell shortcut launch did not initialize executable-root config' }
    $child.Refresh()
    if(!$child.CloseMainWindow() -or !$child.WaitForExit(15000) -or $child.ExitCode -ne 0) { throw 'Shortcut-launched client failed normal shutdown' }
    Write-Output 'PASS Windows shell .lnk launch with unrelated WorkingDirectory initialized and closed normally'
    if((Get-FileHash -LiteralPath $config).Hash -ne $before) { throw 'Original controlled config modified' }
    if([IO.File]::ReadAllText($once) -ne '# controlled original sentinel') { throw 'Original load-once config changed' }
    foreach($p in $original.Keys) { if((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ne $original[$p]) { throw "Original installation changed: $p" } }
    Write-Output "PASS actual static client initialized/closed from unrelated cwd and Chinese executable root; original binaries/config/native dependency hashes unchanged. Stage: $alias"
} finally { if(!$child.HasExited) { Stop-Process -Id $child.Id -Force } }
# This tiny probe forces GetModuleFileNameW beyond its initial 256-wide-char buffer.
$probe = Join-Path $repo 'out/tests/Debug/runtime_paths_tests.exe'
$prefix = Join-Path $repo 'out/r1-tests/'
$leaf = 'runtime_paths_tests.exe'
$padding = 258 - $prefix.Length - 1 - $leaf.Length
$longRoot = Join-Path $prefix ('x' * $padding)
[IO.Directory]::CreateDirectory($longRoot) | Out-Null
$longExe = Join-Path $longRoot $leaf
Copy-Item -LiteralPath $probe -Destination $longExe -Force
$probeChild = Start-Process -FilePath $longExe -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru -Wait
if($probeChild.ExitCode -ne 0) { throw 'Dynamic executable path probe failed' }
Write-Output "PASS dynamic executable path probe, $($longExe.Length) UTF-16 path characters"
