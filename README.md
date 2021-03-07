# qGBA
A GBA emulator using C++ and SDL.
## Features
Still very work-in-progress. No games are fully playable, and there's no sound emulation.  
The ARMWrestler test ROM runs, and passes most tests, but fails others.  
A few commercial games such as "Kirby's Nightmare in Dreamland" and "Pokemon Pinball Ruby and Sapphire" will run and play, but with messed up graphics.
## Building
Currently, there is only build support for Windows.
- Install Microsoft Visual Studio Community 2019.
- Open `qGBA.sln` and build.
## Usage
Run qGBA.exe from command line, with the game ROM as argument 1 and the BIOS ROM as argument 2.  
e.g. `qGBA.exe mario.gba gba_bios.bin`
## Future Plans
- Fix PPU bugs that are causing garbled graphics
- Sound support
- Fix CPU bugs causing failed ARMWrestler tests