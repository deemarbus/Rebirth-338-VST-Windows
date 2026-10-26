# Eerste audioproef op Windows

De zelfstandige ReBirth werkt inmiddels met geluid. Deze proef controleert of
een aparte host de muziek uit dezelfde originele engine kan ophalen. Het is
een tussenstap naar een 64-bits VST3 voor Ableton Live 11/12.

Gebruik de werkende originele installatie in `C:\ReBirth RB-338` en laat de
normaal gebruikte ISO gekoppeld. Sluit ReBirth voor het starten van de proef
normaal af. Het testprogramma bevat alleen onze eigen helper en de gebruikelijke
runtime; de originele ReBirth-engine, songs en ISO worden niet meegeleverd.

## Uitvoeren

Open gewone Windows PowerShell. De starter
[Start-Audio-Test.ps1](Start-Audio-Test.ps1) controleert je originele DLL,
downloadt de vastgelegde helper en controleert de volledige SHA-256 voordat
hij iets uitvoert. Eerst volgt een korte controle zonder song. Als die slaagt,
start de opnameproef automatisch.

1. Laat het PowerShell-venster open en open de originele ReBirth-app.
2. Laad hetzelfde demonummer dat eerder hoorbaar afspeelde.
3. Zet **Loop uit**, stop en spoel terug naar het begin.
4. Ga terug naar het PowerShell-venster en druk op **Enter**. De helper neemt
   tien seconden op bij een vast tempo van 140 BPM.
5. Als de opname klaar is, sluit ReBirth normaal af via **File > Quit**. Als de
   demo als gewijzigd wordt gemeld, kies ervoor de demo niet op te slaan.
6. Na een geslaagde afsluiting staat een nieuw bestand `ReBirth-audiotest-....wav`
   op je bureaublad. Luister bijvoorbeeld via Ableton en zet bij import **Warp
   uit**, zodat je het oorspronkelijke testbestand hoort.

Tijdens deze proef gaat de uitvoer naar het WAV-bestand. De proef bevestigt pas
muziekweergave wanneer je het verwachte demonummer daarin hoort. Een numeriek
geslaagde opname is nog geen voltooide VST3 of geteste Ableton-koppeling.

Bij een fout stopt de starter. Geef de melding en de laatste JSON-regel uit
het venster door. Sluit het originele ReBirth-paneel steeds normaal af; sluit
het hostvenster niet in plaats van de app af. Het testprogramma sluit geen
andere applicatie geforceerd.

## Andere mappen en handmatige uitvoering

Met een lokaal opgeslagen starter kunnen andere paden worden opgegeven:

```powershell
.\Start-Audio-Test.ps1 -EngineDll 'D:\Audio\ReBirth\ReBirth.dll' -OutputFolder 'D:\Opnamen'
```

De standaarduitvoermap is het werkelijke Windows-bureaublad, ook als dat onder
OneDrive staat. De map moet bestaan. Een bestaand WAV-bestand wordt nooit
overschreven. Afgebroken of afgekeurde opnamen worden verwijderd; alleen een
voltooide en gecontroleerde opname blijft staan.

Het uitvoerbare bestand is ook rechtstreeks beschikbaar in
[bin/windows-x86](bin/windows-x86). De vastgelegde versie heeft SHA-256:

```text
10ecb61e88da86840d40b0bd40d5c359e7ff92843106b4bfffa9925f378cf1ef
```

De [technische uitleg](render-README.md) beschrijft de precieze grenzen van de
proef. Broncode, bouwscript, testresultaten en runtimevermeldingen staan bij
het programma in deze repository.
