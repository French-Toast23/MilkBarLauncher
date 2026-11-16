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
  2. **Injected Payload (`InjectDLL.dll`)**: Native C++17 x64 DLL (`DLL/InjectDLL/`). Injected into `Cemu.exe` on startup. Communicates with its launcher via a named pipe, `\\.\pipe\languageConnectionPipe_<Cemu PID>` (per Cemu process since 17 Nov 2026; it used to be one shared name, which let several launchers cross-connect).
  3. **Multiplayer Server (`BOTWM.Server.dll`)**: C# TCP (default port `5050`) / UDP server handling player coordination, packet sync, and game states.
  4. **Graphic Pack (`BreathOfTheWild_Multiplayer`)**: Located at `%APPDATA%\Cemu\graphicPacks\BreathOfTheWild_Multiplayer\`. Houses PPC assembly patches (`patch_*.asm`), 32 custom `Jugador` actors (`content/Actor/Pack/Jugador*.sbactorpack`), and metadata (`rules.txt`, `content/Actor/ActorInfo.product.sbyml`, `content/System/Resource/ResourceSizeTable.product.srsizetable`).

---

## 2. Upstream Reference Sources & Toolchain Ecosystem

The following open-source codebases and reverse-engineering tools provide essential ground-truth logic, file formats, and architectural references for this project:

| Tool / Project | Official Repository | Project Role & Specific Use Case |
| :--- | :--- | :--- |
| **Cemu (2.0 / 2.6 branch)** | [cemu-project/Cemu](https://github.com/cemu-project/Cemu) | **Target Emulation Core & PPC JIT Engine**.<br/>Used to analyze:<br/>1. **HLE Interception System** (`src/Cafe/OS/libs/coreinit/`, `src/Cafe/OS/libs/osLib.cpp`): Analyzed `osLib_registerHLEFunction` and `osLib_getHLEFunctionAddress` to verify that Cemu returns internal 32-bit table indices (`0xb8`, `0xb9`), dispatched via PowerPC opcode 1: `(0x04000000 \| idx)`.<br/>2. **Graphic Pack Codecave Loader** (`src/Cafe/GraphicPack/`): Examined how `.origin = codecave` patches are assembled and mapped into guest virtual memory starting at `0x01800000`, enabling our pure dynamic signature scanner.<br/>3. **CPU Recompiler & Exception Handling** (`src/Cafe/CPU/`): Used to debug crash logs, PPC register state dumps (`sprNew.LR`, `hCPU->instructionPointer`), memory protection boundaries (`VirtualQuery`), and Vulkan pipeline caching. |
| **UKMM (U-King Mod Manager)** | [NiceneNerd/ukmm](https://github.com/NiceneNerd/ukmm) | **Modern BotW Mod Packaging & Merging Core (Rust)**.<br/>Used for:<br/>1. High-speed, conflict-free merging of game archives (`Bootup.pack`, `TitleBG.pack`).<br/>2. Reference implementation for merging `ActorInfo.product.sbyml`, preserving strict ascending CRC32 order and correct BYML primitive node types (`0xD0` for signed `S32`, `0xD1` for unsigned `U32`).<br/>3. Understanding modern BNP mod structure and graphic pack export standards. |
| **BCML (BotW Cross-Platform Mod Loader)** | [NiceneNerd/BCML](https://github.com/NiceneNerd/BCML) | **Legacy Reference Implementation for BotW Mod Merging (Python)**.<br/>Used for:<br/>1. The legacy codebase upon which Milk Bar Launcher's `BcmlSettings.cs` and `GameFilesModifier.cs` were designed.<br/>2. Reference merge logic for `GameData` flags (`gamedata.ssarc`, `savedataformat.ssarc` preservation), AAMP deep merging (`aslist.aamp`, `deepmerge.aamp`), and custom eventflows.<br/>3. Reverse-engineering why custom multiplayer flags require `IsSave: False` to avoid corrupting `game_data.sav` save files. |
| **oead** | [zeldamods/oead](https://github.com/zeldamods/oead) | **Nintendo File Format Library (C++ / Python)**.<br/>Used for parsing, serializing, and validating Nintendo first-party file formats: **BYML**, **AAMP**, **SARC**, and **Yaz0** compression. Critical for generating valid `ActorInfo.product.sbyml` files with 7,973 actors that pass BotW's in-engine binary search without crashing. |
| **rstb** | [zeldamods/rstb](https://github.com/zeldamods/rstb) | **Resource Size Table (RSTB) Calculator (C++ / Python)**.<br/>Used to calculate precise decompressed heap allocation limits in `ResourceSizeTable.product.srsizetable`. Prevents the catastrophic heap overflow crash (`0xc0000005` at `IP 0x03208cec`) when `Bootup.pack` and `gamedata.ssarc` are expanded with multiplayer actor and flag definitions. |
| **botw-anim-extractor / BotW Havok Tools** | [ArchLeaders/botw-havok](https://github.com/ArchLeaders/botw-havok) | **Animation & Physics Reverse-Engineering**.<br/>Used to inspect BotW's animation playback structures (`CustomAnimations.sbfres`, `Player_Animation_NoFace.sbfres`), bone skeletons, and ActionSequence hash identifiers (`Anim_<CRC32 of the animation name>`, e.g. `Anim_2327397999` = `Nml_Wait` idle). |

---

## 3. Exhaustive Bug Analysis & Confirmed Root Causes

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

### F. Popup "Failed to find mod address... Make sure mod is installed correctly in BCML" & Save Loading Crash `0xc0000005` at `IP = 0x03208cec`
* **Symptoms**:
  1. In-game popup: `"Failed to find mod address. Make sure that the mod is installed correctly in BCML and that BCML, Extended Memory and Multiplayer Utilities graphics packs are enabled."`
  2. Or Cemu closes immediately after shader compilation (`0xc0000005` at `IP 0x03208cec LR 0x034dd6f8`) when selecting the user account (`IOSU_ACT: using account <Name> in first slot`).
* **Root Causes**:
  - `InjectDLL` does not hook custom memory for player names, statuses, and equipment; instead, it reads/writes custom `GameData` parameters managed by BotW's internal `GameDataMgr`.
  - In `LocalInstance.h` (line 250):
    `NotPaused = new LittleEndian<bool>({ 0x93, 0x9B, 0xE2, 0xB9, 0x00, -1, 0x00, 0x00, 0x10, 0x29, 0x84, 0x10 }, -0x1, "Memory::LocalInstance::scan::NotPaused", 0);`
  - In `gamedata.yml` (from `MilkBarLauncher.bnp`):
    `notPaused` has CRC32 `HashValue: -1818500423` (unsigned `0x939BE2B9`). Likewise, `Jugador10_DispNameFlag` = `0x82CBCC60`, `Jugador10_Status` = `0x8768FA81`, `Jugador10_Name` = `0xF2A5D4A2`.
  - These are **GameData flags** stored inside `GameData/gamedata.ssarc` within `content/Pack/Bootup.pack`.
  - If `Bootup.pack` does not define these flags, BotW's `GameDataMgr` never allocates them in heap memory (`0x10298410`), causing `PatternScan` to fail after 15 retries.
  - **CRITICAL CAVEAT 1 (`savedataformat.ssarc` vs `gamedata.ssarc`)**:
    - Do **NOT** modify or re-sort `GameData/savedataformat.ssarc`. Existing user save files (`game_data.sav`) index saved fields strictly by their exact offset and chunk in `saveformat_0..5.bgsvdata` (for instance, `PlayReport_PlayTime` at index 660 of `saveformat_5`). Shifting entries corrupts save parsing, causing a NULL pointer dereference (`lwz r3, 0x10(rA)` where `rA == 0`, guest IP `0x03208cec`).
    - Custom multiplayer flags MUST have `IsSave: False` in `gamedata.ssarc`. This instructs `GameDataMgr` to instantiate the flags in RAM without requiring them in `game_data.sav` on disk, eliminating save corruption and crashes while allowing `InjectDLL` to locate all flags cleanly.
  - **CRITICAL CAVEAT 2 (`ResourceSizeTable.product.srsizetable` Heap Allocation Underflow)**:
    - BotW uses the Resource Size Table (RSTB) to pre-allocate heap buffers for every resource before decompression/parsing.
    - When `gamedata.ssarc` was injected into `Bootup.pack`, its uncompressed size increased to `6,537,124` bytes, requiring at least `6,537,412` bytes according to `SizeCalculator`.
    - However, the stock/deployed RSTB entry for `GameData/gamedata.sarc` was only `6,536,932` bytes (480 bytes undersized!).
    - When BotW decompressed `gamedata.ssarc`, the data overflowed the undersized heap allocation, corrupting subsequent heap structures and causing access violation `0xc0000005` at `IP 0x03208cec LR 0x034dd6f8`.
* **Fix Applied**:
  - Merged all 89 `bool_data` flags into `bool_data_2.bgdata` (with `IsSave: False`).
  - Merged all 67 `s32_data` flags into `s32_data_0.bgdata` (with `IsSave: False`).
  - Merged all 34 `string_data` flags into `string32_data_0.bgdata` (with `IsSave: False`).
  - Kept `GameData/savedataformat.ssarc` 100% pristine vanilla from `mlc01`.
  - Injected BNP mod files (`ModelDivideTable.sbyml`, `AIDef_Game.product.sbyml`, `EventInfo.product.sbyml`, `Static.smubin`, etc.) into `Bootup.pack`.
  - Deployed `Bootup.pack` to `%APPDATA%\Cemu\graphicPacks\BreathOfTheWild_Multiplayer\content\Pack\Bootup.pack`, `patches/Bootup.pack`, and `Build/patches/Bootup.pack`.
  - Re-calculated all 38 undersized RSTB entries across the entire graphic pack using `rstb.SizeCalculator`.
  - Raised `GameData/gamedata.sarc` in `ResourceSizeTable.product.srsizetable` to `6,541,508` bytes (+4KB safety padding).
  - Raised `Actor/ActorInfo.product.byml` in `ResourceSizeTable.product.srsizetable` to `2,037,488` bytes (covering the 1,971,952 bytes merged file + 64KB padding).
  - Repacked `ResourceSizeTable.product.srsizetable` as big-endian Yaz0 into `patches/` and `Build/patches/`.
  - Added automatic RSTB and Bootup synchronization in `BcmlSettings.ValidateAndFixGraphicPack()` so `ResourceSizeTable.product.srsizetable` is verified and copied to `%APPDATA%\Cemu\graphicPacks\BreathOfTheWild_Multiplayer\content\System\Resource\` on startup.

---

### G. Elimination of Dead Slow Path in Dynamic Branch Assembly (Claude Spec §2.3 #6)
* **Problem**: When a hook slot was unbound (`HLELoc == 0`), the assembly fast paths in `patch_UKL_Utils_DynamicBranch.asm`, `patch_SpawnActors.asm`, and `patch_Weather.asm` fell through to call `OSDynLoad_Acquire` and `OSDynLoad_FindExport` from arbitrary guest game threads. This triggered coreinit calls that consistently failed on Cemu 2.6 and introduced unnecessary stack frames.
* **Fix**: Replaced the fallthrough with an immediate return/restore when slot is zero:
  ```asm
  lwz r11, 0x0(r5) ; HLELoc
  cmpwi r11, 0x0
  bne UKL_Utils_DynamicBranch_CallSlot
  b UKL_Utils_DynamicBranch_Return_False
  ```
  Applied across all dynamic branch sites.

---

### H. HLE Registration Hardening & Actor Lifecycle Synchronization (Claude Spec §2.3 #5, §3.1, §4, §8.1)
* **HLE Self-Test**: Added a startup self-test in `init()` that registers a dummy function and verifies the `+0x10` offset against `osLib_getHLEFunctionAddress`. If the offset does not return a valid index, binding aborts safely and logs an error rather than writing corrupt opcodes.
* **Actor Lifecycle Logging**: Added detailed logging for actor queuing (`queueActor`), `createActor_` invocation arguments (`setupActor`), and actor creation events (`OnActorCreate`), enabling end-to-end auditability.
* **Flag Re-scan on Link Re-creation**: Added `Player::UpdateFlags` and hooked `GameROMPlayer` creation in `OnActorCreate` so that when Link respawns (after death, fast travel, or loading another save), custom player flag addresses are re-scanned and synchronized dynamically.
* **Cached Pointer Invalidation**: `OnActorErase` clears cached player actor and equipment addresses immediately on the game thread under mutex protection.

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

### I. Pattern Scan Relaxed Wildcarding, Multi-Region Fallback & Graceful Degradation
* **Symptom**: Popup dialog appeared ~15 seconds after loading into the game:
  `"Failed to find mod address. Make sure that the mod is installed correctly in BCML and that BCML, Extended Memory and Multiplayer Utilities graphics packs are enabled."`
* **Root Causes**:
  1. **Over-constrained Signature**: `NotPaused`, PropHunt flags, and `Jugador10_*` flags in `LocalInstance.h` hardcoded bytes 4, 6, 7 to `0x00` (`{ hash, 0x00, -1, 0x00, 0x00, vtable }`). In BotW, bytes 4..7 represent packed 32-bit bitfields (`IsProgramReadable`, `IsProgramWritable`, `IsSave`, `Category`, `ResetType`). When `IsSave: False` or `Category` is present, bitfields differ from 0x00, causing pattern scan rejection.
  2. **Single-Region Barrier**: `PatternScan` was called with `region = 8, multipleRegions = false`. In Cemu 2.6 with Extended Memory (2GB RAM pack), guest heap allocations can be placed in additional committed virtual memory regions beyond region 8.
* **Fix Applied**:
  - Replaced bytes 4..7 with `-1, -1, -1, -1` across `NotPaused`, PropHunt flags, and `Jugador10_*` in `LocalInstance.h`.
  - Added multi-region scanning fallback (`region = 0, multipleRegions = true`) in `LittleEndian.h`, `BigEndian.h`, `LocationAccess.h`, and `CharacterClasses.cpp` when region 8 returns `< 30000`.
  - Replaced fatal exception throwing on non-critical flags with `AddressSet = false` and warning logs, allowing gameplay to proceed.
  - Added `isAddressSet()` safety check in `Main::CheckIfPaused()` to default to unpaused if the flag is absent.

### J. Unblocking Actor Spawning Pipeline & Resolving Invisibility
* **Symptoms**:
  1. Popup dialog appeared ~100 seconds after loading save: `"Failed to find mod address..."`.
  2. Server console showed `Player Vanitas disconnected.`
  3. No remote player models (`Jugador1`..`32`) were visible in the 3D world on either instance.
* **Root Causes**:
  1. **Fatal Exception in `LocationAccess.h`**: `LocationAccess()` attempted 15 retries with `Sleep(2000)` and on failure threw `throw L"Failed to find mod address..."`.
  2. **Initialization Pipeline Abort**: In `dllmain_Functions.cpp`, `catch (LPCWSTR ex)` caught the exception from `LocationAccess`, showed a modal `MessageBoxW`, and called `Main::disconnectFromServer()`. This bypassed `setActorSpawning(&queue_mutex, queueActor, queueActor)` and left `Instances::PlayerList` unpopulated.
  3. **Location Invisibility Gate**: In `Player::PThread()`, remote actor creation (`action = ActCreate`) required `shouldExist = true`, which checked `LocalLocation.Map == this->Location->Map && LocalLocation.Map == "MainField"`. With `LocationAccess` uninitialized, both map strings were empty (`""`), permanently evaluating `shouldExist = false` and preventing remote actors from ever spawning.
  4. **Unbounded Scanner AV**: In `Scanner.cpp`, scanning across unbounded host virtual memory caused access violation `0xc0000005` in Cemu.
* **Fixes Implemented**:
  1. **Non-fatal `LocationAccess.h`**: Initialized `LastKnown` to `Map = "MainField"`, `Section = "None"`. Fast non-blocking scan; if not found, logs a warning and gracefully defaults without sleeping, throwing, or calling `exit(1)`.
  2. **Unconditional `PlayerList` & Spawning Initialization**: Removed `MessageBoxW` and `disconnectFromServer` from `scan()` and `scanPlayerFlags()`. `setActorSpawning` and `Instances::PlayerList` (all 32 players) now initialize unconditionally.
  3. **Map Fallback in `Player.cpp`**: `Player::PThread()` now treats empty or unset map strings as `"MainField"` so remote players spawn immediately in the open world.
  4. **SEH Guard & Clamp in `Scanner.cpp`**: Added `SafeReadByte` with `__try / __except` and clamped scans to `baseAddr + 0x100000000ULL` (4GB Wii U boundary).
  5. **Queued Actor Logging**: Added explicit logging in `queueActor(int playerNumber, ...)` for complete visibility into the spawn ring buffer.

### K. Elimination of Infinite While Loop in `EquippedItems.h`
* **Symptom**: During dual-instance test, Cemu never crashed, but remote players never appeared. The log showed endless repeated entries every 2 seconds:
  `[DBG][DataTypes::EquippedItems::EquippedItems] Failed to find equipped items.`
* **Root Cause**:
  In `EquippedItems.h`, `while (addr < 30000)` had no exit condition or retry counter. When scanning for `{ 0x82, 0x48, 0x92, 0xBE, -1, -1, -1, -1, 0x82, 0x48, 0x92, 0xBE }`, `Memory::ScanOffset` was calculated using uninitialized `Memory::RegionStart`, setting `startingAddress` to a 64-bit host virtual address (`> 2TB`). `PatternScan` immediately returned 0, permanently trapping `LocalInstance::scan()` in an infinite loop and preventing `setActorSpawning()` and `Instances::PlayerList` from ever being initialized.
* **Fix Applied**:
  1. Replaced infinite loops in `EquippedItems.h` with a 2-retry limit (`Sleep(200)`). If addresses are not found, it gracefully logs a warning, sets `AddressSet = false`, and returns default equipment without blocking.
  2. Removed `exit(1)` from `ValidateAddress` in `EquippedItems.h`.
  3. Added scan offset 0 fallback in `BigEndian.h` and `LittleEndian.h` if a non-zero `scanOffset` fails to find an address.
  4. Recompiled `InjectDLL.dll` and updated all launcher resources.

### L. Implementation of Claude Review Recommendations (Stability, CRC32 Validation & Safety)
* **Context**: Claude reviewed the mod address handling and identified 6 critical issues:
  1. **Anchor Underflow in `scanPlayerFlags()`**: Subtracting offsets (1, 4, 0x20) from an unvalidated `rawAddr` (which is 0 when not found) underflowed `uint64_t` to `0xFFFFFFFFFFFFFFFF`, causing access violations.
  2. **Derived Address Overwrite Risk**: Fixed strides (`0x10`, `0x20`, `0xC0`) without per-slot verification could corrupt vanilla flags if layout deviates.
  3. **Uninitialized `Memory::RegionStart`**: Since `LocationAccess` was non-fatalized and stopped calculating Region 8, `Memory::RegionStart` remained 0.
  4. **Silent Scan Failures**: Lack of visibility into which memory scans succeeded or degraded.
  5. **Startup Scan Redundancy**: Avoid unnecessary multi-region scans.
  6. **RSTB Margin**: `GameData/gamedata.sarc` size in RSTB was close to boundary; generous headroom is required.
* **Fixes Implemented**:
  1. **Anchor Guards & Underflow Protection**: In `LocalInstance.h`, `rawAddr >= 30000` is strictly enforced before performing any address arithmetic. If an anchor is not found, derived flag pointers default to 0 and players are skipped rather than writing to garbage memory.
  2. **CRC32 Per-Slot Hash Verification**: For every derived slot (`DispName`, `Exists`, `Status`, `Name`), the 4-byte big-endian hash is read and compared against the IEEE 802.3 CRC32 hash of the expected flag name (e.g. `Jugador{n}_DispNameFlag`, `Jugador{n}_Exists`, `Jugador{n}_Status`, `Jugador{n}_Name`). If the hash doesn't match, the slot is zeroed and never written to, guaranteeing 100% protection for vanilla flags.
  3. **Explicit `Memory::RegionStart`**: Calculated explicitly in `Main::Setup()` upon acquiring `Main::baseAddr` using `Memory::findRegionBaseAddress(Main::baseAddr, 8)` with a fallback 7-region query loop. `findRegionBaseAddress()` in `Scanner.cpp` was also hardened against infinite loops.
  4. **SEH Safe Memory Operations & Elimination of `exit(1)`**: Replaced raw `memcpy` with SEH `__try / __except` inside `LittleEndian<T>::set/get` and `BigEndian<T>::set/get`. Removed `exit(1)` from `ValidateAddress()`.
  5. **Summary & Diagnostic Logging**: Added structured `[SCAN SUMMARY]` log lines for both core instance structures and all 32 player flag sets. Added `DiagnoseFlagHash` to scan for isolated 4-byte hashes and log ±16 bytes of context if an anchor fails.
  6. **RSTB Expansion**: Increased `GameData/gamedata.sarc` size in `ResourceSizeTable.product.srsizetable` (patches, Build, and active Cemu graphic pack) to `6,700,000` bytes (+160KB headroom).

---

### M. Codecave Memory Overlap & JIT Crash `0xc0000005` at `IP = 0x00000000` During Save Loading
* **Symptom**: Cemu crashed with access violation `0xc0000005` (jumping to NULL address `0x0000000000000000`) on Cemu's CPU recompiler thread immediately when loading into the game world / save menu (right after `Link detected at: 0x6ee7ce30`).
* **Dump Analysis (`Cemu.exe.28264.dmp`) & Cemu `log.txt`**:
  - Cemu allocated consecutive graphic pack codecaves as follows:
    - `BotW_SpawnActors_V208`: `0x01800000` - `0x01808204`
    - `BotW_UKL_ActorInterceptor_V208`: `0x01808300` - `0x018084b4`
    - `BotW_UKL_RemoteBombAIInterceptor_V208`: `0x01808500` - `0x018085a0`
    - `BotW_UKL_TimeMgrInterceptor_V208`: `0x01808600` - `0x0180869c`
    - `BotW_UKL_Utils_DynamicBranch_V208`: `0x01808700` - `0x01808794`
    - `BotW_UKL_Utils_RegStore_V208`: `0x01808800` - `0x01808a18`
    - `BotW_MPTesting_V208`: `0x01808b00` - `0x01808c94`
  - In `SpawningVariables.h:BindHLEAddresses()`, hardcoded fallback addresses assumed old non-expanded codecave offsets:
    - `WeatherSync` wrote to `0x01808a00` -> directly inside `UKL_Utils_RegStore` (`0x01808800` - `0x01808a18`), corrupting register restore instructions (`WriteR29`..`WriteR31`).
    - `RemoteBomb` wrote to `0x01808428` -> directly inside `ActorInterceptor` (`0x01808300` - `0x018084b4`), corrupting `ActorEraseHook`.
    - `TimeMgr` wrote to `0x01808524` -> directly inside `RemoteBomb` (`0x01808500` - `0x018085a0`).
  - When `GameROMPlayer` was created and intercepted by `ActorCreateHook`, returning from `OnActorCreate` executed corrupted `RegStore` code, restoring a 0 return address and executing `blr` to `0x00000000`.
* **Fixes Implemented**:
  1. **Pure Dynamic Codecave Scanning**: Completely eliminated all blind hardcoded slot writes (`0x0180822c`, `0x01808244`, `0x01808258`, `0x01808a00`, `0x01808428`, `0x01808524`). The engine now scans for `"spawnactors"`, `"ukl_actorinterceptor"`, `"ukl_remotebombaiinterceptor"`, `"ukl_timemgrinterceptor"`, and `"WeatherSync"` dynamically in memory range `[0x01800000, 0x01820000]`. Slots are bound ONLY to verified matching offsets.
  2. **Guarded `instructionPointer` Restoration**: In `OnActorCreate`, `OnActorErase`, `WeatherFn`, `mainFn`, `remoteBomb_onAICalc`, `remoteBomb_onAICalc2`, and `timemgr_OnInit`, `hCPU->instructionPointer = hCPU->sprNew.LR;` is strictly guarded by `if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)` to ensure it never branches to 0 or invalid memory.
  3. **Non-blocking Position Resolution in `LocalInstance`**: Overrode `setAddress` to call `Actor::setAddress(addr)` without clearing `baseAddr`, and hooked `resolvePosition()` inside `get_characterData()` to resolve Link's position pointers on demand.

---

### N. Host Crash on Continue (`0xc0000005` in `InjectDLL.dll+0x24c7d`) & Joinable `std::thread` Lifecycle
* **Symptom**: When Instance 2 connected to Instance 1 (Host) and selected "Continue" from the BotW main menu, Instance 1 crashed immediately with an access violation (`0xc0000005`) in `InjectDLL.dll`.
* **Minidump (`Cemu.exe.13856.dmp`) & Log Analysis**:
  - Crash occurred inside `BigEndian<float>::get` at RVA `0x24c7d` with `RCX = 0x0000000000000000` (dereferencing a NULL `this` pointer).
  - Call stack: `mainServerLoop()` -> `GameInstance->World->set(serverResponse->WorldData)` -> `WorldObj::set()` / `WorldObj::get()`.
  - In `LocalInstance.h`, `WorldObj::ReadableTime` and `WritableTime` are dynamically populated only when `timemgr_OnInit` fires from the `ukl_timemgrinterceptor` HLE hook.
  - If a client connects before `timemgr_OnInit` completes or in a scene where it hasn't fired yet, `ReadableTime` and `WritableTime` remained uninitialized pointers (`nullptr`). Calling `get()` or `set()` on them called `BigEndian<float>::get(RCX=0)`, crashing Cemu immediately.
  - Concurrently, in `Player::Connect()`, reassigning `pThread = std::thread(&Player::PThread, this)` while `pThread` was already running/joinable threw an unhandled `std::terminate()`.
* **Fixes Implemented**:
  1. In `LocalInstance.h`, explicitly initialized all `WorldObj` pointer members (`Day = nullptr; WritableTime = nullptr; ReadableTime = nullptr;`) and `LocalInstance` pointer members (`Health`, `Defense`, `AtkUp`, `IsEquipped`, `Location`, `Equipment`, `Animation`, `NotPaused`) to `nullptr`.
  2. Guarded `WorldObj::get()` and `WorldObj::set()` with defensive NULL and `isAddressSet()` checks returning safe defaults (`0` and `0.0f`).
  3. Guarded `dllmain_Functions.cpp:mainServerLoop` against NULL `World` and `Location` before attempting time sync.
  4. In `Player.h`, added `if (connected) return;` at entry of `Connect()`. Added `if (pThread.joinable()) pThread.join();` before assigning a new thread in `Connect()` and inside `Disconnect()`. Added `~Player()` destructor calling `Disconnect()`.

---

### O. Stage Readiness & "Can't See Players Anymore" Spawning Freeze
* **Symptom**: After applying earlier crash guards, neither instance crashed, but remote players stopped appearing entirely ("can't see players anymore"). The spawn thread ceased queueing actors.
* **Root Cause**:
  - `Memory::IsStageReady()` checked `base + 0x1047c244`.
  - In BotW EU v208 (`0x6267BFD0`), section 4 (`.data`) ends at `0x104697a7` and section 5 (`.bss`) starts at `0x104855e0`. Address `0x1047c244` falls into an unmapped hole in memory, constantly returning `0`.
  - As a result, `IsStageReady()` returned `false` 100% of the time, permanently blocking `Player::PThread()`, `RequestCreate()`, and `ProcessActorSpawnsInWeather()`.
* **Fixes Implemented**:
  1. Replaced the unmapped `0x1047c244` check in `Memory::IsStageReady()` with `Main::s_LocalLinkAddr != 0 && getBaseAddress() != 0`. When Link's actor pointer is alive in the scene graph, the stage is guaranteed loaded and active.
  2. Created `IsActorManagerReady(uint32_t f_r3, uint64_t baseAddr)` in `SpawningVariables.h` to inspect `f_r3` -> `+0x80` -> `+0x0C` -> `+0x84` directly during actor dispatch, eliminating the PPC crash at `0x03948074` without blocking general player coordination.

---

### P. Confirmed Dual-Instance Coexistence & T-Pose Investigation
* **Verified Milestone**:
  - Both Cemu 2.6 instances run side-by-side at 54-57 FPS on Vulkan on an AMD GPU.
  - Instance 1 does not crash when Instance 2 connects and presses Continue.
  - Both players spawn and can see each other's 3D character models in real time on the Great Plateau Resurrection Tower without crashing (`media_1794690243707_acf0f909.jpg`).
* **Current Observed Behavior (T-Pose)**:
  - The remote player model renders in a bind T-pose (arms outstretched horizontally).
  - Root Cause Analysis:
    1. In `Player.h`, when `this->baseAddr == 0`, `Player::set()` returns at line 117 before writing `AnimAddr`.
    2. When `AnimAddr` is not written, it retains the default eventflow string `"Jugador1_animationthing"`.
    3. Because `"Jugador1_animationthing"` is not a valid ActionSequence in `CustomAnimations.sbfres`, BotW falls back to the actor's default bind skeleton pose (T-pose).
    4. To resolve this, `Player::set()` must ensure `AnimAddr` is always populated with a valid animation (e.g. `"Anim_2327397999"` = `Nml_Wait` idle; `789467239` is `Sword_Attack_Jump_F_St`, see section R) regardless of actor resolution state, and dynamic ActionSequence pointer extraction in `LocalInstance::getAnimation()` must map Link's movement states (idle, walk, run, jump, attack).

---

### WPF .NET 8 Launcher (`Breath of the Wild Multiplayer.exe`)
```powershell
dotnet build "WPF .NET 6\Breath of the Wild Multiplayer\Breath of the Wild Multiplayer.csproj" -c Release
```

---

### Q. Self-Contained Installer & Uninstaller (no BCML / UKMM)
* **Goal**: Install the mod from the launcher alone, and revert every change it made.
* **`ModPack/` (repo root, copied to the launcher output)**: the full graphic pack. It contains `rules.txt`, all `patch_*.asm`, `content/` (Jugador/CustomRemote actor packs, events, `CustomAnimations.sbfres`, merged `ActorInfo.product.sbyml`, expanded RSTB) and `merge/` (`Bootup.pack` holds only the 13 entries the mod changes; `TitleBG.pack` is the BNP diff). No full Nintendo archives are shipped. The old `patches/` folder was removed.
* **`Source files/ModInstaller.cs`**:
  - `Install()`:
    - Copies `ModPack` into `<Cemu user data>/graphicPacks/BreathOfTheWild_Multiplayer`. The user data folder is `%APPDATA%/Cemu`, or `portable/` next to Cemu.exe.
    - Merges `merge/*.pack` on top of the player's own v208 `Pack/Bootup.pack` / `Pack/TitleBG.pack`. For TitleBG, actor packs are merged per inner file.
    - Copies `PauseMenuPlayer.sbactorpack` from the update.
    - Enables the pack and Extended Memory in `settings.xml`.
  - The game dump (mlc01) is only read, never written.
  - A pre-existing pack folder that the launcher did not install is moved to `%APPDATA%/BOTWM/Backups` and restored on uninstall.
  - Every change is recorded in `%APPDATA%/BOTWM/install_manifest.json`.
  - `settings.xml` is edited as text, touching only our own `<Entry>` line or `disabled` attribute. Uninstall therefore restores it byte for byte (verified).
  - `Uninstall()` deletes the pack, restores the backed-up pack, reverts the `settings.xml` edits, and deletes the backups and manifest. Cemu must be closed for both operations.
  - `Repair()` runs before each launch (`BcmlSettings.ValidateAndFixGraphicPack`) and re-copies changed bundle files.
* **Launcher**:
  - Connecting auto-installs the mod if it's missing.
  - The Settings panel has "Install / Repair" and "Uninstall" buttons.
  - All BCML `settings.json` lookups were removed. `MergedDir` now always points at the installer's pack.
* **Multiple Cemu installs (16 Nov 2026)**: the launcher has up to 4 Cemu slots, implemented in `Source files/CemuProfiles.cs` and stored in `%APPDATA%/BOTWM/cemu_profiles.json`, which all launchers share.
  - Each launcher window picks its own slot with the `<` / `>` selector above Connect (`CemuProfiles.Active`, per process). `BcmlSettings.ResolveCemuDir()` returns the active slot, and auto-detection is used only while no slot is set.
  - Settings has four "Cemu N" rows: click to choose a `Cemu.exe`, right-click to clear. Install / Repair / Uninstall act on the active slot.
  - Each Cemu user data folder has its own manifest and backups: `install_manifest_<sha1-8>.json` and `Backups_<sha1-8>`. `%APPDATA%/Cemu` keeps the original names.
  - Install and Uninstall only require that slot's Cemu to be closed. Connect refuses a slot whose Cemu is already running.
  - Extra Cemus must be **portable** (a `portable` folder next to `Cemu.exe`). Otherwise they all share `%APPDATA%/Cemu`: `settings.xml`, `log.txt`, caches and saves.
  - Picking a non-portable Cemu offers `CemuProfiles.MakePortable()`. It copies settings (minus our pack entry, `mlc_path` cleared), profiles, saves, the transferable shader cache and downloaded graphic packs, and adds a junction for `mlc01/usr/title`.
* **Several players on one PC**: Connect opens `PlayerSetupWindow`, which asks for 1-4 players and a Cemu slot per player.
  - Player 1 plays in this launcher. Each other player gets a new launcher process started with `--cemu-slot N --connect <base64 JSON server>` (`LaunchArgs`), which sets its slot for that process only and connects automatically.
  - A launcher drives exactly one Cemu (one DLL, one pipe, one overlay).
  - Injection now targets the Cemu process the launcher started (`InjectToSpecificProcess`), so launchers starting Cemus together can't grab each other's.
  - Not tested in game yet (16 Nov 2026).
* **Portability rule**: never hardcode paths to a specific PC. Resolve everything at runtime (`SpecialFolder`, `AppContext.BaseDirectory`, Cemu settings).

### R. Remote Player Model, Duplicate Spawn & Animation Fixes
* **Wrong model (player 2+)**: every `Jugador<N>` actor's ModelList uses the `Jugador1ModelNameLongForASpecificReason*` names. A previous change used `Jugador<N>...` in `EquipmentAccess`, so armor was never found or applied. Reverted to the constant `Jugador1*` names.
* **Armor never shown**: model strings are read when the actor is created. As in the original mod, `Player::PThread` respawns the actor after the armor strings are written, capped at 2 respawns per equipment change.
* **Duplicate spawn (one instance saw nobody)**: a create was re-queued 3 s later while the first was still in flight. `PendingCreateSince` now blocks re-queueing for up to 10 s, until `OnActorCreate` binds the actor.
* **Sliding / no animation**: the sender sent `0` whenever the ActionSequence pointer chain was briefly unreadable, so the receiver snapped to idle. `LocalInstance::getAnimation()` now returns the last good value, and `Player::set` keeps the previous animation on `0`.
* **Spawn blocked entirely** (commit `2bfa4f5`): the `IsActorManagerReady(f_r3)` gate used offsets nothing backs up and always failed. Removed. Captured `createActor_` registers are invalidated when Link is erased (stage change) instead.
* **Fast travel inside MainField does not erase or recreate `GameROMPlayer`**, so the Link-erase reset never runs. Remote Jugador actors are destroyed by the stage unload without hitting our erase hooks.
  - Before this fix, the stale `Player::baseAddr` pointed at freed memory reused by other actors: a Hyrule Bass followed Link.
  - Before this fix, the captured `createActor_` registers from before the teleport were reused, which spawned random actors.
  - Fix: `Player::ValidateActor()` re-reads the actor name (`baseAddr+0x10` must equal `Jugador<N>`) each tick and in `set()`, and drops stale pointers.
  - Fix: `queueActor()` sets `s_needFreshCapture`, so every spawn waits for a fresh capture of the game's own `createActor_` call.
* **Deleting a Jugador through `Status = 1` does not work**: "Could not delete player" every time. Respawning to apply armor left the old actor uncontrolled, sliding in T-pose. If the old actor is still alive after the 5 s timeout, the thread now keeps controlling it and does not spawn a duplicate.
* **Deleting via `Status = 1` does work**, but the game skips our erase hooks for it. `ValidateActor()` now detects the deletion, so the armor respawn completes (verified in a two-player run on 15 Nov 2026).
* **Jugador1 stuck in T-pose (tests on 15 Nov 2026)**:
  - `Jugador<N>_animationthing` is not a GameData flag value. It's a placeholder string in the Jugador event-flow script loaded in memory (BFEVFL with `DIC ` tables). The DLL overwrites it with `Anim_<hash>`.
  - A read-only memory scan of both Cemu instances showed the DLL overwrites the right placeholder for the remote player (player 1 and player 2 alike). So the write address was not the problem, and `FixAnimationFlagsExact()` (built on the GameData theory) corrects nothing. It's kept only for its `AnimFlagDiag` log lines.
  - Real difference: in the BNP, only `Jugador2.sbactorpack` used its own AI program and `LODUser: NoCalcSkipForEachFrame` (`DisableOutScreenCalcStop`, `DisableCalcSkipFrame: 4`). The other 31 used `MultiplayerAI` with `LODUser: Dummy`, and only player 2 ever animated.
  - All 31 packs were rebuilt: `MultiplayerAI.baiprog` content = Jugador2's AI (no player-specific strings), plus `NoCalcSkipForEachFrame.blod`, plus `LODUser` switched.
  - RSTB: `MultiplayerAI.baiprog` raised to 116661 (Jugador2's value), and each `Actor/Pack/JugadorN.bactorpack` raised by its size growth + 0x1000.
* **How remote animations are applied (IDA + memory scan, 15 Nov 2026)**:
  - `TitleBG.pack/EventFlow/MultiplayerEvent.bfevfl` runs, per player, `CheckGameDataInt(Jugador<N>_Status)` and then `Demo_PlayASForDemo` with `ASName = "Jugador<N>_animationthing"` (and `Jugador<N>_AttackAnimation` for attacks). The DLL replaces those placeholders with `Anim_<hash>` / `Attack_<hash>`, names that exist in the `MultiplayerAS` ASList.
  - BFEVFL files are little-endian (`ff fe`). On Wii U the game builds a separate big-endian runtime copy and runs that one, where each string is a u16 BE length, the text, then 0. The old walk in `scanPlayerFlags` only ever finds the LE file buffer, so writes there never animated anyone. This is the root cause of the T-pose for all players.
  - `PlayASForDemo` (AI class, CRC `0xa8bfb550`; param setup `sub_2358BDC`) copies `ASName` into its own `sead::SafeString` (at `this+0x2C`) from the event's dynamic param via `sub_30EC1B8`, which takes a C-string pointer. So the text is read up to the NUL each time the action loads its params.
  - `ScanEventFlowStrings()` finds the BE runtime copies, and `WriteEvflAnimation()` writes the zero-padded text there **without touching the u16 length**. An earlier version rewrote the length, and the game deleted the actor within a second.
* **Crash when teleporting**: stage teardown erases the remote Jugador. `PThread` immediately re-queued it, and `createActor_` ran with registers from the dying stage, so Cemu crashed 1-2 s later (dumps `Cemu.exe.24016/26024.dmp`).
  - An erase or disappearance we didn't request (`Player::DeleteRequested` false) now calls `Main::MarkStageUnstable(8000)`, as do a Link erase and a >50-unit local jump (5000).
  - While the stage is unstable, `mainFn` won't call `createActor_` and `PThread` won't queue creates. A fresh register capture is also required afterwards.
* **Stage teardown in IDA (`U-King.rpx.i64`)**: `sub_2C268D0` is the stage manager's unload/reload state machine, with Japanese step labels such as `AutoPlacementMgr/PlacementMgr/RadarMgr stopThread()待ち`. Step 9 starts at `0x2c27314`. Hooking it there would give an exact "stage unloading" event if the heuristics above prove insufficient.
* **`ModInstaller.Repair()`** re-deploys the whole bundle when the SHA-256 of `ModPack` differs from the `bundle=` line in `.milkbar_installed`. It now runs before the per-connect edits (player model, Jugador attention).
* **Fresh `createActor_` capture is only required after a stage change**: Link erased, or a local jump over 50 units per tick (`Main::RequestFreshSpawnCapture`). Requiring it for every spawn stalled respawns while the game created no actors of its own.
* **Launcher UI**:
  - The window is resizable (`CanResizeWithGrip`). `MainWindow.xaml.cs` scales `RootGrid` uniformly from the 1186x666.8 design size, but not in the in-game overlay, which follows Cemu's window.
  - The settings panel uses fixed 99 px rows in a `ScrollViewer`.
* **`PlayASForDemo` works; the idle hash was wrong (PPC debugger + IDA, 15 Nov 2026)**:
  - A breakpoint on the enter function (`0x2357db4`) showed the action entered for `Jugador2` with `ASName = "Anim_789467239"`.
  - With `SeqBank = 0` (the event's value), enter (`sub_2359564`) calls `sub_37946F0` → `sub_370DCF8`, which plays the AS by name.
  - An AS hash is the CRC32 of the animation file name. `789467239` is `Sword_Attack_Jump_F_St`, not an idle. This is why the remote player played a jump attack and then froze on its last frame. The real idle is `Nml_Wait` = `2327397999`.
  - `AnimTables.h` (generated from the Jugador pack) lists the 1292 `Anim_` entries in `MultiplayerAS.baslist` and the 176 `Attack_` demo actions.
  - `Player::ResolveAnim()` only sends hashes the ASList has, falling back to the last good one or to idle. Attacks need a matching `Attack_` action.
  - Test switch: `%APPDATA%\BOTWM\anim_mode.txt` (`off` / `idle` / `full`, default `full`).
* **Animation test mode**: set `test` in `%APPDATA%\BOTWM\anim_mode.txt` (implemented in `AnimDebug.h`).
  - Remote players cycle through the ASList, and their name tag shows `<i>/<n> <name>`.
  - Hotkeys work only while this Cemu window has focus: Ctrl+Shift+Right/Left = next/previous, Up = mark OK, Down = mark BAD, R = replay, A = auto-advance.
  - Optional `anim_test_filter.txt` takes name fragments (one per line) to limit which animations are tested.
  - Verdicts are appended to `anim_test_results.txt`.
  - `AnimNames.h` maps hashes to names, so the log names every animation it receives.
* **Animation slots (IDA, 15 Nov 2026)**:
  - `actor+0x394` is the AS holder: `+0x94` = AnmTarget count, `+0x98` = targets (0x34 bytes each); target `+0x18` = slot count, `+0x1C` = slots (0x68 bytes each).
  - `slot+0xC != 0` means an AS is playing, and `*(*slot+4)` is its name (`sub_370E458`, `sub_37143EC`).
  - `sub_370DBDC(holder, target, slot, ...)` plays an AS by name. `PlayASForDemo` plays on target 0, slot 0.
  - `BindFlg 0` includes a bone subtree and `BindFlg 3` excludes it. In Link's ModelList: target 0 = lower body, 1 = upper body (no face), 2 = right arm, 3 = face (TargetType 1, three slots).
  - `LocalInstance::LogASTargets()` logs every target's AS name for the local Link (tag `ASTargets`).
* **Face misplaced on remote players**: the Jugador's target 0 (where `PlayASForDemo` plays) excluded `Face_Root`, and nothing ever plays on the face-only target 1, so the face bones stayed at rest while the head moved.
  - All 32 packs now have `IsParticalEnable = false` on `AnmTarget_0`. The `.bmodellist` size is unchanged, so the RSTB needs no update.
* **Respawn stalled forever when standing still**: an unrequested Jugador disappearance forced a fresh `createActor_` capture, and a static scene never provides one.
  - `MarkStageUnstable(ms, keepCaptureBackup = true)` now keeps the previous capture for that case and reuses it after 15 s without a fresh one.
  - Link erase and the teleport detector still drop it.
* **Tests on 16 Nov 2026 (logs `LatestLog_25076/19916/18348`, dumps `Cemu.exe.25076/19916.dmp`)**:
  - Reusing the saved capture crashed Cemu when it fired during the local Link's fast travel (target 0 AS = `Warp`). `LogASTargets()` now treats `Warp` as a hard stage change (`MarkStageUnstable(10000)`), which drops the backup.
  - A freshly spawned Jugador was sometimes deleted by the game within a second, and its memory was reused (`Kibako_Contain_0`). The placement data copied from the game's last `createActor_` (r7 storage) is the suspect.
    - `Player::CheckInstantDeletion()` discards the capture when that happens within 5 s.
    - `setupActor` logs `Spawn template: placement data copied from '<actor>'`.
  - `ValidateActor` drops an actor only after 3 consecutive failed name reads, and logs the name it read.
  - The `Status = 1` delete wait went from 5 to 8 s; the game takes just over 5 s.
  - The other dump (25076) crashed in recompiled game code reading guest address `0x111`. Cemu's `log.txt` was overwritten by the next start, so the guest PC is unknown.
  - Launcher crash: two launchers started together raced on `user.config` (`ConfigurationErrorsException` in `SettingsPanelModel`). Every settings save now goes through `SettingsSaver.SaveSafe()` (retries, never throws).
  - The launcher builds with the user-local SDK: `%LOCALAPPDATA%\Microsoft\dotnet\dotnet.exe build ... -c Release`.
* **Wrong / late remote animations (test 16 Nov 2026 23:37)**: the hash from `getAnimation()`'s pointer chain lags behind. It kept `Nml_Move_Run` while Link's AS was `Wait`, and `Sword_Charge` during `CutS1`.
  - `LocalInstance::CorrectAnimation()` reads Link's target-0 AS name and keeps the hash only if that AS can play it.
  - Otherwise it sends the AS's animation whose name is closest to the last consistent one (same weapon family), or the AS's default.
  - The AS-to-animation table is `LinkASMap.h`, generated from `GameROMPlayer`'s `Player.baslist` and `Actor/AS/*.bas` (`FileName` params). Animations are ordered so start/end/random/pose clips come last.
  - The network rate is 60 updates/s (`Server.SerializationRate`), so it isn't the cause.
* **The spawn hook is `ActorCreator::requestCreateMapActor`** (zeldaret/botw `actActorCreator.cpp`).
  - Arguments: r3 = this, r4 = name, r5 = heap, r6 = mubin_iter, r7 = map_object, r8 = handle, r9 = task_lane_id, r10 = params.
  - Our 104-byte "actor storage" is a copied `ksys::map::Object` (`mapObject.h`). Wii U offsets: 0x00 mFlags0, 0x04 mActorDataIdx/mFlags, 0x0C mId, 0x0E mStaticCompoundActorId, 0x10/0x14/0x18 pointers, 0x1C MubinIter, 0x24 mProc, 0x28 mLinkData, 0x2C revival flag, 0x34 mHashId, 0x38 mTranslate, 0x44 mTraverseDist, 0x48 mTraverseDistInt, 0x4A mHardModeFlags.
  - `PrepareInstanceData` now also resets mId, mStaticCompoundActorId (-1), the 0x10/0x14 pointers and mHardModeFlags. Before, these came from the copied actor, a suspected cause of the instant deletions.
  - The decompilation is a Switch 1.5.0 reference, so Wii U offsets must be re-derived from the 32-bit layout.
* **Remote movement smoothing (17 Nov 2026)**: the old speed-guess extrapolation in `Player::Teleport(pos, true)` overshot and snapped back, and part of its formula divided by the absolute world coordinate. It is replaced by snapshot interpolation.
  - `set()` → `PushSnapshot()` stores each received position with a high-resolution timestamp (`NowMs`, QueryPerformanceCounter; GetTickCount only ticks every ~15.6 ms).
  - `PThread` draws `SamplePosition()`: 100 ms in the past, blended between the two samples around that time.
  - When packets are late it extrapolates along the last movement for at most 150 ms. Jumps over 25 units snap.
  - `PThread` runs at ~120 updates/s for FPS++ (60-120 FPS). FPS++ keeps game speed in real time, so time-based blending is correct at any FPS.
* **Upper-body layer experiment (17 Nov 2026)**: in `PlayASForDemo`, `TargetIndex` = AnmTarget (-1 = all) and `SeqBank` = slot inside it (`sub_2359564`: param +20 / +24).
  - Link's shield, horse call and bow draw play on his target 1, slot 1 (`WaitAttentionUpper`, `MoveAttentionUpper`, `HorseCall`, `BowCharge`), an overlay the mod never sent.
  - Test: `MultiplayerEvent.bfevfl` gets an extra `Demo_PlayASForDemo` step for Jugador1/2 (`Jugador<N>_UpperTest`, `ASName = Anim_618558213` = `Sword_Guard_Wait`, `TargetIndex = 2`, the Jugador upper-body target), chained after the main PlayAS.
  - The bfevfl grew to 84296 bytes, so its RSTB entry was raised from 83616 to 92000. Backups: `TitleBG.pack.before_upper`, `rstb.before_upper` in the scratchpad.
  - Link's ASList also has a crossfade (`CFDefines`) section that the Jugador's `MultiplayerAS` lacks.
* **Upper-body layer sync (17 Nov 2026)**:
  - **Packet:** new `int UpperAnimation` right after `Animation` in the DLL (`ClientCharacterDTO`, `CloseCharacterDTO`, `Serialization.cpp`) and the server (`ClientPlayerDTO`, `ClosePlayerDTO`, `ServerClasses/Player`). The server serializes by reflection in field order, so client and server must be the same build.
  - **Sender:** `LocalInstance::GetUpperAnimation()` reads Link's target 1, slot 1 overlay AS (`WaitAttentionUpper`, `MoveAttentionUpper`, `HorseCall`, `BowCharge`...) and sends a playable clip for it via `LinkASMap` and `PickPlayable`, matching the weapon family of the base clip. 0 means nothing is layered.
  - **Receiver:** every player's flow in `MultiplayerEvent.bfevfl` now starts with `Jugador<N>_Hold` → `Jugador<N>_Upper` → status switch.
    - `Jugador<N>_Upper` is a `Demo_PlayASForDemo` with `TargetIndex = 2` (the Jugador's upper-body target) and the placeholder `ASName = Jugador<N>_upperanimthing`, so it runs on every pass, attacks included.
    - The DLL writes it as event-string kind 3 (`EvflUpperStrings`): the overlay clip, otherwise the attack clip, otherwise the body clip, so the upper body follows.
  - The bfevfl is now 100120 bytes; its RSTB entry was raised to 112000. The `Jugador1/2_UpperTest` experiment was replaced.
  - Ideas for this design came from the TotK online mod's puppet / animation-request sync (no code used).
* **Boot crash after the upper-body change (17 Nov 2026, dumps Cemu.exe.4124/19488/11224/25372)**: every dump was a game read at guest 0x20 right after account select. Cause: the merged `Pack/TitleBG.pack` (77,090,788 bytes) outgrew its RSTB entry (77,078,304), because the bfevfl grew.
  - The merged packs contain the player's own game files, so their size differs per copy. `ModInstaller.DeployBundle` now calls `RstbPatcher.EnsureAtLeast` to raise `Pack/TitleBG.pack` and `Pack/Bootup.pack` to their real size + 256 KB after merging (never lowers; tested against zeldamods/rstb).
  - The shipped RSTB was raised as well.
  - Lesson: any change that grows `merge/TitleBG.pack` or `merge/Bootup.pack` also grows the pack itself; the installer now covers that.
* **Removed**:
  - The Weather-hook spawn path (it never dispatched, and it raced `mainFn` on `ringPtr`).
  - The 100-unit "teleport detector" (it orphaned live remote actors).
  - The unregistered `remoteBomb_onAICalc2`.

---

## 5. Verification Checklist for Testing

1. **Clean Boot**: Game boots past shader cache compilation and loads title screen without access violation `0xc0000005`.
2. **Camera Follow**: Load save file; confirm Link moves and camera follows smoothly.
3. **Log Integrity (`%APPDATA%\BOTWM\LatestLog_<PID>.txt`)**:
   - `init(): All HLE interceptors registered successfully.`
   - `BindHLEAddresses Readback: ...`
   - `OnActorCreate: actor 'GameROMPlayer'` binds local Link.
4. **Multiplayer Spawn & 3D Render**:
   - Instance 2 connects to host (`127.0.0.1:5050`).
   - Host logs `setupActor` queuing `Jugador1`.
   - `ActorCreator::createActor_("Jugador1")` executes cleanly.
   - Remote player model renders in full 3D (`media_1794690243707_acf0f909.jpg`).
5. **Animation Synchronization**:
   - Verify `LocalInstance::resolveAnimation: Resolved animation address ...` in log.
   - Walking, running, jumping, climbing, attacking, and idle play animations smoothly instead of bind T-pose.
6. **Fast-Travel / Teleportation Stability**:
   - Fast-travel between towers/shrines on both instances.
   - Confirm zero crashes during world scene unload and reload.
