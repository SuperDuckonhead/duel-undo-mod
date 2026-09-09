param(
    [string]$RuntimeRoot='F:/MyCardLibrary/ygopro',
    [string]$Cases = '10,100,1000',
    [string]$OutFile = '',
    [ValidateRange(1,100)][int]$UndoCount = 20
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'UndoMeasurementSafety.ps1')
$runtime=[IO.Path]::GetFullPath($RuntimeRoot)
if(-not (Test-Path -LiteralPath (Join-Path $runtime 'cards.cdb') -PathType Leaf)){throw "Missing runtime database: $runtime/cards.cdb"}
$caseNumbers=@($Cases.Split(',') | ForEach-Object {if($_ -notmatch '^[1-9][0-9]{0,4}$'){throw 'Cases must be distinct positive response counts'}; $n=[int]$_;if($n -gt 10000){throw 'Maximum case size is 10000'};$n})
if(@($caseNumbers | Select-Object -Unique).Count -ne $caseNumbers.Count){throw 'Duplicate case size'}
if(-not $OutFile){$OutFile='out/tests/HostPerformance/runs/'+[guid]::NewGuid().ToString('N')+'/host.json'}
$output=if([IO.Path]::IsPathRooted($OutFile)){[IO.Path]::GetFullPath($OutFile)}else{[IO.Path]::GetFullPath((Join-Path $repoRoot $OutFile))}
$outRoot=Join-Path $repoRoot 'out/tests/HostPerformance'
$build=Join-Path $outRoot 'Release'
$bot=Join-Path $repoRoot 'out/bot/Release/WindBot-undo.exe'
if([IO.Path]::GetExtension($output) -ne '.json'){throw 'Output must be a .json file beneath this checkout out directory'}
$writePaths=@('','.configure.log','.build.log','.run.log','.samples.jsonl','.inputs.json') | ForEach-Object {$output+$_}
function AssertWritePaths {
    foreach($path in $writePaths){Assert-MeasurementOutputPath $path $outRoot}
    # CMake and make write many nested outputs: reject any existing link in the
    # whole build tree before invoking them, rather than only checking -B.
    Assert-MeasurementOutputTree $build $outRoot
}
AssertWritePaths
foreach($path in $writePaths){if(Test-Path -LiteralPath $path){throw 'Output already exists; prior measurement evidence is preserved'}}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output)) | Out-Null
$profile=Get-Content -LiteralPath (Join-Path $repoRoot 'build-profile.json') -Raw | ConvertFrom-Json
$cmake=Join-Path $repoRoot $profile.dependencies.cmake
$compiler=Join-Path $repoRoot $profile.compiler.path
$make=Join-Path $repoRoot $profile.dependencies.make
$toolRoot=Split-Path (Split-Path $compiler)
$cmakeRoot=Split-Path (Split-Path $cmake)
$targets=@('undo_host_measure_tests','undo_rebuilder','undo_driver','undo_resources','undo_ocgcore','undo_lua')
function InputManifest {
    # Deliberately overinclude source/header trees and installed toolchain files.
    # Compiler .d files are checked after compilation against this manifest.
    $directories=@('client/ocgcore','client/lua/src','client/irrlicht/include','client/sqlite3','client/event/include','client/lzma/src/liblzma/api','tools/patches') | ForEach-Object {Join-Path $repoRoot $_}
    $directories+=@(Join-Path $repoRoot 'out/bot/Release')
    $directories+=@('bin','include','lib','x86_64-w64-mingw32') | ForEach-Object {Join-Path $toolRoot $_}
    $directories+=@((Join-Path $cmakeRoot 'bin'),(Join-Path $cmakeRoot 'share/cmake-4.4'))
    $files=@('CMakeLists.txt','sources.lock.json','build-profile.json','tests/undo_host_measure_tests.cpp','tests/core_test_globals.cpp','tests/test_support.h','tests/measurement_process.h') | ForEach-Object {Join-Path $repoRoot $_}
    $files+=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'tests') -File -Filter '*.cmake' | ForEach-Object FullName)
    $files+=@('Measure-UndoHost.ps1','UndoMeasurementSafety.ps1','Apply-LuaUndoPatch.ps1') | ForEach-Object {Join-Path $PSScriptRoot $_}
    $files+=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'client/gframe') -Recurse -File | Where-Object {$_.Extension -ne '.cpp' -and $_.Extension -ne '.c'} | ForEach-Object FullName)
    foreach($target in $targets){
        $dependency=Join-Path $build ('CMakeFiles/'+$target+'.dir/DependInfo.cmake')
        foreach($match in [regex]::Matches([IO.File]::ReadAllText($dependency),'"([^"]+[.](?:c|cpp))"')){$files+=$match.Groups[1].Value}
    }
    $files+=$bot
    $files+=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'client/bin/release') -File -Filter '*.lib' | ForEach-Object FullName)
    Get-MeasurementManifest $directories $files $repoRoot
}
function RecipeManifest {
    $files=@((Join-Path $build 'CMakeCache.txt'),(Join-Path $build 'Makefile'))
    foreach($name in $targets){
        $dir=Join-Path $build ('CMakeFiles/'+$name+'.dir')
        $files+=@(Get-ChildItem -LiteralPath $dir -File | Where-Object {$_.Name -in @('flags.make','build.make','link.txt','DependInfo.cmake') -or $_.Extension -eq '.rsp'} | ForEach-Object FullName)
    }
    $files+=@(Get-ChildItem -LiteralPath (Join-Path $build 'CMakeFiles') -File -Filter '*.cmake' | ForEach-Object FullName)
    foreach($version in Get-ChildItem -LiteralPath (Join-Path $build 'CMakeFiles') -Directory | Where-Object {$_.Name -match '^[0-9]+[.]'}){
        $files+=@(Get-ChildItem -LiteralPath $version.FullName -File -Filter '*.cmake' | ForEach-Object FullName)
    }
    Get-MeasurementManifest @() $files $repoRoot
}
function VerifyCompilerDependencies($Manifest) {
    $known=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($file in $Manifest.files){[void]$known.Add([IO.Path]::GetFullPath((Join-Path $repoRoot $file.path)))}
    $dependencies=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $count=0
    foreach($name in $targets){
        $files=@(Get-ChildItem -LiteralPath (Join-Path $build ('CMakeFiles/'+$name+'.dir')) -Recurse -File -Filter '*.obj.d')
        if(-not $files.Count){throw "No compiler dependency evidence for $name"}
        foreach($file in $files){
            ++$count
            $text=[IO.File]::ReadAllText($file.FullName) -replace '\\\r?\n',' '
            $separator=$text.IndexOf(': ')
            if($separator -lt 0){throw "Invalid compiler dependency file: $($file.FullName)"}
            foreach($token in [regex]::Matches($text.Substring($separator+2),'(?:\\.|[^\s])+')){
                $path=$token.Value -replace '\\([ #\\])','$1'
                if(-not [IO.Path]::IsPathRooted($path)){$path=Join-Path $build $path}
                $path=[IO.Path]::GetFullPath($path)
                if(-not $known.Contains($path)){throw "Actual compiler input missing from manifest: $path"}
                [void]$dependencies.Add($path)
            }
        }
    }
    [PSCustomObject]@{dependencyFiles=$count;uniqueInputs=$dependencies.Count}
}
function RunNative([string]$Program,[string[]]$Arguments,[string]$Log) {
    AssertWritePaths
    $savedPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $Program @Arguments > $Log 2>&1
        $nativeExit=$LASTEXITCODE
    } finally {$ErrorActionPreference=$savedPreference}
    if($nativeExit -ne 0){throw "Measurement command failed ($nativeExit); see $Log"}
}
function ToolVersion([string]$Program) {
    $text=@(& $Program --version 2>&1)
    if($LASTEXITCODE -ne 0){throw "Cannot read tool version: $Program"}
    return ($text -join "`n")
}
Push-Location -LiteralPath $repoRoot
try {
    $commit=(& git rev-parse HEAD).Trim()
    if($LASTEXITCODE -ne 0){throw 'Cannot resolve source commit'}
    $dirty=[bool](& git status --porcelain)
    if($LASTEXITCODE -ne 0){throw 'Cannot read source dirty state'}
    Write-Host 'Configuring before fingerprinting actual sources, headers, libraries and tools.'
    $versions=[ordered]@{compiler=(ToolVersion $compiler);cmake=(ToolVersion $cmake);make=(ToolVersion $make);linker=(ToolVersion (Join-Path $toolRoot 'bin/ld.lld.exe'));archiver=(ToolVersion (Join-Path $toolRoot 'bin/llvm-ar.exe'))}
    $configure=@('-S',$repoRoot,'-B',$build,'-G','MinGW Makefiles','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_CXX_COMPILER=$compiler","-DCMAKE_MAKE_PROGRAM=$make","-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$build","-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$build","-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$build")
    RunNative $cmake $configure ($output+'.configure.log')
    $inputs=InputManifest
    $recipes=RecipeManifest
    RunNative $cmake @('--build',$build,'--target','undo_host_measure_tests','--clean-first','-j','2') ($output+'.build.log')
    if((InputManifest).sha256 -ne $inputs.sha256 -or (RecipeManifest).sha256 -ne $recipes.sha256){throw 'Measured inputs changed during configure/compilation; rerun after edits stabilize'}
    if((& git rev-parse HEAD).Trim() -ne $commit){throw 'Source HEAD changed during compilation; refusing inconsistent commit metadata'}
    $dependencyEvidence=VerifyCompilerDependencies $inputs
    $binary=Join-Path $build 'undo_host_measure_tests.exe'
    $artifacts=Get-MeasurementManifest @() (@($binary)+@($targets | Where-Object {$_ -ne 'undo_host_measure_tests'} | ForEach-Object {Join-Path $build ('lib'+$_+'.a')})) $repoRoot
    $binaryDigest=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
    $raw=$output+'.samples.jsonl'
    Write-Host 'Compiled inputs verified. Measuring actual host/core/N3/private ChainBurn transactions.'
    $manifest=[ordered]@{schemaVersion=1;scope='Overincluded file inputs, actual compiler dependency coverage, generated build recipes and linked artifacts; excludes runtime OS implementation and unrecorded ambient machine state';inputs=$inputs;recipes=$recipes;artifacts=$artifacts;compilerDependencies=$dependencyEvidence;toolVersions=$versions;configureArguments=$configure}
    AssertWritePaths
    [IO.File]::WriteAllText($output+'.inputs.json',($manifest | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    RunNative $binary @($bot,$runtime,($caseNumbers -join ','),$raw,[string]$UndoCount) ($output+'.run.log')
    if((InputManifest).sha256 -ne $inputs.sha256 -or (RecipeManifest).sha256 -ne $recipes.sha256){throw 'Measured inputs changed during execution; refusing to publish a mixed-input record'}
    if((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $binaryDigest){throw 'Measured executable changed during execution'}
    if((& git rev-parse HEAD).Trim() -ne $commit){throw 'Source HEAD changed during execution; refusing inconsistent commit metadata'}
    $rows=@(Get-Content -LiteralPath $raw | ForEach-Object {$_ | ConvertFrom-Json})
    $samples=@($rows | Where-Object {$_.kind -in @('success','failure') -and $_.phase -eq 'settled'})
    foreach($n in $caseNumbers){
        if(@($rows|Where-Object {$_.kind -eq 'scale-complete' -and $_.requestedResponses -eq $n -and $_.descendantProcessCount -eq 0}).Count -ne 1){throw 'Missing descendant cleanup/scale completion'}
        $fixture=@($rows|Where-Object {$_.kind -eq 'fixture' -and $_.requestedResponses -eq $n})
        if($fixture.Count -ne 1){throw 'Missing actual fixture metadata'}
        foreach($kind in @('success','failure')){
            $selected=@($samples|Where-Object {$_.requestedResponses -eq $n -and $_.kind -eq $kind})
            if($selected.Count -ne $UndoCount -or @($selected.iteration|Select-Object -Unique).Count -ne $UndoCount){throw 'Missing or duplicate real transaction sample'}
            if(@($selected|Where-Object {$_.descendantProcessCount -ne $fixture[0].stableDescendantProcessCount -or $_.candidatePid -ne 0 -or $_.retainedPid -ne 0}).Count){throw 'Unreleased candidate at stable sample'}
        }
    }
    $result=[ordered]@{
        schemaVersion=3; scope='actual in-process host/core/N3/private ChainBurn, no GUI/client TCP timing; stage-sampled aggregate peaks and per-process lifetime OS peaks'
        sourceCommit=$commit; sourceDirty=$dirty; inputManifestSha256=$inputs.sha256; buildRecipesSha256=$recipes.sha256; binarySha256=$binaryDigest
        provenanceManifestFile=[IO.Path]::GetFileName($output)+'.inputs.json'; provenanceManifestFileSha256=(Get-FileHash -LiteralPath ($output+'.inputs.json') -Algorithm SHA256).Hash.ToLowerInvariant()
        compilerDependencies=$dependencyEvidence;toolVersions=$versions
        measuredAtUtc=[DateTime]::UtcNow.ToString('o'); runtimeRoot=$runtime
        compiler=$profile.compiler; resourceDigest=$rows[0].resourceDigest; fixtures=@($rows|Where-Object kind -eq 'fixture'); samples=$rows
    }
    AssertWritePaths
    [IO.File]::WriteAllText($output,($result | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    Write-Host "Measured $($caseNumbers -join ',') requested response scales, $UndoCount successes and failures per case. $output"
    Write-Host 'Actual target indices and cleanup successes are reported separately. GUI/TCP transport timing is outside this measurement.'
} finally {Pop-Location}
