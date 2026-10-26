# ReBirth audio diagnostic. Requires the user's original working installation.
param(
    [string]$EngineDll = 'C:\ReBirth RB-338\ReBirth.dll',
    [string]$OutputFolder = [Environment]::GetFolderPath('Desktop')
)
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Deze audioproef vereist Windows.'
}
if ($Host.Name -like '*ISE*') {
    throw 'Open gewone Windows PowerShell; deze proef gebruikt een consolevenster.'
}
$rbEngine = [IO.Path]::GetFullPath($EngineDll)
if ((Get-FileHash -LiteralPath $rbEngine -Algorithm SHA256).Hash -ne
    '4C3A6441A2CD50F797D99B1C5B6CF0AAF198C7AD8CA2648196B530C2E3E58B12') {
    throw 'De DLL wijkt af van de bekende originele ReBirth-versie. Er wordt niets gestart.'
}
if (-not (Test-Path -LiteralPath $OutputFolder -PathType Container)) {
    throw 'Kies een bestaande uitvoermap met -OutputFolder.'
}
$rbToken = [Guid]::NewGuid().ToString('N')
$rbTemp = Join-Path ([IO.Path]::GetTempPath()) ('ReBirth-audiotest-' + $rbToken)
$rbProbe = Join-Path $rbTemp 'rebirth-render-probe.exe'
$rbWave = Join-Path ([IO.Path]::GetFullPath($OutputFolder)) ('ReBirth-audiotest-' + $rbToken.Substring(0,8) + '.wav')
[IO.Directory]::CreateDirectory($rbTemp) | Out-Null

function Invoke-ReBirthAudioProbe([string]$ArgumentLine) {
    $rbStart = New-Object System.Diagnostics.ProcessStartInfo
    $rbStart.FileName = $rbProbe
    $rbStart.Arguments = $ArgumentLine
    $rbStart.WorkingDirectory = [IO.Path]::GetDirectoryName($rbEngine)
    $rbStart.UseShellExecute = $false
    $rbStart.CreateNoWindow = $false
    # Inherit the real console. Native diagnostic stderr stays out of
    # PowerShell's error-record conversion, and console input remains available.
    $rbProcess = [Diagnostics.Process]::Start($rbStart)
    if ($null -eq $rbProcess) { throw 'Het testprogramma kon niet worden gestart.' }
    try {
        $rbProcess.WaitForExit()
        return $rbProcess.ExitCode
    } finally {
        $rbProcess.Dispose()
    }
}

try {
    $rbUrl = 'https://raw.githubusercontent.com/deemarbus/Rebirth-338-VST-Windows/codex/windows-startup-compat/bridge-prototype/bin/windows-x86/rebirth-render-probe.exe'
    Invoke-WebRequest -UseBasicParsing -Uri $rbUrl -OutFile $rbProbe
    if ((Get-FileHash -LiteralPath $rbProbe -Algorithm SHA256).Hash -ne
        '10ECB61E88DA86840D40B0BD40D5C359E7FF92843106B4BFFFA9925F378CF1EF') {
        throw 'De downloadcontrole is mislukt. Het testprogramma wordt niet uitgevoerd.'
    }
    Write-Host 'Sluit ReBirth en eventuele andere ReWire-programmas eerst normaal af.'
    Write-Host 'Eerst volgt een korte controle zonder song. Bij een fout stopt de proef.'
    $rbCode = Invoke-ReBirthAudioProbe ('--no-panel-smoke "' + $rbEngine + '"')
    if ($rbCode -ne 0) {
        throw ('De eerste controle is gestopt met code ' + $rbCode + '. Geef de getoonde melding door.')
    }
    Write-Host ''
    Write-Host 'De audioproef begint nu. Laat dit venster open.'
    Write-Host '1. Open de originele ReBirth-app en laad hetzelfde demonummer dat eerder geluid gaf.'
    Write-Host '2. Zet Loop uit, stop en spoel terug naar het begin.'
    Write-Host '3. Ga terug naar dit venster en druk op Enter om tien seconden op te nemen.'
    Write-Host '4. Sluit ReBirth daarna normaal af via File > Quit; sla wijzigingen in de demo niet op.'
    Write-Host 'De host gebruikt 140 BPM. Tijdens deze proef gaat de uitvoer naar een WAV-bestand.'
    $rbCode = Invoke-ReBirthAudioProbe ('--capture "' + $rbEngine + '" "' + $rbWave + '"')
    if ($rbCode -ne 0) {
        throw ('De opname is nog niet geslaagd (code ' + $rbCode + '). Geef de getoonde melding door.')
    }
    if (-not (Test-Path -LiteralPath $rbWave -PathType Leaf)) {
        throw 'Het testprogramma meldde succes, maar het WAV-bestand ontbreekt.'
    }
    Write-Host ''
    Write-Host ('Opname voltooid: ' + $rbWave)
    Write-Host 'Luister naar het bestand, bijvoorbeeld in Ableton met Warp uit.'
    Write-Host 'Geef door of je het verwachte demonummer hoort. Dit is nog geen VST3.'
} finally {
    # Only this run's uniquely named helper is removed; the user's WAV remains.
    if (Test-Path -LiteralPath $rbProbe) {
        Remove-Item -LiteralPath $rbProbe -Force -ErrorAction SilentlyContinue
    }
    if ((Test-Path -LiteralPath $rbTemp) -and
        @(Get-ChildItem -LiteralPath $rbTemp -Force).Count -eq 0) {
        Remove-Item -LiteralPath $rbTemp -ErrorAction SilentlyContinue
    }
}
