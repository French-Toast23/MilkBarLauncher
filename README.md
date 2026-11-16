# MilkBarLauncher
Online multiplayer for *The Legend of Zelda: Breath of the Wild* (Wii U, v208) on **Cemu 2.6**.
This repository holds every project needed to build the launcher, the injected DLL, the server and the mod files.

No BCML or UKMM is needed: the launcher installs the mod itself and can remove it again.

**Main folders:**

## [C#](C%23)
- The multiplayer server.
- The solution is located at `C#\GUIApp\GUIApp.sln`.
- Implemented in C#; uses sockets for connectivity.

## [DLL](DLL/InjectDLL)
- The code injected into Cemu.
- The solution is located at `DLL\InjectDLL\InjectDLL.sln`; the entry point is `DLL\InjectDLL\dllmain.cpp`.
- Reads and writes player data in game memory, spawns remote players and plays their animations.
- Talks to the launcher over named pipes.

## [WPF .NET 6](WPF%20.NET%206/Breath%20of%20the%20Wild%20Multiplayer)
- The launcher (WPF, .NET 8).
- The solution is located at `WPF .NET 6\Breath of the Wild Multiplayer\Breath of the Wild Multiplayer.sln`.
- Installs / repairs / uninstalls the mod, starts Cemu, injects the DLL and shows the in-game overlay.

## [ModPack](ModPack)
- The mod as a self-contained Cemu graphic pack, copied next to the launcher at build time.
- `merge/` holds only the files the mod changes inside `Bootup.pack` / `TitleBG.pack`; at install time they are merged
  on top of the player's own game files, so no Nintendo archives are shipped.

## [BNP Files](BNP%20Files)
- The original BCML mod packages (kept for reference; no longer needed to play).

# Installing and uninstalling
- **Install / Repair:** Settings > *Install / Repair*, or simply press Connect: the mod is installed automatically when missing.
  The game dump is only read, never modified. Everything the installer changes is recorded so it can be undone.
- **Uninstall:** Settings > *Uninstall* removes the graphic pack and restores Cemu's settings exactly as they were.
- Cemu must be closed for both.

# Several Cemu installs / players on one PC
- Settings has up to four *Cemu* slots. Extra Cemus should be **portable** copies (a `portable` folder next to `Cemu.exe`)
  so they don't share settings, saves, logs and caches.
- Pressing **Connect** asks how many players play on this PC and which Cemu each one uses. Each extra player gets its
  own launcher window, which starts its Cemu and joins the same server.
- Install / Uninstall act on the Cemu selected above the Connect button.

# Building the project
The process can be automated with the [python script](buildWPF.py). Manual commands are listed in [CLAUDE.md](CLAUDE.md#4-build-instructions).

# Technical architecture & status
For the reverse-engineering notes (Cemu 2.6 HLE hooks, actor spawning, animation system, known bugs and their fixes),
see [CLAUDE.md](CLAUDE.md).

# Credits & sources
- **Milk Bar Launcher / Breath of the Wild Multiplayer**: the original project by the MilkBarModding team, which this
  repository continues.
- Reference material (read for research; no code copied):
  - [zeldaret/botw](https://github.com/zeldaret/botw), the BotW decompilation (actor creation, map objects). No license file.
  - [leoetlino/botw-re-notes](https://github.com/leoetlino/botw-re-notes), BotW reverse-engineering notes. No license file.
  - [cemu-project/Cemu](https://github.com/cemu-project/Cemu), emulator internals (HLE calls, graphic packs). MPL-2.0.
  - [NiceneNerd/UKMM](https://github.com/NiceneNerd/UKMM) (GPL-3.0) and [NiceneNerd/BCML](https://github.com/NiceneNerd/BCML)
    (no license file): how BotW mods are merged.
  - KirbyMimi's *BotW couch co-op* graphic pack and *TotK online* mod: design ideas for remote players
    (puppet actors, interpolated movement, forwarding animation requests). Ideas only, no code used.
- Tools used to build and verify the mod files (not shipped):
  - [zeldamods/oead](https://github.com/zeldamods/oead) (GPL-2.0): SARC, BYML, AAMP, Yaz0.
  - [zeldamods/rstb](https://github.com/zeldamods/rstb) (GPL-2.0): resource size table.
  - [zeldamods/evfl](https://github.com/zeldamods/evfl) (GPL-2.0): event flows.
  - IDA Pro: disassembly of the game executable.

# Third-party libraries shipped with the launcher
| Library | License |
| :--- | :--- |
| [SarcLibrary](https://github.com/EPD-Libraries/SarcLibrary) | AGPL-3.0-or-later |
| [CsYaz0](https://github.com/EPD-Libraries/cs-yaz0) | AGPL-3.0-or-later |
| [AampLibrary](https://github.com/ArchLeaders/NCF-Library) | GPL-3.0-or-later (NuGet package) |
| [BfresLibrary](https://github.com/KillzXGaming/BfresLibrary) | MIT |
| [Newtonsoft.Json](https://github.com/JamesNK/Newtonsoft.Json) | MIT |
| [Revrs](https://github.com/ArchLeaders/Revrs) | MIT |
| [SharpYaml](https://github.com/xoofx/SharpYaml) | MIT |
| [CommunityToolkit.HighPerformance](https://github.com/CommunityToolkit/dotnet) | MIT |
| [Syroot.BinaryData](https://gitlab.com/Syroot/BinaryData) / [Syroot.Maths](https://gitlab.com/Syroot/Maths) | MIT (see each repository's LICENSE) |

*The Legend of Zelda: Breath of the Wild* is © Nintendo. Players need their own copy of the game and its v208 update.
`ModPack` contains no full game archives, but some of its files are built from game data (the merged `ActorInfo`,
the resource size table, and `GameData` merged into `merge/Bootup.pack`).

# Usage
- You can use the project in any way you prefer.
- You can make any modifications to the project, but you cannot redistribute it unless you have modified a substantial portion of the project's code. Modification to non functional parts of the code, do not count as code modification.
