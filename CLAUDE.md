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

### A. Post-Shader Crash: Access Violation `0xc0000005` at `IP = 0x000000b8`
* **Symptom**: Game crashed ~1 second after shader compilation finished, right before the title screen.
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
  - Line 69 of `patch_UKL_Utils_DynamicBranch.asm` performs `mtctr r11; bctrl`, where `r11` was loaded from `HLELoc` (`lwz r11, 0x0(r5)`).
  - `BindHLEAddresses()` in `SpawningVariables.h` previously called `osLib_getHLEFunctionAddress` and wrote the returned value into `HLELoc`.
  - **Disassembly of Cemu 2.6 at RVA `0x1FC1E0` revealed that `osLib_getHLEFunctionAddress` does NOT return a PPC instruction address!** It returns an internal 32-bit integer **HLE table index** (e.g. `0xb8`, `0xb6`, `0xbb`).
  - Writing `0xb8` into `HLELoc` caused PPC to execute `bctrl` to address `0x000000b8` (unmapped zero-page), causing an immediate memory access violation `0xc0000005`.
* **Fix Applied**:
  - In `SpawningVariables.h`, added a strict guard: only write to codecave locations if the address is a valid guest memory address (`addr >= 0x01000000 && addr != 0xFFFFFFFF`). Small indices are never written into guest execution memory.

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
