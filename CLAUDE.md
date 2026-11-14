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
