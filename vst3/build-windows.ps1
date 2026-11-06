# Native Windows/MSVC build. Original ReBirth files are never used by CI.
param(
    [string]$BuildRoot = (Join-Path $PSScriptRoot '..\build-vst3'),
    [string]$SdkRoot = '',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Deze build vereist Windows en Visual Studio 2022 C++ build tools.'
}
$rbRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
[IO.Directory]::CreateDirectory($BuildRoot) | Out-Null
$rbLogs = Join-Path $BuildRoot 'logs'
[IO.Directory]::CreateDirectory($rbLogs) | Out-Null
function Invoke-Checked([string]$Program, [string[]]$ArgumentList) {
    & $Program @ArgumentList 2>&1 | Tee-Object -FilePath (Join-Path $rbLogs 'build-commands.log') -Append
    if ($LASTEXITCODE -ne 0) { throw ($Program + ' failed with exit ' + $LASTEXITCODE) }
}
function Invoke-Logged([string]$Program, [string[]]$ArgumentList, [string]$LogFile) {
    & $Program @ArgumentList 2>&1 | Tee-Object -FilePath $LogFile
    if ($LASTEXITCODE -ne 0) { throw ($Program + ' failed with exit ' + $LASTEXITCODE + '; see ' + $LogFile) }
}
$rbSdkCommit = '3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96'
if (-not $SdkRoot) { $SdkRoot = Join-Path $BuildRoot 'vst3sdk' }
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
if (-not (Test-Path -LiteralPath $SdkRoot)) {
    Invoke-Checked 'git' @('clone', '--no-checkout', 'https://github.com/steinbergmedia/vst3sdk.git', $SdkRoot)
    Invoke-Checked 'git' @('-C', $SdkRoot, 'checkout', '--detach', $rbSdkCommit)
}
$rbActualSdk = & git -C $SdkRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $rbActualSdk -ne $rbSdkCommit) { throw 'SDK revision mismatch; existing SDK left unchanged.' }
Invoke-Checked 'git' @('-C', $SdkRoot, 'submodule', 'update', '--init', '--depth', '1', 'base', 'cmake', 'pluginterfaces', 'public.sdk')
$rbExpectedModules = @{
    'base' = 'fcf9da0bd27a16f7f03773a3a39822f28f5c8477'
    'cmake' = '054c9143cbb8d47fc4694e473f2ee3b4d951a8f5'
    'pluginterfaces' = '4f547e8e102b47de4a8b8aaf343c73b700786372'
    'public.sdk' = '586dc5e6c8012c3e4b01c79389375cbe96bdb1da'
}
foreach ($rbModule in $rbExpectedModules.Keys) {
    $rbRevision = & git -C (Join-Path $SdkRoot $rbModule) rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $rbRevision -ne $rbExpectedModules[$rbModule]) { throw ('SDK submodule mismatch: ' + $rbModule) }
}
foreach ($rbArch in @('Win32', 'x64')) {
    $rbTests = Join-Path $BuildRoot ('ipc-' + $rbArch)
    Invoke-Checked 'cmake' @('-S', (Join-Path $PSScriptRoot 'tests'), '-B', $rbTests, '-G', 'Visual Studio 17 2022', '-A', $rbArch)
    Invoke-Checked 'cmake' @('--build', $rbTests, '--config', 'Release', '--parallel', '4')
    Invoke-Logged 'ctest' @('--test-dir', $rbTests, '-C', 'Release', '--output-on-failure', '--no-tests=error') (Join-Path $rbLogs ('ipc-' + $rbArch + '.log'))
}
$rbWorkerBuild = Join-Path $BuildRoot 'worker-x86'
Invoke-Checked 'cmake' @('-S', (Join-Path $PSScriptRoot 'worker'), '-B', $rbWorkerBuild, '-G', 'Visual Studio 17 2022', '-A', 'Win32', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
Invoke-Checked 'cmake' @('--build', $rbWorkerBuild, '--config', 'Release', '--parallel', '4')
Invoke-Logged 'ctest' @('--test-dir', $rbWorkerBuild, '-C', 'Release', '--output-on-failure', '--no-tests=error') (Join-Path $rbLogs 'worker-transport.log')
$rbPluginBuild = Join-Path $BuildRoot 'plugin-x64'
Invoke-Checked 'cmake' @('-S', (Join-Path $PSScriptRoot 'plugin'), '-B', $rbPluginBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64', ('-DVST3_SDK_ROOT=' + $SdkRoot), '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded', '-DSMTG_USE_STATIC_CRT=ON', '-DSMTG_RUN_VST_VALIDATOR=OFF', '-DSMTG_CREATE_PLUGIN_LINK=OFF', '-DSMTG_CREATE_MODULE_INFO=OFF')
Invoke-Checked 'cmake' @('--build', $rbPluginBuild, '--config', 'Release', '--target', 'ReBirthBridge', 'validator', 'bridge-adapter-test', '--parallel', '4')
Invoke-Logged 'ctest' @('--test-dir', $rbPluginBuild, '-C', 'Release', '-R', '^bridge-adapter$', '--output-on-failure', '--no-tests=error') (Join-Path $rbLogs 'adapter.log')
$rbWorker = @(Get-ChildItem -LiteralPath $rbWorkerBuild -Filter 'rebirth-bridge-worker.exe' -File -Recurse)
$rbPlugin = @(Get-ChildItem -LiteralPath $rbPluginBuild -Filter 'ReBirthBridge.vst3' -File -Recurse)
$rbValidator = @(Get-ChildItem -LiteralPath $rbPluginBuild -Filter 'validator.exe' -File -Recurse)
if ($rbWorker.Count -ne 1 -or $rbPlugin.Count -ne 1 -or $rbValidator.Count -ne 1) { throw 'Missing or ambiguous build products.' }
$rbBundle = $rbPlugin[0].Directory.Parent.Parent.FullName
if ([IO.Path]::GetFileName($rbBundle) -ne 'ReBirthBridge.vst3') { throw 'Unexpected VST3 bundle layout.' }
Invoke-Logged 'python' @((Join-Path $PSScriptRoot 'verify-pe.py'), $rbPlugin[0].FullName, $rbWorker[0].FullName) (Join-Path $rbLogs 'pe-exports.log')
Invoke-Logged $rbValidator[0].FullName @('-selftest') (Join-Path $rbLogs 'validator-selftest.log')
Invoke-Logged $rbValidator[0].FullName @($rbBundle) (Join-Path $rbLogs 'validator-plugin.log')
Invoke-Checked 'git' @('-C', $SdkRoot, 'diff', '--exit-code')
foreach ($rbModule in $rbExpectedModules.Keys) { Invoke-Checked 'git' @('-C', (Join-Path $SdkRoot $rbModule), 'diff', '--exit-code') }
if ($Package) {
    $rbDist = Join-Path $rbRepo 'dist\windows'
    if (Test-Path -LiteralPath $rbDist) { throw 'Package directory already exists; choose a clean build checkout.' }
    [IO.Directory]::CreateDirectory($rbDist) | Out-Null
    Copy-Item -LiteralPath $rbBundle -Destination $rbDist -Recurse
    Copy-Item -LiteralPath $rbWorker[0].FullName -Destination $rbDist
    Copy-Item -LiteralPath $rbLogs -Destination (Join-Path $rbDist 'verification') -Recurse
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third-party-notices') -Destination $rbDist -Recurse
    $rbSourceCommit = & git -C $rbRepo rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Could not determine source revision.' }
    $rbFiles = @(Get-ChildItem -LiteralPath $rbDist -Recurse -File | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($rbDist.Length + 1).Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(); bytes = $_.Length }
    })
    [ordered]@{
        format = 1; source_commit = $rbSourceCommit; sdk_commit = $rbSdkCommit
        compiler = 'Visual Studio 2022 MSVC'; validator = 'passed'; engine_included = $false
        ableton_tested = $false; sample_rate = 44100
        host_sample_rates = @(44100, 48000, 88200, 96000, 176400, 192000)
        resampler = 'libsamplerate 0.2.2'; tempo_range_bpm = @(20, 500)
        latency_engine_samples = 4096
        files = $rbFiles
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $rbDist 'manifest.json') -Encoding UTF8
}
Write-Host 'MSVC build, IPC fixtures, PE exports and full SDK validator completed.'
Write-Host 'This does not verify realtime ReBirth music or Ableton compatibility.'
