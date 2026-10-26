# ReBirth RB-338 op Windows

Doel: ReBirth gebruiken op modern Windows en uiteindelijk in Ableton Live 11/12.
Er is nog geen werkende VST3 gebouwd. De aangeleverde macOS-plug-ins bevatten
gecompileerde Mach-O-programma's, geen broncode die rechtstreeks voor Windows
kan worden gebouwd.

## Bevestigd: zelfstandig starten en geluid op Windows 10

De gebruiker heeft op 26 oktober 2026 bevestigd dat ReBirth met de nieuwe
launcher start en dat het afspelen van een demonummer hoorbaar werkt. Na normaal
afsluiten via File > Quit start ook het originele `Rebirth.exe` weer met geluid,
zonder PowerShell en zonder opnieuw vast te lopen bij de eerste benchmark/Help.
De launcher hoeft op deze installatie dus niet bij iedere start te worden gebruikt.

De eerdere blokkade ontstond bij de automatische Windows Help-aanroep na de
eerste benchmark. Windows opende een Microsoft-pagina en er verscheen `(9009)`.

Een eerdere testkopie met vijf gewijzigde bytes in het EXE-bestand is **ingetrokken**.
Die wijziging verstoort ReBirths eigen bestandscontrole, slaat daardoor de
audio-initialisatie over en verandert de afhandeling van opdrachten. Gebruik die
kopie en de eerdere scripts niet verder. Het originele bestand is behouden.

[De nieuwe experimentele launcher](windows-startup/Start-ReBirth.ps1) houdt de
programmabestanden intact. Hij start het origineel geschorst, controleert de
versie en de instructies, en slaat alleen in dat nieuwe proces de automatische
eerste Help-aanroep over. De bestandscontrole en oorspronkelijke mediacontrole
blijven actief. Windows-systeembestanden en het register worden niet gewijzigd.

De start en hoorbare geluidsweergave zijn nu door de gebruiker op Windows 10
getest. De controles in de Linux-cloud bevestigen daarnaast invoercontrole,
foutafhandeling en specifieke programma-invarianten. Ableton-integratie,
transportkoppeling, VST3-projectstatus en meerdere plug-ininstanties zijn nog
niet gevalideerd.

## Volgende stap: de oude engine-interface controleren

De oude engine is een 32-bits DLL met een ReWire 1-interface. Die kan niet
rechtstreeks in het 64-bits proces van Ableton Live worden geladen. De te
onderzoeken route is een afzonderlijke 32-bits helper, met later een 64-bits
VST3 die audio en transport met die helper uitwisselt.

Eerst moet een kleine testhost de precieze oude interface en echte
audio-overdracht aantonen. De aangeleverde DLL gebruikt een andere
audio-structuur dan latere ReWire-headers; die headers kunnen niet blind worden
overgenomen. Een VST3-wrapper volgt pas na een geslaagde engineproef.

De [eerste diagnostiek](bridge-prototype/README.md) en
[analyse van de oude audiostructuur](bridge-prototype/legacy-drive-notes.md)
staan bij de broncode. De metadata-aanroepen zijn onder Wine uitgevoerd en
leveren versie 8 en de 18 verwachte kanalen op. Een afzonderlijke proef heeft
daar ook Open, tien Idle-aanroepen, de sluitingscontrole, Close en het ontladen
van de DLL succesvol doorlopen. Dat bewijst nog geen audio-overdracht naar een
andere host; de renderproef en een Windows-test van de helper blijven nodig.

De [renderhelper](bridge-prototype/render-README.md) heeft inmiddels ook negen
echte renderaanroepen zonder song onder Wine doorlopen, met intacte buffers
en normale afsluiting. De volgende proef is een opname van tien seconden met
een echt geladen demonummer op Windows. De
[PowerShell-starter](bridge-prototype/Start-Audio-Test.ps1) en
[Windows-uitleg](bridge-prototype/TESTEN-WINDOWS.md) staan klaar. Er is nog geen
geladen-song-opname of VST3 in Ableton gevalideerd.

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
