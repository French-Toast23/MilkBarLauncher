# Breath of the Wild Multiplayer: Technical Brief for Claude (Status & Review)

> **Summary for Claude**
> This update details the resolution of the HLE recompiler binding, the investigation of the "Failed to find mod address" error, the root cause of the post-shader save loading crash, and the hardening implemented per the Change Specification (`BOTW_Multiplayer_Cemu2.6_Change_Spec.md`) for standalone *Breath of the Wild Multiplayer* (Cemu 2.6 / BotW v208 on Windows 11).

---

## 1. What Is Confirmed Working

1. **HLE Opcode Architecture Implemented & Verified**:
   - Custom HLE calls are encoded as PowerPC opcode 1: `op = (0x04000000 | idx)`.
   - `patch_UKL_Utils_DynamicBranch.asm` fast-path branches to `UKL_Utils_DynamicBranch_CallSlot` (`mr r11, r5; b CallDllFunc`), calling `&HLELoc` directly.
   - `patch_SpawnActors.asm` and `patch_Weather.asm` compute `&HLELoc` via `addi r11, r11, HLELoc@l` instead of dereferencing `lwz`.
   - `init()` in `InjectDLL` is guarded by `static bool s_initialized` so registration with Cemu occurs only once, keeping table indices stable.
   - **Live Log Verification (`LatestLog_3272.txt`)**:
     ```
     BindSlot: OnActorCreate at 0x247baab822c (idx=0xb8) wrote op=0x40000b8 (VERIFIED MATCH)
     BindSlot: fnCallMain at 0x247baab0000 (idx=0xb6) wrote op=0x40000b6 (VERIFIED MATCH)
     BindSlot: WeatherSync at 0x247baab8a00 (idx=0xb7) wrote op=0x40000b7 (VERIFIED MATCH)
     OnActorCreate: actor 'GameROMPlayer' at 0x1860544848
     Link detected at: 0x6ee5a950
     mainFn HLE invoked by Cemu.
     WeatherFn HLE invoked by Cemu.
     ```
   - **Zero Recompiler Crashes**: The earlier crash at `IP = 0x000000b8` is completely eliminated.

2. **In-Game Camera Follows Link**:
   - The corrupt 173-byte dummy `GameRomCamera.sbactorpack` was quarantined to `.disabled`.
   - The camera now attaches smoothly to Link in third-person without snapping to world origin `(0, 0, 0)`.

3. **ActorInfo & 32 Custom Actors**:
   - `ActorInfo.product.sbyml` has all 7,973 actors (including `Jugador1`..`Jugador32` and custom bombs) strictly ascending unsigned 32-bit sorted with proper BYML `S32`/`U32` node types.
   - RSTB table verified: all `Jugador*` dependencies have allocated buffer sizes.

4. **Dual Instances & Networking**:
   - Host server runs on TCP `5050`.
   - Dual Cemu instances side-by-side connect simultaneously (`Player Vanitas joined the server. Assigned to player 1`, `Assigned to player 2`).

---

## 2. Hardening Implemented per Change Specification

### A. Dead Slow Path Removal in Dynamic Branch (§2.3 #6)
- In `patch_UKL_Utils_DynamicBranch.asm`, `patch_SpawnActors.asm`, and `patch_Weather.asm`, the legacy fallthrough calling `OSDynLoad_Acquire` and `OSDynLoad_FindExport` has been replaced with an immediate return/restore when `HLELoc == 0`.
- This removes unnecessary coreinit invocations from game threads and eliminates potential stack clobbering before DLL registration is complete.

### B. HLE Lookup Self-Test (§2.3 #5)
- In `init()` (`SpawningVariables.h`), a dummy function `dummySelfTestFn` is registered to `selftest/dummy` upon startup.
- The index is immediately retrieved via `osLib_getHLEFunctionAddress` at offset `+0x10`. If the returned index is invalid (`0` or `> 0xFFFF`), binding aborts safely and logs an error, preventing corrupt opcode execution if Cemu's internal layout changes.

### C. Resource Size Table (RSTB) Audit & Expansion (§5.5, §5.8)
- Audited the deployed `ResourceSizeTable.product.srsizetable` against all graphic pack files using `rstb.SizeCalculator`.
- **Root Cause of Post-Shader Crash `0x03208cec`**:
  - `GameData/gamedata.sarc` decompressed size in `Bootup.pack` was `6,537,124` bytes (requiring `6,537,412` bytes according to `SizeCalculator`).
  - The deployed RSTB had only `6,536,932` bytes allocated (480 bytes undersized!), causing heap buffer overflow and memory corruption when loading save files at account selection (`IOSU_ACT: using account Vanitas in first slot`).
- **Fix**: Enlarged all 38 undersized RSTB entries, setting `GameData/gamedata.sarc` to `6,541,508` bytes and `Actor/ActorInfo.product.byml` to `2,037,488` bytes.
- Integrated automated RSTB deployment into `BcmlSettings.ValidateAndFixGraphicPack()`.

### D. GameData vs SaveData Integrity (§5.5)
- Kept `GameData/savedataformat.ssarc` byte-identical to vanilla stock from `mlc01`, ensuring `PlayReport_PlayTime` remains at index 660 and existing save files load without schema mismatch.
- Added all 190 custom multiplayer flags into `GameData/gamedata.ssarc` with `IsSave: False`, allowing runtime heap instantiation without disk save dependence.

### E. Actor Spawning Logging & Flag Re-Scan (§3.1, §3.3, §8.1)
- Added explicit logging in `queueActor` and `setupActor` showing actor name, 3D target coordinates, function pointer `0x037b6040`, and ring buffer offsets.
- Added `Player::UpdateFlags` to dynamically re-scan and update player flags when `GameROMPlayer` is re-created (e.g. after death, fast travel, or loading another save).
- `OnActorErase` clears cached player actor and equipment addresses immediately on the game thread under mutex protection.

---

## 3. Verification & Build Artifacts

1. **Native C++ DLL**: Recompiled into `Build/Resources/InjectDLL.dll` (417 KB) and synced to WPF Resources.
2. **WPF Launcher**: Built cleanly with .NET 8 (`Milk Bar Launcher.exe` & `Milk Bar Launcher.dll`).
3. **Graphic Pack Patches**:
   - `patches/patch_UKL_Utils_DynamicBranch.asm`
   - `patches/patch_SpawnActors.asm`
   - `patches/patch_Weather.asm`
   - `patches/Bootup.pack` (29,022,096 bytes)
   - `patches/ActorInfo.product.sbyml` (831,691 bytes)
   - `patches/ResourceSizeTable.product.srsizetable` (498,831 bytes)
