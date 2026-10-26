param(
    [string]$OriginalFolder = 'C:\ReBirth RB-338'
)
$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Launcher.cs'))
$harness = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'TestHarness.cs'))
Add-Type -TypeDefinition ($source + [Environment]::NewLine + $harness)
$exe = Join-Path $OriginalFolder 'Rebirth.exe'
$dll = Join-Path $OriginalFolder 'ReBirth.dll'
$before = @((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash,
            (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash)
$passed = [Collections.Generic.List[string]]::new()
$launcherScript = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Start-ReBirth.ps1'))
$embedded = [regex]::Match($launcherScript, "(?s)Add-Type -TypeDefinition @'\r?\n(.*?)\r?\n'@")
if (-not $embedded.Success -or $embedded.Groups[1].Value.TrimEnd() -cne $source.TrimEnd()) {
    throw 'Embedded launcher source differs from Launcher.cs'
}
$parseTokens = $null
$parseErrors = $null
[Management.Automation.Language.Parser]::ParseInput($launcherScript, [ref]$parseTokens, [ref]$parseErrors) | Out-Null
if ($parseErrors.Count -ne 0) { throw ('Launcher PowerShell syntax errors: ' + ($parseErrors -join ', ')) }
$passed.Add('PowerShell script parses; embedded C# source matches Launcher.cs')
foreach ($name in [RebirthStartupCompat.Tests.Harness]::Run($exe)) { $passed.Add($name) }
$testFolder = Join-Path $PSScriptRoot ('input-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testFolder | Out-Null
try {
    Copy-Item -LiteralPath $exe,$dll -Destination $testFolder
    $copyExe = Join-Path $testFolder 'Rebirth.exe'
    $copyDll = Join-Path $testFolder 'ReBirth.dll'
    foreach ($target in @($copyExe, $copyDll)) {
        $bytes = [IO.File]::ReadAllBytes($target)
        $bytes[500] = $bytes[500] -bxor 1
        [IO.File]::WriteAllBytes($target, $bytes)
        try {
            [RebirthStartupCompat.Launcher]::Start($copyExe) | Out-Null
            throw 'Unexpected success with modified input'
        } catch {
            if ($_.Exception.ToString() -notmatch 'unsupported SHA-256') { throw }
        }
        $passed.Add('modified ' + [IO.Path]::GetFileName($target) + ': refused before native APIs')
        Copy-Item -LiteralPath (Join-Path $OriginalFolder ([IO.Path]::GetFileName($target))) -Destination $target -Force
    }
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        try {
            [RebirthStartupCompat.Launcher]::Start($copyExe) | Out-Null
            throw 'Unexpected success on a non-Windows host'
        } catch {
            if ($_.Exception.ToString() -notmatch '64-bit Windows PowerShell') { throw }
        }
        $passed.Add('Linux host: refuses native process execution after valid input checks')
    }
} finally {
    Remove-Item -LiteralPath $testFolder -Recurse -Force
}
$after = @((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash,
           (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash)
if (($before -join ',') -cne ($after -join ',')) { throw 'Original files changed' }
$passed.Add('original EXE and DLL byte hashes unchanged')
[pscustomobject]@{
    status = 'passed'
    checks = $passed.Count
    passed = $passed.ToArray()
    scope = 'Compiled C#; simulated Windows process API lifecycle; real file hash validation. Native Windows APIs and application/audio behavior were NOT tested.'
    original_sha256 = $after
} | ConvertTo-Json -Depth 4
