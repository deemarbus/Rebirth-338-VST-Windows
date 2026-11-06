# Experimentele Windows VST3-brug

De gebruiker heeft op 6 november 2026 bevestigd dat de eerdere x86-testhost
een WAV met hoorbare ReBirth-muziek maakt. Deze map bouwt daarop voort met
een blijvende x86-helper en een aparte x64-VST3 voor Ableton Live 11/12.
Een geslaagde opname is nog geen test van deze nieuwe realtime koppeling.

## Grenzen van deze eerste versie

- Windows x64, **44,1 kHz**, **20-500 BPM** (volgt Live), stereo en één actieve instantie.
- Het originele ReBirth-paneel blijft nodig om een song te laden en op te slaan.
- **Loop uit** in ReBirth en Live. Start in Live vanaf **maat 1**.
- Play/Stop komen uit Live. De Play-knop van ReBirth bestuurt Live niet.
- Geen MIDI-nootbesturing, parameterautomatisering of songopslag in het Live-project.
- Geen offline export: neem de uitvoer in realtime op een audiospoor op.
- De plug-in meldt 4.096 samples latentie (ongeveer 92,9 ms) aan de host.
  De werking van Live's latentiecompensatie moet nog op Windows worden getest.

De plug-in toont ontbrekende verbindingen, ongeschikte instellingen en
ontbrekende audioblokken in zijn venster. Zonder een geschikte verbinding
produceert hij stilte. Dat is geen geslaagde test van muziekweergave.
Slechts één instantie kan de engine gebruiken; een tweede krijgt een melding
dat de engine al in gebruik is.

## Bouw en controles

Op Windows met Visual Studio 2022 C++ build tools, Windows SDK, CMake, Git en
Python:

```powershell
.\vst3\build-windows.ps1
```

Het script haalt de vastgelegde officiële VST3 SDK 3.8.1 op en controleert de
revisies van de gebruikte submodules. Het bouwt de x86-helper en x64-plug-in
met MSVC en voert vervolgens uit:

1. Tests van het echte IPC-formaat en de rings op x86 en x64.
2. Tests van de transportstatus zonder originele engine.
3. Tests van de 64-frame-adapter met verschillende hostbuffergroottes.
4. Controles van de PE-architecturen en VST3-exportfuncties.
5. De volledige officiële validator, inclusief diens zelftest.

Deze controles gebruiken geen originele ReBirth-bestanden. De SDK blijft
ongewijzigd. `-Package` verzamelt alleen na geslaagde controles de eigen
programmabestanden, runtimevermeldingen en testlogs in `dist/windows`.
De GitHub-workflow kan die bestanden op dezelfde broncodetak publiceren voor
directe downloads. De manifest vermeldt bronrevisie en bestandshashes.

Een lokale MinGW-crossbuild gebruikt een andere bouwroute met alleen de
benodigde, ongewijzigde SDK-bronbestanden. Een x64-PE met juiste exports
bewijst nog geen geslaagde Windows-validator of Ableton-test. Kijk voor de
werkelijk uitgevoerde controles in [verification/README.md](../verification/README.md).

## Windows-proef na een geslaagde native build

Gebruik de bij elkaar horende plug-in en helper van dezelfde build.

1. Sluit bestaande ReBirth- en ReWire-sessies normaal af. Laat de benodigde
   originele CD/ISO gekoppeld.
2. Start de helper en laat zijn console open:

   ```powershell
   .\rebirth-bridge-worker.exe 'C:\ReBirth RB-338\ReBirth.dll'
   ```

3. Open het originele ReBirth, laad de eerder hoorbare demo en zet Loop uit.
4. Zet Live op 44,1 kHz en een tempo tussen 20 en 500 BPM en zet Live Loop uit. Plaats de complete
   `ReBirthBridge.vst3`-map in de VST3-map die Live scant, scan opnieuw en laad
   ReBirth Bridge op een MIDI-spoor. Open het plug-invenster en controleer de status.
5. Zet de cursor van Live op maat 1 en druk in **Live** op Play. Controleer
   hoorbare muziek en de melding over ontbrekende audioblokken. Stop en start
   steeds opnieuw vanaf maat 1; springen tijdens afspelen is nog niet ondersteund.
6. Neem een korte passage in realtime op een audiospoor op. Vergelijk de
   inhoud met de eerdere demo-opname en controleer starten/stoppen en timing.
7. Sluit af door Live te stoppen en ReBirth via **File > Quit** af te sluiten.
   De helper sluit zijn eigen engine daarna normaal. Sluit vervolgens de
   plug-in. Bij opnieuw beginnen: verwijder eerst de vorige plug-ininstantie,
   start een nieuwe helper en laad de plug-in opnieuw.

De helper wijzigt of installeert geen originele bestanden, songs, registerwaarden
of systeembestanden. De normale mediacontrole blijft actief. Het stoppen of
verwijderen van de plug-in beëindigt het originele paneel niet geforceerd.
