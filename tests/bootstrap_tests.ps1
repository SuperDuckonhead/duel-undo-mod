$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$area = Join-Path $repo ('out/bootstrap-tests-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($area) | Out-Null
$passed = 0; $failed = 0
function Check([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function New-Fixture([string]$Name) {
    $fixture = Join-Path $area $Name
    foreach ($dir in @('tools','.cache/dependencies','fixture/package/src','client/gframe')) {
        [IO.Directory]::CreateDirectory((Join-Path $fixture $dir)) | Out-Null
    }
    foreach ($tool in @('Bootstrap.ps1','Test-SourceLock.ps1')) {
        $source = Join-Path $repo "tools/$tool"
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $fixture "tools/$tool") }
    }
    [IO.File]::WriteAllText((Join-Path $fixture 'fixture/package/src/lua.h'), 'public fixture bytes')
    [IO.File]::WriteAllText((Join-Path $fixture 'client/gframe/sentinel.txt'), 'preserve source')
    & tar.exe -czf (Join-Path $fixture '.cache/dependencies/lua.tar.gz') -C (Join-Path $fixture 'fixture') package
    if ($LASTEXITCODE -ne 0) { throw 'Fixture archive creation failed' }
    $hash = (Get-FileHash -LiteralPath (Join-Path $fixture '.cache/dependencies/lua.tar.gz') -Algorithm SHA256).Hash.ToLowerInvariant()
    $lock = @{
        components=@(@{name='client';url='https://example.invalid/client.git';commit=('a'*40);destination='client';evidenceUrl='https://example.invalid/commit';evidenceNote='Test fixture'})
        archives=@(@{name='lua';url='https://example.invalid/lua.tar.gz';sha256=$hash;destination='client/lua'})
        toolchainArchives=@()
    }
    [IO.File]::WriteAllText((Join-Path $fixture 'sources.lock.json'), ($lock | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    return $fixture
}
function Run-Bootstrap([string]$Fixture) {
    $ErrorActionPreference = 'Continue'; $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Fixture 'tools/Bootstrap.ps1') -Target Libraries -LibraryName lua -Offline 2>&1
    return @{ Code=$LASTEXITCODE; Text=($output -join ' ') }
}
function New-ToolFixture([string]$Name) {
    $fixture = Join-Path $area $Name
    foreach ($dir in @('tools','.cache/tools','fixture/tool-package','client')) {
        [IO.Directory]::CreateDirectory((Join-Path $fixture $dir)) | Out-Null
    }
    foreach ($tool in @('Bootstrap.ps1','Test-SourceLock.ps1')) {
        Copy-Item -LiteralPath (Join-Path $repo "tools/$tool") -Destination (Join-Path $fixture "tools/$tool")
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.File]::WriteAllText((Join-Path $fixture 'fixture/tool-package/payload.txt'), 'fixed tool fixture')
    $archiveTemplate = Join-Path $fixture 'fixture/tool.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $fixture 'fixture/tool-package'), $archiveTemplate)
    $toolSpecs = @(
        @{name='cmake';cache='.cache/tools/cmake.zip';probe='.cache/tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe'},
        @{name='llvm-mingw';cache='.cache/tools/llvm-mingw.zip';probe='.cache/tools/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe'},
        @{name='premake';cache='.cache/tools/premake.zip';probe='.cache/tools/premake/premake5.exe'},
        @{name='dotnet-sdk';cache='.cache/tools/dotnet-sdk-8.0.425.zip';probe='.cache/tools/dotnet/dotnet.exe'},
        @{name='net48-reference-assemblies';cache='.cache/microsoft.netframework.referenceassemblies.net48.1.0.3.nupkg';probe='.cache/net48-ref/build/.NETFramework/v4.8/mscorlib.dll'}
    )
    $toolRecords = @()
    foreach ($spec in $toolSpecs) {
        $cache = Join-Path $fixture $spec.cache
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($cache)) | Out-Null
        Copy-Item -LiteralPath $archiveTemplate -Destination $cache
        $probe = Join-Path $fixture $spec.probe
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($probe)) | Out-Null
        [IO.File]::WriteAllText($probe, 'existing probe')
        $toolRecords += @{name=$spec.name;url="https://example.invalid/$($spec.name).zip";sha256=(Get-FileHash -LiteralPath $cache -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $lock = @{
        components=@(@{name='client';url='https://example.invalid/client.git';commit=('a'*40);destination='client';evidenceUrl='https://example.invalid/commit';evidenceNote='Test fixture'})
        archives=@()
        toolchainArchives=$toolRecords
    }
    [IO.File]::WriteAllText((Join-Path $fixture 'sources.lock.json'), ($lock | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    return $fixture
}
function Run-BootstrapTools([string]$Fixture) {
    $ErrorActionPreference = 'Continue'; $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Fixture 'tools/Bootstrap.ps1') -Target Tools -Offline 2>&1
    return @{ Code=$LASTEXITCODE; Text=($output -join ' ') }
}
function Case([string]$Name, [scriptblock]$Body) {
    try { & $Body; $script:passed++; Write-Output "PASS: $Name" }
    catch { $script:failed++; Write-Output "FAIL: $Name - $_" }
}
Case 'verified offline archive extracts expected bytes' {
    $fixture = New-Fixture 'valid'
    $result = Run-Bootstrap $fixture
    Check ($result.Code -eq 0) $result.Text
    Check ([IO.File]::ReadAllText((Join-Path $fixture 'client/lua/src/lua.h')) -eq 'public fixture bytes') 'Wrong extracted bytes'
}
Case 'checksum mismatch refuses before destination mutation' {
    $fixture = New-Fixture 'checksum'
    $lockPath = Join-Path $fixture 'sources.lock.json'
    $lock = Get-Content -Raw $lockPath | ConvertFrom-Json
    $lock.archives[0].sha256 = '0'*64
    [IO.File]::WriteAllText($lockPath, ($lock | ConvertTo-Json -Depth 8))
    $result = Run-Bootstrap $fixture
    Check ($result.Code -ne 0 -and $result.Text -match 'checksum') 'Checksum mismatch was not rejected'
    Check (-not (Test-Path -LiteralPath (Join-Path $fixture 'client/lua'))) 'Destination created before checksum validation'
}
Case 'source destination is forbidden and source stays untouched' {
    $fixture = New-Fixture 'destination'
    $lockPath = Join-Path $fixture 'sources.lock.json'
    $lock = Get-Content -Raw $lockPath | ConvertFrom-Json
    $lock.archives[0].destination = 'client/gframe'
    [IO.File]::WriteAllText($lockPath, ($lock | ConvertTo-Json -Depth 8))
    $result = Run-Bootstrap $fixture
    Check ($result.Code -ne 0 -and $result.Text -match 'destination') 'Forbidden source destination accepted'
    Check ([IO.File]::ReadAllText((Join-Path $fixture 'client/gframe/sentinel.txt')) -eq 'preserve source') 'Source modified'
}
Case 'offline missing archive reports prerequisite without extraction' {
    $fixture = New-Fixture 'missing'
    $archive = Join-Path $fixture '.cache/dependencies/lua.tar.gz'
    Move-Item -LiteralPath $archive -Destination ($archive + '.saved')
    $result = Run-Bootstrap $fixture
    Check ($result.Code -ne 0 -and $result.Text -match 'Offline') 'Offline missing archive not diagnosed'
    Check (-not (Test-Path -LiteralPath (Join-Path $fixture 'client/lua'))) 'Missing archive created destination'
}
Case 'incomplete existing dependency refuses rather than reporting ready' {
    $fixture = New-Fixture 'partial'
    [IO.Directory]::CreateDirectory((Join-Path $fixture 'client/lua')) | Out-Null
    $result = Run-Bootstrap $fixture
    Check ($result.Code -ne 0 -and $result.Text -match 'incomplete') 'Incomplete dependency was accepted'
}
Case 'interrupted tool extraction with an existing probe is rejected' {
    $fixture = New-ToolFixture 'partial-tool'
    $marker = Join-Path $fixture '.cache/tools/.undo-bootstrap-premake-incomplete'
    [IO.File]::WriteAllText($marker, 'Extraction in progress')
    $probe = Join-Path $fixture '.cache/tools/premake/premake5.exe'
    $result = Run-BootstrapTools $fixture
    Check ($result.Code -ne 0 -and $result.Text -match 'incomplete') 'Interrupted tool extraction was reported ready'
    Check ([IO.File]::ReadAllText($probe) -eq 'existing probe') 'Existing partial tool probe was modified'
    Check (Test-Path -LiteralPath $marker -PathType Leaf) 'Interrupted extraction marker was removed'
}

Write-Output "Result: $passed passed, $failed failed"
if ($failed) { exit 1 }
