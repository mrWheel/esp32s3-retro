# ESP32-S3 Retro Computer

De ESP32-S3 Retro Computer is een retrocomputer-emulator voor meerdere systemen, gebouwd rond de Espressif ESP32-S3 microcontroller. In plaats van één klassieke machine na te bouwen, herbergt hij meerdere retrocomputers op één klein bord: CP/M-80, CP/M-86, UCSD Pascal, de Apple II en — als toekomstige toevoeging — de SWTPC 6800. Na het inschakelen toont het scherm een hoofdmenu op de USB-console; de gebruiker kiest een systeem en de bijbehorende emulator start zijn eigen gast-CPU, firmware en schijfstations. Alle systemen delen één gemeenschappelijke set van hostvoorzieningen.

Het referentiebord is een LOLIN S3 Pro (ESP32-S3-WROOM-1) met 16 MB flash en 8 MB octale PSRAM. De firmware is ontwikkeld met native ESP-IDF. De interne flash bevat een LittleFS-partitie met de minimale opstartbronnen voor elk systeem, zodat de esp32s3-Retro ook zonder geheugenkaart werkt. Alle diskettes staat op een microSDHC-kaart met FAT32, gerangschikt onder een /retro/-hoofdpad met per-systeem mappen voor images en back-ups; echte floppy-stations worden vervangen door disk-imagebestanden.

De bediening is bewust tekstgericht. Elke ANSI/VT100-compatibele USB-terminal met een venster van 80 kolommen bij 24 regels doet dienst als toetsenbord en scherm, waardoor het apparaat bij uitstek geschikt is voor serieuze, tekstgerichte software. De esp32s3-Retro is niet geschikt voor graphics of games.

File Transfer is een browsergebaseerde Disk Image Manager die rechtstreeks door de ESP32-S3 zelf wordt bediend. Na verbinding met het wifi-netwerk toont het apparaat zijn IP-adres in de terminal. De ingebouwde webserver/GUI biedt de mogelijkheid voor het uploaden, downloaden, kopiëren, hernoemen en verwijderen van losse bestanden en volledige disk images — inclusief beheer van de bestanden binnen ondersteunde disk images — zonder iets op de computer te hoeven installeren. Gastprogramma's kunnen een monotone milisecondetimer gebruiken voor timing en benchmarks.

Het is een praktische manier om de beginjaren van de personal computer te herbeleven en het programmeren op de retro-manier te ervaren.
