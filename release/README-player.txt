Megami Ibunroku Persona Digital Collection - English translation & native port
Recomp by Second Save
https://github.com/ACR-GD/persona-digital-collection-recomp

This is a native version of the 1998 Windows CD-ROM "Megami Ibunroku Persona Digital Collection"
(ASCII / Digital Famitsu) with an English translation. No game data is included: you need your own
copy of the original Japanese disc, as a disc image (.iso, .bin/.cue) or as a folder (the CD itself,
or a copy of its files).

First start
  1. Start PersonaDC (macOS: PersonaDC.app; Windows: PersonaDC.exe; Linux: ./PersonaDC).
  2. In the launcher, click "Disc image..." or "Folder..." and choose your disc (you can also drop it
     on the window), then click Install. The game files (about 480 MB) are copied to your user folder
     and the English translation (persona_en.patch, next to the program) is applied.
  3. Click Play.

Settings (in the launcher): language (English or the original Japanese), window size, fullscreen,
pixel or smooth scaling, sound, and whether the launcher shows at startup. Alt+Enter toggles
fullscreen in the game. To get the launcher back after choosing "Skip", start the program with
--launcher, or delete persona.cfg in the folder below.

Playing: aim with the butterfly - it is the game's mouse cursor.

Tickets and hidden attractions
  Five mini-games each give a ticket when you win them: the mopping race, Reiji & Brown's magic,
  turtle hockey, the shooting gallery and Maki's puzzle. The other attractions (tarot, karaoke,
  checks...) give none. To use your tickets, go to the principal's office and answer Yes when
  Principal Oishi offers to show you around; each ticket opens one hidden attraction. Tickets are
  not saved: quitting the program loses them. Details: GUIDE.md on the project page.

Where things are kept
  macOS:   ~/Library/Application Support/Second Save/Persona Digital Collection/
  Windows: %APPDATA%\Second Save\Persona Digital Collection\
  Linux:   ~/.local/share/Second Save/Persona Digital Collection/
  (data = the game files, data_en = the English movies, save = files the game writes, persona.cfg)

macOS: the app is not notarized. The first time, right-click PersonaDC.app and choose Open.
