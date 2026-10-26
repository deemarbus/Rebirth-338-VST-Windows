# ReBirth RB-338 op Windows

Doel: ReBirth gebruiken op modern Windows en uiteindelijk in Ableton Live 11/12.
Er is nog geen werkende VST3 gebouwd. De aangeleverde macOS-plug-ins bevatten
gecompileerde Mach-O-programma's, geen broncode die rechtstreeks voor Windows
kan worden gebouwd.

## Huidige stap: de zelfstandige Windows-app laten starten

De originele Windows-app bereikt op de Windows 10-pc de eerste benchmark.
Daarna roept ReBirth de oude Windows Help-functie aan; Windows opent vervolgens
een Microsoft-pagina en er verschijnt een melding `(9009)`.

Een eerdere testkopie met vijf gewijzigde bytes in het EXE-bestand is **ingetrokken**.
Die wijziging verstoort ReBirths eigen bestandscontrole, slaat daardoor de
audio-initialisatie over en verandert de afhandeling van opdrachten. Gebruik die
kopie en de eerdere scripts niet verder. Het originele bestand is behouden.

[De nieuwe experimentele launcher](windows-startup/Start-ReBirth.ps1) houdt de
programmabestanden intact. Hij start het origineel geschorst, controleert de
versie en de instructies, en slaat alleen in dat nieuwe proces de automatische
eerste Help-aanroep over. De bestandscontrole en oorspronkelijke mediacontrole
blijven actief. Windows-systeembestanden en het register worden niet gewijzigd.

**Dit is een kandidaat voor een Windows-test.** De controles in de Linux-cloud
bevestigen invoercontrole, foutafhandeling en specifieke programma-invarianten.
Ze bewijzen nog geen geslaagde Windows-start of hoorbaar geluid.

## Testen op Windows 10

1. Gebruik de complete originele map `C:\ReBirth RB-338`, met de oorspronkelijke
   `Rebirth.exe` en bijbehorende `ReBirth.dll`. Gebruik de ingetrokken testmap niet.
2. Sluit een eventuele andere ReBirth-instantie zelf af en voldoe aan de normale
   vereisten voor de oorspronkelijke CD/ISO.
3. Sla `windows-startup/Start-ReBirth.ps1` lokaal op. Open gewone **64-bits Windows
   PowerShell** en voer het script vanuit zijn map uit:

   ```powershell
   .\Start-ReBirth.ps1
   ```

   Als het lokaal opgeslagen script door het uitvoeringsbeleid wordt geweigerd,
   kan de leesbare inhoud in een nieuw PowerShell-venster worden geplakt. De
   launcher wijzigt het uitvoeringsbeleid niet en vraagt geen administratorrechten.

4. Voer een eventuele benchmark uit. Het beoogde vervolg is Preferences en daarna
   het songvenster. Kies een geluidsuitgang, open een meegeleverd demonummer en
   controleer of afspelen hoorbaar werkt.
5. Sluit ReBirth normaal af en start het origineel opnieuw. Controleer of de
   voorkeuren zijn bewaard. De automatische Help-stap hoort alleen bij nieuwe
   voorkeuren; het oude Help-menu zelf is niet aangepast.

Een andere installatiemap kan expliciet worden opgegeven:

```powershell
.\Start-ReBirth.ps1 -Executable 'D:\Audio\ReBirth\Rebirth.exe'
```

Het script weigert afwijkende bestanden voordat een proces wordt gestart. De
bekende originele EXE heeft SHA-256
`6A651E8164B8444A5F0639CB6A2FEF2AA43C5500BD9C5288627C2030B047F876`.
De oude onbruikbare testkopie heeft SHA-256
`2A587E62EF1919522AC829EFA94DAE22A1D9303CAE742F4EB783625DDC51DCD8`.

## Ontwikkelcontroles

De launcher vereist Windows PowerShell 5.1 of later en een 64-bits proces.
De C#-bron staat afzonderlijk in `windows-startup/Launcher.cs` en is identiek
ingebed in het zelfstandige PowerShell-script.

De tests gebruiken gesimuleerde Windows-aanroepen; zij starten ReBirth niet:

```powershell
.\windows-startup\Test-Launcher.ps1 -OriginalFolder 'C:\ReBirth RB-338'
```

De aanvullende x86-emulatie vereist Python, `pefile==2024.8.26` en
`unicorn==2.1.4` en leest het oorspronkelijke EXE-bestand:

```text
python verification/verify_file_selfcheck.py /pad/naar/originele/Rebirth.exe
```

Deze controle voert de echte checksumlus, beslissende initialisatietak en
opdrachtberekening uit voor drie gevallen: origineel, ingetrokken bestandspatch
en tijdelijke geheugenwijziging met ongewijzigd bestand. Bestandstoegang en de
audiofunctie zelf worden daarbij niet op Windows uitgevoerd.

De repository bevat geen originele ReBirth-binaries, ISO, engine, songs of
macOS-programmabestanden. Voor een latere VST3-koppeling moeten eerst de
zelfstandige werking, audio en de oude engine-/ReWire-koppeling worden gevalideerd.
