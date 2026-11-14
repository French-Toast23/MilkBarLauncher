# Claude Engineering Guide: Breath of the Wild Multiplayer (Milk Bar Launcher / Cemu 2.6 / BotW v208)

> [!IMPORTANT]
> **Documentation for Future AI Agents & Reverse Engineers**
> This file contains the complete reverse-engineering reference, architectural blueprint, confirmed bugs, root-cause analyses, and solutions for the *Breath of the Wild Multiplayer* mod (Milk Bar Launcher / Cemu 2.6 / BotW v208). Read this before modifying any DLL hooks, graphic pack patches, or launcher serialization code.

---

## 1. Project Overview & Environment

* **Target Title**: *The Legend of Zelda: Breath of the Wild* (Wii U version)
* **Title IDs**:
  - `00050000101C9500` (USA Base Game)
  - `0005000E101C9500` (v208 / v1.5.0 Update, Module Hash: `0x6267BFD0`)
  - `0005000C101C9500` (DLC 3.0)
* **Host Platform**: Windows 11 x64
* **Emulator**: **Cemu 2.6** (x64 native, wxWidgets GUI, PPC JIT Recompiler)
* **Core Components**:
  1. **Launcher (`Breath of the Wild Multiplayer.exe`)**: WPF desktop app on .NET 8 (`WPF .NET 6/Breath of the Wild Multiplayer/`). Handles Cemu process management, graphic pack verification, Yaz0 decompression, SARC packing, and GUI settings.
  2. **Injected Payload (`InjectDLL.dll`)**: Native C++17 x64 DLL (`DLL/InjectDLL/`). Injected into `Cemu.exe` on startup. Communicates with launcher via Named Pipes (`\\.\pipe\BOTW_MilkBar_Pipe_<PID>`).
  3. **Multiplayer Server (`BOTWM.Server.dll`)**: C# TCP (default port `5050`) / UDP server handling player coordination, packet sync, and game states.
  4. **Graphic Pack (`BreathOfTheWild_Multiplayer`)**: Located at `%APPDATA%\Cemu\graphicPacks\BreathOfTheWild_Multiplayer\`. Houses PPC assembly patches (`patch_*.asm`), 32 custom `Jugador` actors (`content/Actor/Pack/Jugador*.sbactorpack`), and metadata (`rules.txt`, `content/Actor/ActorInfo.product.sbyml`, `content/System/Resource/ResourceSizeTable.product.srsizetable`).

---

## 2. Exhaustive Bug Analysis & Confirmed Root Causes

### A. HLE Hook Dispatch Architecture & Crash `0xc0000005` at `IP = 0x000000b8`
* **Symptom**: Game crashed ~1 second after shader compilation finished, or custom hooks (`OnActorCreate`, `fnCallMain`, `WeatherSync`) never executed.
* **Cemu Crash Log (`log.txt`)**:
  ```
  Exception 0xc0000005 at 0x7ff746569a90(+0x2c9a90) in module Cemu.exe
  Active PPC instance:
  IP 0x000000b8 LR 0x018086a4 Thread 0x0e000700
  PPC stack dump:
  [0x0e200ac4] 01808570 0180851c 0e200ad0 0365ec84 ...
  ```
* **Reverse-Engineering Finding**:
  - `0x0365ec84` is hooked by `patch_UKL_TimeMgrInterceptor.asm` (`UKL_TimeMgrInterceptor_OnInit`).
  - `UKL_TimeMgrInterceptor_OnInit` calls `UKL_Utils_DynamicBranch` in `patch_UKL_Utils_DynamicBranch.asm` (`0x01808600`).
  - Disassembly of Cemu 2.6 at RVA `0x1FC1E0` (`osLib_getHLEFunctionAddress`) revealed that it returns an internal 32-bit integer **HLE table index** (`0xb8`, `0xb6`, `0xbb`), NOT a guest PPC code pointer!
  - Writing `0xb8` into `HLELoc` caused PPC to execute `bctrl` to address `0x000000b8` (unmapped zero-page), causing crash `0xc0000005`.
  - Guarding against `addr < 0x01000000` prevented writing altogether, leaving `HLELoc == 0`. As a result, the hooks never bound and `OnActorCreate` never fired!
* **How Cemu 2.6 Executes Guest HLE Calls**:
  - In Cemu, custom HLE calls are encoded as PowerPC opcode 1: `(1 << 26) | idx` (`0x04000000 | idx`).
  - When PPC executes at an address containing `0x04000000 | idx`, Cemu intercepts opcode 1 and dispatches the native C++ HLE callback for index `idx`.
  - In the C++ callback, returning sets `regs->instructionPointer = regs->sprNew.LR;`. Cemu sets PPC's instruction pointer directly to `LR`, returning to the caller with no trailing `blr` instruction required.
* **Complete Solution Implemented**:
  1. **`patch_UKL_Utils_DynamicBranch.asm`**:
     - Fast path: `lwz r11, 0x0(r5); cmpwi r11, 0; bne UKL_Utils_DynamicBranch_CallSlot`.
     - `UKL_Utils_DynamicBranch_CallSlot`: `mr r11, r5` (sets `r11 = &HLELoc`, which holds the opcode) and branches to `CallDllFunc`.
     - `CallDllFunc` loads `CTR = &HLELoc` and calls `bctrl`. PPC executes the opcode at `&HLELoc`, Cemu executes the callback, and returns to line 71 (`Return_True`).
  2. **`patch_SpawnActors.asm` & `patch_Weather.asm`**:
     - Updated `CallDllFunc` to compute `&HLELoc` via `addi r11, r11, HLELoc@l` instead of dereferencing `lwz`, branching directly to the opcode at `&HLELoc`.
  3. **`BindSlot` in `SpawningVariables.h`**:
     - Encodes `op = (int)(0x04000000u | idx)`.
     - Checks if current memory matches `op`; writes only on mismatch and verifies readback. Continually rewrites if Cemu reinitializes graphic pack `.int 0` memory.
  4. **Stable Registration in `init()`**:
     - Guarded by `static bool s_initialized` to register with Cemu exactly once, ensuring HLE table indices remain permanent and avoiding recompiler cache churn.

---

### B. Missing `ActorInfo.product.sbyml` (Remote Players Invisible)
* **Symptom**: Player 2 connects, but their 3D model never appears; `OnActorCreate: actor 'Jugador1'` never logged.
* **Root Cause**:
  - `ActorCreator::createActor_("Jugador1")` at `0x037b6040` looks up the actor's CRC32 hash in `ActorInfo.product.sbyml`.
  - The graphic pack was missing `content/Actor/ActorInfo.product.sbyml` entirely. BotW fell back to the base game's file, which had 0 `Jugador` entries, causing `createActor_` to return `nullptr`.
* **Fix Applied**:
  - Extracted all custom actor definitions from `logs/actorinfo.yml` (all 34 `Jugador` variations + custom bombs) and merged them into base game `ActorInfo.product.sbyml`.
  - **Nintendo BYML Schema Compliance**:
    - BYML requires node type `0xD0` (`S32`) for values `< 0x80000000` and node type `0xD1` (`U32`) for values `>= 0x80000000`.
    - Hashes are sorted strictly ascending unsigned 32-bit (required for BotW's in-engine binary search).
    - Serialized with `oead` and Yaz0 compressed to 831,691 bytes. All 7,973 actors verified 1-to-1.
  - Backed up to `patches/ActorInfo.product.sbyml`.
  - Auto-recovery added to `BcmlSettings.cs`: verifies that `content/Actor/ActorInfo.product.sbyml` exists and is $\ge 800\text{ KB}$, automatically restoring it from `patches/` if missing.

---

### C. Camera Locked at World Coordinates `(0, 0, 0)`
* **Symptom**: Camera spawned underground or detached from Link at `(0, 0, 0)`.
* **Root Cause**:
  - `%APPDATA%\Cemu\graphicPacks\BreathOfTheWild_Multiplayer\content\Actor\Pack\GameRomCamera.sbactorpack` was a corrupt 173-byte dummy file containing only an empty `baischedule`.
  - The official base game camera pack is 18,209 bytes and contains `Actor/ActorLink/GameRomCamera.bxml` and `Actor/AIProgram/GameRomCamera.baiprog`.
  - Because `fsPriority = 100`, this 173-byte dummy overrode the base game camera, detaching Link from the camera controller.
* **Fix Applied**:
  - Renamed the dummy file to `GameRomCamera.sbactorpack.disabled`.
  - Added auto-quarantine in `BcmlSettings.ValidateAndFixGraphicPack()`: any `GameRomCamera.sbactorpack` under 1,000 bytes is automatically renamed to `.disabled`.

---

### D. Fast-Travel Screen Freeze
* **Symptom**: Teleporting to any shrine or tower resulted in an infinite loading screen with background audio still playing.
* **Root Cause**:
  - `patch_NoFreezingLoadingScreen.asm` forced a `li r3, 0` return at `0x031507c4`, breaking Cemu 2.6's loading state machine transitions.
* **Fix Applied**:
  - Renamed to `patch_NoFreezingLoadingScreen.asm.disabled`.
  - Added auto-quarantine in `BcmlSettings.ValidateAndFixGraphicPack()`.

---

### E. Runaway Memory Leak: `hookshot.exe`
* **Symptom**: System ran out of paging file memory (`0x800705AF`).
* **Root Cause**: An orphaned legacy `hookshot.exe` process (PID 11284) from an external tool was spinning in an infinite loop consuming **15.8 GB of physical RAM** and 100% of a CPU core.
* **Fix Applied**: Terminated the runaway process, recovering ~17 GB of system RAM.

---

### F. Popup "Failed to find mod address... Make sure mod is installed correctly in BCML"
* **Symptom**: In-game camera works and Link is controllable, but after ~15 seconds of gameplay, a popup appears:
  `"Failed to find mod address. Make sure that the mod is installed correctly in BCML and that BCML, Extended Memory and Multiplayer Utilities graphics packs are enabled."`
  Both instances disconnect.
* **Root Cause**:
  - `InjectDLL` does not hook custom memory for player names, statuses, and equipment; instead, it reads/writes custom `GameData` parameters managed by BotW's internal `GameDataMgr`.
  - In `LocalInstance.h` (line 250):
    `NotPaused = new LittleEndian<bool>({ 0x93, 0x9B, 0xE2, 0xB9, 0x00, -1, 0x00, 0x00, 0x10, 0x29, 0x84, 0x10 }, -0x1, "Memory::LocalInstance::scan::NotPaused", 0);`
  - In `scratch/logs/gamedata.yml` (from `MilkBarLauncher.bnp`):
    `notPaused` has CRC32 `HashValue: -1818500423` (unsigned `0x939BE2B9`)!
  - Likewise, `Jugador10_DispNameFlag` = `0x82CBCC60`, `Jugador10_Status` = `0x8768FA81`, `Jugador10_Name` = `0xF2A5D4A2`.
  - These are **GameData & SaveData flags** that BCML merges into `GameData/gamedata.ssarc` and `GameData/savedataformat.ssarc` inside `Bootup.pack`.
  - Because `content/Pack/Bootup.pack` in the graphic pack was unmerged vanilla, BotW's `GameDataMgr` never initialized these flags in heap memory (`0x10298410`), causing `PatternScan` to fail after 15 retries.
* **Fix Applied**:
  - Merged all 89 `bool_data` flags into `bool_data_2.bgdata` (total 1,461).
  - Merged all 68 `s32_data` flags into `s32_data_0.bgdata` (total 2,776).
  - Merged all 34 `string_data` flags into `string32_data_0.bgdata` (total 40).
  - Merged all 101 `savedata` flags into `savedataformat.ssarc`, sorted strictly ascending by signed 32-bit `HashValue`, re-chunked across `saveformat_0..5.bgsvdata` (total 43,768).
  - Repacked `gamedata.ssarc` and `savedataformat.ssarc` into `Bootup.pack` (29,026,976 bytes).
  - Saved to `patches/Bootup.pack` and `Build/patches/Bootup.pack`.
  - Added automatic verification and synchronization in `BcmlSettings.ValidateAndFixGraphicPack()` so `Bootup.pack` is copied to `%APPDATA%\Cemu\graphicPacks\BreathOfTheWild_Multiplayer\content\Pack\Bootup.pack` if missing or differing in length/timestamp.

---

## 3. Cemu 2.6 Internal Architecture Reference

### A. HLE Dispatch & Trampolines
Disassembly of `Cemu.exe` (2.6 x64):
* **`osLib_registerHLEFunction` (RVA `0x1FC1D0` / `0x1FBF20`)**: Registers `(libraryName, functionName, functionPtr)`. Assigns an internal table index `eax` (e.g. `0xb8`), stored at `[entry + 0x30]`. It does **not** allocate guest PPC memory.
* **`osLib_getHLEFunctionAddress` (RVA `0x1FC1E0`)**: Searches Cemu's internal HLE table by hashed library and function name and returns `[entry + 0x30]` (the **HLE index**, not a PPC address).
* **`PPC_ExecuteHLEFunction` (RVA `0x3B9780`)**:
  ```x86asm
  mov rcx, [rip + 0x13487d9] ; HLE function pointer table
  movsxd r8, edx            ; edx = HLE function index (0xb8)
  mov rax, [rcx + r8*8]     ; host C++ function pointer
  mov rcx, rbx              ; rcx = PPCInterpreter_t* hCPU
  call rax                  ; invokes host callback!
  ```
* **Graphic Pack `import.<lib>.<func>` Syntax**:
  - Handled in Cemu at RVA `0x2D5F96`.
  - Splits tokens by `.`.
  - Calls `0x1F4AA0` (`FindLoadedRPL(libraryName)`).
  - Calls `0x1F51B0` (`FindExportAddress(rplIndex, fnName)`).
  - Cemu allocates native PPC trampolines in the `0x00E00000` memory region.

---

## 4. Build Instructions

### Native C++ DLL (`InjectDLL.dll`)
```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "DLL\InjectDLL\InjectDLL.vcxproj" /p:Configuration=Release /p:Platform=x64
Copy-Item "Build\Resources\InjectDLL.dll" "WPF .NET 6\Breath of the Wild Multiplayer\Resources\InjectDLL.dll" -Force
```

### WPF .NET 8 Launcher (`Breath of the Wild Multiplayer.exe`)
```powershell
dotnet build "WPF .NET 6\Breath of the Wild Multiplayer\Breath of the Wild Multiplayer.csproj" -c Release
```

---

## 5. Verification Checklist for Testing

1. **Clean Boot**: Game boots past shader cache compilation and loads title screen without access violation `0xc0000005`.
2. **Camera Follow**: Load save file; confirm Link moves and camera follows smoothly.
3. **Log Integrity (`%APPDATA%\BOTWM\LatestLog_<PID>.txt`)**:
   - `init(): All HLE interceptors registered successfully.`
   - `BindHLEAddresses Readback: ...`
   - `OnActorCreate: actor 'GameROMPlayer'` binds local Link.
4. **Multiplayer Spawn**:
   - Instance 2 connects to host (`127.0.0.1:5050`).
   - Host logs `setupActor` queuing `Jugador1`.
   - `ActorCreator::createActor_("Jugador1")` finds `Jugador1` in `ActorInfo.product.sbyml`.
   - `OnActorCreate: actor 'Jugador1'` binds remote player model.
