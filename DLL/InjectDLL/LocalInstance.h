#pragma once

#include "Actor.h"
#include "LinkASMap.h"
#include "AnimNames.h"
#include <cstring>

using namespace DataTypes;

namespace Main {
	extern uint64_t s_LocalLinkAddr;
	void RequestFreshSpawnCapture();
	void MarkStageUnstable(DWORD ms, bool keepCaptureBackup = false);
	bool IsStageUnstable();
}

namespace MemoryAccess
{
	class WorldObj
	{
	public:
		BigEndian<int>* Day = nullptr;
		BigEndian<float>* WritableTime = nullptr;
		BigEndian<float>* ReadableTime = nullptr;
		int Weather = 0;
		int LocalWeather = 0;

		void set(DTO::WorldDTO* input)
		{
			if (!input) return;
			Mutex.lock();

			int currentDay = (this->Day && this->Day->isAddressSet()) ? this->Day->get(__FUNCTION__) : 0;
			float currentTime = (this->ReadableTime && this->ReadableTime->isAddressSet()) ? this->ReadableTime->get(__FUNCTION__) : 0.0f;

			if (currentTime < 350.0f && (input->Day > currentDay || currentDay - input->Day > 1 || (currentDay == input->Day && input->Time - currentTime > 2.0f)))
			{
				if (this->Day && this->Day->isAddressSet())
					this->Day->set(input->Day, __FUNCTION__);
				if (this->WritableTime && this->WritableTime->isAddressSet())
					this->WritableTime->set(input->Time, __FUNCTION__);
				Sleep(100);
			}

			Weather = input->Weather;
			Mutex.unlock();
		}

		DTO::WorldDTO* get()
		{
			Mutex.lock();
			DTO::WorldDTO* result = new DTO::WorldDTO();

			result->Day = (this->Day && this->Day->isAddressSet()) ? this->Day->get(__FUNCTION__) : 0;
			result->Time = (this->ReadableTime && this->ReadableTime->isAddressSet()) ? this->ReadableTime->get(__FUNCTION__) : 0.0f;
			result->Weather = this->LocalWeather;

			Mutex.unlock();
			return result;
		}
		
	private:
		std::shared_mutex Mutex;
	};

	class LocalInstance : public Actor
	{
	public:
		/* Flags object */
		struct FlagAddresses {
			uint64_t DispNameFlag;
			uint64_t ExistsFlag;
			uint64_t StatusFlag;
			uint64_t NameFlag;
			uint64_t AnimationFlag;
			uint64_t HoldFlag;
			uint64_t MapPin;
			uint64_t AttackAnimation;
		};

		/* Global flags - Flags used in events like GameRomCamera */
		struct PropHunt_Flags {
			LittleEndian<bool>* HidingPhase = nullptr;
			LittleEndian<bool>* RunningPhase = nullptr;
			LittleEndian<bool>* Countdown = nullptr;
			LittleEndian<bool>* HidePlayer = nullptr;
			LittleEndian<bool>* ShowPlayer = nullptr;
			LittleEndian<bool>* StopMusic = nullptr;
		};

		BigEndian<int>* Health = nullptr;
		BigEndian<int>* Defense = nullptr;
		BigEndian<float>* AtkUp = nullptr;
		LittleEndian<bool>* IsEquipped = nullptr;
		LocationAccess* Location = nullptr;
		EquippedItems* Equipment = nullptr;
		BigEndian<int>* Animation = nullptr;
		BombAccess* Bomb = new BombAccess();
		BombAccess* Bomb2 = new BombAccess();
		BombAccess* BombCube = new BombAccess();
		BombAccess* BombCube2 = new BombAccess();
		EnemyAccess* EnemyService = new EnemyAccess();
		QuestAccess* QuestService = new QuestAccess();

		std::shared_mutex PauseMutex;

		WorldObj* World = new WorldObj();

		LittleEndian<bool>* NotPaused = nullptr;

		DWORD LastQuestUpdate = 0;

		int playerNumber;
		PropHunt_Flags propHuntFlags = PropHunt_Flags();
		
		/* PVP */
		DWORD LastHealthUpdate = GetTickCount();
		std::map<std::string, int> WeaponDamages = readWeaponDamages();

		/* Actor Spawning */
		std::shared_mutex* queue_mutex;
		void (*queuePlayer)(int, float*);
		void (*queueActor)(std::string, float*);
		std::vector<std::tuple<int, Vec3f>> PlayerQueue = {};

		void setActorSpawning(std::shared_mutex* mutex, void (*method)(int, float*), void (*queueActorMethod)(std::string, float*))
		{
			queue_mutex = mutex;
			queuePlayer = method;
			queueActor = queueActorMethod;
		}

		void ChangeActorModel(std::string actorName, uint64_t actorAddr)
		{
			uint64_t addr = Memory::ReadPointers(actorAddr, { 0x39C, 0x6C });
			
			addr = Memory::ReadPointers(addr, { 0x4B0, 0x38 });
			uint64_t face = Memory::ReadPointers(addr, { 0x0, 0xC }, true);
			uint64_t folder = 0;

			int i = 0;
			std::string readString = "";

			while (readString != "Armor" && i < 50)
			{
				i++;
				readString = Memory::read_string(face - i, 5, "ChangeActorModel");
			}

			if (readString == "Armor")
			{
				folder = face - i;
			}

			std::stringstream stream;
			stream << actorName << ": " << std::hex << actorAddr << " -> Folder: " << std::hex << folder << " Model: " << std::hex << face;
			Logging::LoggerService::LogInformation(stream.str());

			Memory::write_string(folder, "Armor_002", 10, "Rewrite armor");
			Memory::write_string(face, "Armor_002", 10, "Rewrite armor");
		}

		bool IsGamePaused = false;

		void RequestCreate(int playerNumber, Vec3f pos = Vec3f(0, 0, 0))
		{
			if (this->baseAddr == 0 || Main::s_LocalLinkAddr == 0 || !Memory::IsStageReady())
			{
				Logging::LoggerService::LogWarning("Create request failed: Local Link or Stage is not active in world.");
				return;
			}

			if (IsPaused())
			{
				Logging::LoggerService::LogWarning("Create request failed: Game is paused.");
				return;
			}

			if (!queue_mutex)
			{
				Logging::LoggerService::LogWarning("Create request failed: queue_mutex is null.");
				return;
			}

			queue_mutex->lock();

			Logging::LoggerService::LogDebug("Requesting creation of player " + std::to_string(playerNumber) + "...", __FUNCTION__);

			if (pos.x() == 0 && pos.y() == 0 && pos.z() == 0)
			{
				if (Position && Position->isAddressSet())
					pos = Position->get(__FUNCTION__);
			}

			PlayerQueue.insert(PlayerQueue.begin(), {playerNumber, pos});

			queue_mutex->unlock();

			Logging::LoggerService::LogInformation("Queued creation of player " + std::to_string(playerNumber) + " at (" + std::to_string(pos.x()) + ", " + std::to_string(pos.y()) + ", " + std::to_string(pos.z()) + ")", __FUNCTION__);
		}

		void ProcessCreationRequests()
		{
			if (!queue_mutex || !queuePlayer)
				return;

			if (this->baseAddr == 0 || Main::s_LocalLinkAddr == 0 || IsGamePaused || !Memory::IsStageReady())
				return;

			queue_mutex->lock();

			if (PlayerQueue.size() > 0 && !IsGamePaused)
			{
				int playerToSpawn = std::get<0>(PlayerQueue.back());
				Vec3f posToSpawn = std::get<1>(PlayerQueue.back());

				PlayerQueue.pop_back();

				float SpawnPos[3] = { posToSpawn.x(), posToSpawn.y(), posToSpawn.z() };

				queuePlayer(playerToSpawn, SpawnPos);
			}

			queue_mutex->unlock();
		}

		bool RequestCreate(std::string actor, Vec3f pos = Vec3f(0, 0, 0))
		{
			if (this->baseAddr == 0 || Main::s_LocalLinkAddr == 0)
			{
				Logging::LoggerService::LogWarning("Create request failed: Local Link is not active in world.");
				return false;
			}

			if (IsPaused())
			{
				Logging::LoggerService::LogWarning("Create request failed: Game is paused.");
				return false;
			}

			if (!queue_mutex || !queueActor)
			{
				Logging::LoggerService::LogWarning("Create request failed: queueActor is null.");
				return false;
			}

			queue_mutex->lock();

			if (pos.x() == 0 && pos.y() == 0 && pos.z() == 0)
			{
				if (Position && Position->isAddressSet())
					pos = Position->get(__FUNCTION__);
			}

			float SpawnPos[3] = { pos.x(), pos.y(), pos.z() };

			std::stringstream stream;
			stream << "Requesting creation of " << actor << " at x: " << SpawnPos[0] << " y: " << SpawnPos[1] << " z: " << SpawnPos[2];

			Logging::LoggerService::LogDebug(stream.str(), __FUNCTION__);

			queueActor(actor, SpawnPos);

			queue_mutex->unlock();

			Logging::LoggerService::LogInformation("Requested creation of actor " + actor, __FUNCTION__);

			return true;
		}

		void ClearQueue()
		{
			if (queue_mutex)
			{
				queue_mutex->lock();
				PlayerQueue.clear();
				queue_mutex->unlock();
			}
		}

		int lastAnimation = 0;
		Vec3f lastTeleportCheckPos = Vec3f(0, 0, 0);
		bool hasLastTeleportCheckPos = false;

		// Returns the last good value when the ActionSequence chain is briefly unreadable
		// (it is rebuilt on state changes); sending 0 made remote players snap to idle.
		int getAnimation()
		{
			if (this->baseAddr == 0) return lastAnimation;
			std::vector<int> offsets = { 0x448, 0x4C, 0x10, 0x19C, 0x38, 0xA0, 0x10, -0x5C };
			uint64_t animAddr = Memory::ReadPointers(this->baseAddr, offsets, true);
			if (animAddr >= 30000)
			{
				MEMORY_BASIC_INFORMATION mbi{ 0 };
				if (VirtualQuery((LPCVOID)(animAddr + 0x10), &mbi, sizeof(mbi)) &&
					(mbi.State & MEM_COMMIT) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
				{
					int anim = Memory::read_bigEndian4Bytes(animAddr + 0x10, "getAnimation");
					if (anim != 0)
						lastAnimation = anim;
				}
			}
			return lastAnimation;
		}

		// Guarded big-endian u32 read at a guest address (no logging on failure).
		static bool SafeGuest32(uint32_t guest, uint32_t& out)
		{
			uint64_t base = Memory::getBaseAddress();
			if (base == 0 || guest < 0x1000) return false;
			const unsigned char* p = (const unsigned char*)(base + guest);
			__try { out = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		static bool SafeGuestCStr(uint32_t guest, char* buf, int cap)
		{
			uint64_t base = Memory::getBaseAddress();
			if (base == 0 || guest < 0x1000) return false;
			const char* p = (const char*)(base + guest);
			__try { int i = 0; for (; i < cap - 1 && p[i]; i++) buf[i] = p[i]; buf[i] = 0; return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { buf[0] = 0; return false; }
		}

		// Name of the AS playing on Link's animation target / slot ("" if none or unreadable).
		std::string ReadASName(uint32_t target, uint32_t slot)
		{
			if (this->baseAddr == 0) return "";
			uint32_t holder = 0, count = 0, targets = 0, slots = 0, nslots = 0, set = 0, as = 0, str = 0;
			if (!SafeGuest32(uint32_t(this->baseAddr) + 0x394, holder) || holder == 0) return "";
			if (!SafeGuest32(holder + 0x94, count) || !SafeGuest32(holder + 0x98, targets) || target >= count || count > 8) return "";
			if (!SafeGuest32(targets + 0x34 * target + 0x18, nslots) || slot >= nslots || !SafeGuest32(targets + 0x34 * target + 0x1C, slots)) return "";
			char name[64] = "";
			uint32_t s = slots + 0x68 * slot;
			if (SafeGuest32(s + 0xC, set) && set != 0 && SafeGuest32(s, as) && as != 0 && SafeGuest32(as + 4, str) && str != 0)
				SafeGuestCStr(str, name, sizeof(name));
			return name;
		}

		// The animation hash from getAnimation()'s pointer chain lags behind or goes stale: it kept sending
		// Nml_Move_Run while Link's AS was already "Wait", and Sword_Charge during "CutS1" (16 Nov 2026 test).
		// Link's AS on target 0 is reliable, so the hash is only kept when it is one of the animations that AS
		// can play (LinkASMap, generated from GameROMPlayer's ASList). Otherwise the AS's animation whose name is
		// closest to the last good one is sent (same weapon family), or the AS's default.
		uint32_t lastConsistentAnim = 0;
		uint32_t CorrectAnimation(uint32_t chainHash)
		{
			std::string asName = ReadASName(0, 0);
			const LinkASMap::Entry* e = asName.empty() ? nullptr : LinkASMap::Find(asName.c_str());
			if (!e)
				return chainHash;
			for (int i = 0; i < e->AllCount; i++)
				if (e->All[i] == chainHash)
				{
					lastConsistentAnim = chainHash;
					return chainHash;
				}
			if (e->PlayableCount == 0)
				return chainHash;
			return PickPlayable(e, lastConsistentAnim);
		}

		// The AS's playable clip whose name is closest to the reference clip (same weapon family: Sword_*,
		// Spear_*, Lsword_*...), or the AS's default (first in LinkASMap order).
		static uint32_t PickPlayable(const LinkASMap::Entry* e, uint32_t reference)
		{
			uint32_t best = e->Playable[0];
			if (reference != 0)
			{
				std::string last = AnimNames::Lookup(reference);
				size_t bestLen = 4; // must share more than a short prefix to beat the default
				for (int i = 0; i < e->PlayableCount; i++)
				{
					std::string cand = AnimNames::Lookup(e->Playable[i]);
					size_t n = 0;
					while (n < cand.size() && n < last.size() && cand[n] == last[n]) n++;
					if (n > bestLen) { bestLen = n; best = e->Playable[i]; }
				}
			}
			return best;
		}

		// Upper-body overlay: Link layers shield raise (WaitAttentionUpper / MoveAttentionUpper), horse call,
		// bow draw... on target 1 (upper body), slot 1. Returns the matching clip, or 0 when nothing is layered.
		uint32_t GetUpperAnimation()
		{
			std::string asName = ReadASName(1, 1);
			if (asName.empty())
				return 0;
			const LinkASMap::Entry* e = LinkASMap::Find(asName.c_str());
			if (!e || e->PlayableCount == 0)
				return 0;
			return PickPlayable(e, lastConsistentAnim);
		}

		// Diagnostics: the AS playing on each of Link's animation targets (IDA, 15 Nov 2026).
		// actor+0x394 = AS holder; holder+0x94 = target count, +0x98 = targets (0x34 bytes each);
		// target+0x18 = slot count, +0x1C = slots (0x68 bytes each); slot+0xC != 0 means an AS is
		// set, and *(*slot + 4) is its name (see sub_370E458 / sub_37143EC / sub_37946F0).
		// Link: target 0 = lower body, 1 = upper body, 2 = right arm, 3 = face.
		std::string lastASTargets;
		int asTargetLogs = 0;
		void LogASTargets()
		{
			if (this->baseAddr == 0) return;
			uint32_t holder = 0, count = 0, targets = 0;
			if (!SafeGuest32(uint32_t(this->baseAddr) + 0x394, holder) || holder == 0) return;
			if (!SafeGuest32(holder + 0x94, count) || !SafeGuest32(holder + 0x98, targets) || count == 0 || count > 8) return;
			std::string desc;
			// Fast travel: Link plays "Warp" on target 0 before the stage unloads. Treat it as a real
			// stage change right away (no spawns, drop the saved createActor_ capture). Reusing that
			// capture during a warp crashed Cemu (16 Nov 2026, dump Cemu.exe.19916.dmp).
			{
				uint32_t slots0 = 0, set0 = 0, as0 = 0, str0 = 0;
				char n0[16] = "";
				if (SafeGuest32(targets + 0x1C, slots0) && SafeGuest32(slots0 + 0xC, set0) && set0 != 0 &&
					SafeGuest32(slots0, as0) && as0 != 0 && SafeGuest32(as0 + 4, str0) && str0 != 0 &&
					SafeGuestCStr(str0, n0, sizeof(n0)) && strcmp(n0, "Warp") == 0)
					Main::MarkStageUnstable(10000);
			}
			if (asTargetLogs >= 600) return;
			for (uint32_t t = 0; t < count; t++)
			{
				uint32_t slots = 0, nslots = 0;
				if (!SafeGuest32(targets + 0x34 * t + 0x18, nslots) || !SafeGuest32(targets + 0x34 * t + 0x1C, slots)) continue;
				for (uint32_t s = 0; s < nslots && s < 3; s++)
				{
					uint32_t slot = slots + 0x68 * s, set = 0, as = 0, str = 0;
					char name[64] = "-";
					if (SafeGuest32(slot + 0xC, set) && set != 0 && SafeGuest32(slot, as) && as != 0 &&
						SafeGuest32(as + 4, str) && str != 0)
						SafeGuestCStr(str, name, sizeof(name));
					desc += " [" + std::to_string(t) + "." + std::to_string(s) + "]=" + name;
				}
			}
			if (desc != lastASTargets)
			{
				lastASTargets = desc;
				asTargetLogs++;
				Logging::LoggerService::LogInformation("Link AS targets:" + desc + " | sent " + std::to_string(uint32_t(lastAnimation)), "ASTargets");
			}
		}

		bool resolveAnimation()
		{
			if (this->baseAddr == 0) return false;
			if (this->Animation && this->Animation->isAddressSet()) return true;

			std::vector<int> offsets = { 0x448, 0x4C, 0x10, 0x19C, 0x38, 0xA0, 0x10, -0x5C };
			uint64_t animAddr = Memory::ReadPointers(this->baseAddr, offsets, true) + 0x10;
			if (animAddr >= 30000)
			{
				if (!this->Animation)
					this->Animation = new BigEndian<int>(animAddr, "Memory::LocalInstance::resolveAnimation");
				else
					this->Animation->setAddress(animAddr, "Memory::LocalInstance::resolveAnimation", false);

				int initialVal = this->Animation->get("resolveAnimation");
				std::stringstream ss;
				ss << "LocalInstance::resolveAnimation: Resolved animation address 0x" << std::hex << animAddr
				   << " for Link 0x" << this->baseAddr << " initialVal=" << std::dec << initialVal;
				Logging::LoggerService::LogInformation(ss.str());
				return true;
			}
			return false;
		}

		void setAddress(uint64_t addr)
		{
			Actor::setAddress(addr);
			if (Animation) Animation->setAddress(0, "setAddress");
			if (Health && addr != 0) Health->setAddress(Memory::getBaseAddress() + addr + 0x540, "setAddress", false);
			if (IsEquipped && addr != 0) IsEquipped->setAddress(Memory::ReadPointers(addr, { 0x394, 0xAC, 0xA4 }, true), "setAddress", false);
		}

		void scan()
		{
			Animation = new BigEndian<int>(0, "Memory::LocalInstance::setAddress::Animation");
			resolveAnimation();

			Logging::LoggerService::LogInformation("Scanning game instance...", __FUNCTION__);

			Location = new LocationAccess();
			Equipment = new EquippedItems();
			World->Day = new BigEndian<int>({ 0x7B, 0x4B, 0xE8, 0x51, 0x00, 0x00, 0x00, 0x00, 0x10, 0x29, 0x84, 0xC8 }, -0x04, "Memory::LocalInstance::scan::World->Day", -1, 1);
			AtkUp = new BigEndian<float>({ 0xBF, 0x80, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, -1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, -1, 0x80, 0x00, 0x00 }, 0x10C, "Memory::LocalInstance::scan::AtkUp", -1, 1);
			IsEquipped = new LittleEndian<bool>(this->baseAddr, { 0x394, 0xAC, 0xA4 }, 0x0, "Memory::LocalInstance::scan::IsEquipped");
			Defense = new BigEndian<int>(this->baseAddr, { 0x3b4, 0x0, 0x20, 0x4, 0x28, 0xd4, 0x158, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0xb0, 0x4, 0x4, 0xac, 0x28, 0x4 }, -0xE54, "Memory::LocalInstance::scan::Defense");
			Health = new BigEndian<int>(Memory::getBaseAddress() + this->baseAddr + 0x540, "Memory::LocalInstance::scan::Health");
			NotPaused = new LittleEndian<bool>({ 0x93, 0x9B, 0xE2, 0xB9, -1, -1, -1, -1, 0x10, 0x29, 0x84, 0x10 }, -0x1, "Memory::LocalInstance::scan::NotPaused", 0, 0);

			propHuntFlags.Countdown = new LittleEndian<bool>({ 0x30, 0xA9, 0xAC, 0x57, -1, -1, -1, -1, 0x10, 0x29, 0x84, 0x10 }, -0x1, "LocalInstance::scan::PH_Countdown", 0, 0);

			uint64_t newOffset = (propHuntFlags.Countdown && propHuntFlags.Countdown->isAddressSet() && Memory::RegionStart != 0 && propHuntFlags.Countdown->address > Memory::RegionStart) ? (propHuntFlags.Countdown->address - Memory::RegionStart) : 0;

			propHuntFlags.HidePlayer = new LittleEndian<bool>({ 0x05, 0x22, 0xFA, 0x47, -1, -1, -1, -1, 0x10, 0x29, 0x84, 0x10 }, -0x1, "LocalInstance::scan::PH_HideTimer", newOffset, 0);
			propHuntFlags.HidingPhase = new LittleEndian<bool>({ 0xA8, 0xA5, 0x8F, 0x41, -1, -1, -1, -1, 0x10, 0x29, 0x84, 0x10 }, -0x1, "LocalInstance::scan::PH_HidingPhase", newOffset, 0);
			propHuntFlags.RunningPhase = new LittleEndian<bool>({ 0xB6, 0x68, 0x05, 0x99, -1, -1, -1, -1, 0x10, 0x29, 0x84, 0x10 }, -0x1, "LocalInstance::scan::PH_RunningPhase", newOffset, 0);
			propHuntFlags.ShowPlayer = new LittleEndian<bool>({ 0x60, 0x3C, 0x05, 0xAA, -1, -1, -1, -1, 0x10, 0x29, 0x84, 0x10 }, -0x1, "LocalInstance::scan::PH_ShowPhase", newOffset, 0);
			propHuntFlags.StopMusic = new LittleEndian<bool>({ 0xBC, 0x92, 0x90, 0x4B, -1, -1, -1, -1, 0x10, 0x29, 0x84, 0x10 }, -0x1, "LocalInstance::scan::PH_StopMusic", newOffset, 0);

			std::stringstream scanSummary;
			scanSummary << "[SCAN SUMMARY] LocalInstance: Location=" << (Location && Location->isAddressSet() ? "OK" : "DEFAULT")
				<< ", Equipment=" << (Equipment && Equipment->isAddressSet() ? "OK" : "DEFAULT")
				<< ", Day=" << (World && World->Day && World->Day->isAddressSet() ? "OK" : "DEFAULT")
				<< ", NotPaused=" << (NotPaused && NotPaused->isAddressSet() ? "OK" : "DISABLED")
				<< ", PH_Countdown=" << (propHuntFlags.Countdown && propHuntFlags.Countdown->isAddressSet() ? "OK" : "DISABLED");
			Logging::LoggerService::LogInformation(scanSummary.str(), __FUNCTION__);
			Logging::LoggerService::LogInformation("Scanned game instance successfully.", __FUNCTION__);
		}

		static inline uint32_t CalculateCRC32(const std::string& str)
		{
			uint32_t crc = 0xFFFFFFFF;
			for (char c : str)
			{
				crc ^= (uint8_t)c;
				for (int k = 0; k < 8; k++)
					crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
			}
			return ~crc;
		}

		static inline bool SafeReadU32BE(uint64_t addr, uint32_t& outVal)
		{
			if (addr < 30000) return false;
			__try
			{
				uint32_t raw = *(const uint32_t*)addr;
				outVal = _byteswap_ulong(raw);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		static inline bool SafeReadByte(uint64_t addr, BYTE& outByte)
		{
			if (addr < 30000) return false;
			__try
			{
				outByte = *(const BYTE*)addr;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		static inline void DiagnoseFlagHash(uint32_t hash, const std::string& flagName)
		{
			std::vector<int> hashSig = {
				(int)((hash >> 24) & 0xFF),
				(int)((hash >> 16) & 0xFF),
				(int)((hash >> 8) & 0xFF),
				(int)(hash & 0xFF)
			};
			uint64_t found = Memory::PatternScan(hashSig, Memory::getBaseAddress(), 8, 0);
			if (found >= 30000)
			{
				std::stringstream ss;
				ss << "[DIAGNOSTIC] Hash for '" << flagName << "' (0x" << std::hex << hash << ") FOUND at 0x" << found << ". Context [-16..+16]: ";
				uint64_t startCtx = (found >= 16) ? (found - 16) : found;
				for (uint64_t p = startCtx; p < found + 16; p++)
				{
					BYTE b = 0;
					if (SafeReadByte(p, b))
					{
						ss << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
					}
					else
					{
						ss << "?? ";
					}
				}
				Logging::LoggerService::LogWarning(ss.str(), __FUNCTION__);
			}
			else
			{
				std::stringstream ss;
				ss << "[DIAGNOSTIC] Hash for '" << flagName << "' (0x" << std::hex << hash << ") NOT found in ANY memory region. Flag was never instantiated by GameDataMgr.";
				Logging::LoggerService::LogWarning(ss.str(), __FUNCTION__);
			}
		}

		// ---------------------------------------------------------------------------------------------
		// Event-flow animation strings.
		// The Jugador event scripts (Common.bfevfl) are little-endian files ("ff fe" BOM). On Wii U the
		// game converts them into a separate big-endian copy at load time and runs THAT copy; writing
		// the animation name into the file buffer (what scanPlayerFlags finds) has no effect, which is
		// why every remote player stayed in T-pose. Here we find the big-endian strings
		// (u16 BE length, text, 0) and the DLL writes the animation name there as well.
		// ---------------------------------------------------------------------------------------------
		static inline std::map<int, std::vector<uint64_t>> EvflAnimStrings;    // player -> host addresses of the text
		static inline std::map<int, std::vector<uint64_t>> EvflAttackStrings;
		static inline std::map<int, std::vector<uint64_t>> EvflHoldStrings;   // Demo_ChangeEquipState "EquipState"
		static inline std::map<int, std::vector<uint64_t>> EvflUpperStrings;  // upper-body PlayASForDemo (TargetIndex 2)
		static inline std::mutex EvflMutex;
		static inline bool EvflRuntimeWritesEnabled = true;

		static bool SafeCopyBytes(void* dst, const void* src, size_t n)
		{
			__try { memcpy(dst, src, n); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		// Scans the guest address space once. Returns how many player strings were found.
		static int ScanEventFlowStrings()
		{
			uint64_t base = Memory::getBaseAddress();
			if (base == 0) return 0;

			std::map<int, std::vector<uint64_t>> anim, attack, hold, upper;
			const std::string suffixes[4] = { "_animationthing", "_AttackAnimation", "_Hold", "_upperanimthing" };
			const size_t CHUNK = 0x400000;
			std::vector<BYTE> buf(CHUNK + 64);

			uint64_t addr = base, end = base + 0x100000000ULL;
			MEMORY_BASIC_INFORMATION mbi{};
			while (addr < end && VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)))
			{
				uint64_t regionEnd = (uint64_t)mbi.BaseAddress + mbi.RegionSize;
				bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
					(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY));
				if (readable)
				{
					uint64_t regionStart = (uint64_t)mbi.BaseAddress > addr ? (uint64_t)mbi.BaseAddress : addr;
					for (uint64_t p = regionStart; p < regionEnd; p += CHUNK)
					{
						uint64_t remaining = regionEnd - p;
						size_t n = (size_t)(remaining < CHUNK + 64 ? remaining : CHUNK + 64);
						if (!SafeCopyBytes(buf.data(), (const void*)p, n)) continue;
						for (int s = 0; s < 4; s++)
						{
							const std::string& suf = suffixes[s];
							auto it = buf.begin();
							auto last = buf.begin() + n;
							while ((it = std::search(it, last, suf.begin(), suf.end())) != last)
							{
								size_t pos = (size_t)(it - buf.begin());
								size_t endPos = pos + suf.size();
								it++;
								if (endPos >= n || buf[endPos] != 0) continue;
								// walk back over "Jugador<digits>"
								size_t d = pos;
								while (d > 0 && buf[d - 1] >= '0' && buf[d - 1] <= '9') d--;
								if (d == pos || d < 9 || memcmp(&buf[d - 7], "Jugador", 7) != 0) continue;
								size_t textStart = d - 7;
								size_t textLen = endPos - textStart;
								// big-endian u16 length right before the text = the runtime copy
								if (buf[textStart - 2] != (BYTE)(textLen >> 8) || buf[textStart - 1] != (BYTE)(textLen & 0xFF)) continue;
								int player = atoi(std::string(buf.begin() + d, buf.begin() + pos).c_str());
								if (player < 1 || player > 32) continue;
								(s == 0 ? anim : s == 1 ? attack : s == 2 ? hold : upper)[player].push_back(p + textStart);
							}
						}
					}
				}
				addr = regionEnd;
			}

			int found = 0;
			{
				std::lock_guard<std::mutex> lock(EvflMutex);
				EvflAnimStrings = anim;
				EvflAttackStrings = attack;
				EvflHoldStrings = hold;
				EvflUpperStrings = upper;
				found = (int)anim.size();
			}
			std::stringstream ss;
			ss << "Event-flow runtime strings: animation for " << anim.size() << " players, attack for " << attack.size() << ", hold for " << hold.size() << ", upper body for " << upper.size();
			if (anim.count(1)) ss << " (Jugador1 x" << anim[1].size() << " @0x" << std::hex << anim[1][0] << ")";
			Logging::LoggerService::LogInformation(ss.str(), "AnimFlagDiag");
			return found;
		}

		// Writes "text" over a runtime event-flow string, zero-padded to the placeholder's length.
		// The u16 length in front of the string is left untouched: the AI param loader copies the text as
		// a C string (PlayASForDemo param setup, sub_2358BDC -> sub_30EC1B8 in v208), while the event
		// system relies on that length for its own bookkeeping; changing it made the game delete the actor.
		static void WriteEvflString(uint64_t textAddr, const std::string& text, size_t originalLen)
		{
			if (text.size() >= originalLen) return;
			std::vector<BYTE> data(originalLen, 0);
			memcpy(data.data(), text.data(), text.size());
			SafeCopyBytes((void*)textAddr, data.data(), data.size());
		}

		// Writes into every runtime copy of a player's placeholder. kind: 0 = animation (ASName),
		// 1 = attack (action name), 2 = hold/equip state. Rescans (at most every 15 s) when none are known
		// yet, e.g. the scripts weren't loaded at connect time.
		// Test switch, read from %APPDATA%\BOTWMnim_mode.txt (re-read every 5 s):
		//   off   - never write the runtime event script (old behaviour: visible, T-pose)
		//   idle  - always write the idle animation Anim_2327397999 = Nml_Wait (no attacks/hold)
		//   full  - write the real animation, attacks and hold state (default)
		//   test  - animation test mode, see AnimDebug.h (remote players cycle through the ASList)
		static int AnimMode()
		{
			static std::atomic<int> mode{ 2 };
			static std::atomic<DWORD> lastRead{ 0 };
			DWORD now = GetTickCount();
			if (now - lastRead.load() > 5000)
			{
				lastRead = now;
				char* appdata = nullptr; size_t len = 0;
				if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata)
				{
					std::ifstream f(std::string(appdata) + "\\BOTWM\\anim_mode.txt");
					free(appdata);
					std::string v;
					if (f >> v)
					{
						int m = v == "off" ? 0 : v == "idle" ? 1 : v == "test" ? 3 : 2;
						if (m != mode.load())
							Logging::LoggerService::LogInformation("anim_mode.txt: " + v, "AnimFlagDiag");
						mode = m;
					}
				}
			}
			return mode.load();
		}

		static void WriteEvflAnimation(int player, const std::string& textIn, int kind)
		{
			if (!EvflRuntimeWritesEnabled) return;
			int mode = AnimMode();
			if (mode == 0) return;
			if (mode == 1 && kind != 0) return;
			const std::string text = mode == 1 ? std::string("Anim_2327397999") : textIn;
			static std::atomic<DWORD> lastScan{ 0 };
			std::vector<uint64_t> targets;
			{
				std::lock_guard<std::mutex> lock(EvflMutex);
				auto& table = kind == 1 ? EvflAttackStrings : kind == 2 ? EvflHoldStrings : kind == 3 ? EvflUpperStrings : EvflAnimStrings;
				if (table.count(player)) targets = table[player];
			}
			if (targets.empty())
			{
				DWORD now = GetTickCount();
				if (now - lastScan.load() < 15000) return;
				lastScan = now;
				ScanEventFlowStrings();
				std::lock_guard<std::mutex> lock(EvflMutex);
				auto& table = kind == 1 ? EvflAttackStrings : kind == 2 ? EvflHoldStrings : kind == 3 ? EvflUpperStrings : EvflAnimStrings;
				if (table.count(player)) targets = table[player];
			}
			size_t originalLen = ("Jugador" + std::to_string(player) + (kind == 1 ? "_AttackAnimation" : kind == 2 ? "_Hold" : kind == 3 ? "_upperanimthing" : "_animationthing")).size();
			for (uint64_t t : targets)
				WriteEvflString(t, text, originalLen);
		}

		// Pins each player's animation flag to the live value of its "Jugador<N>_animationthing" GameData
		// string flag (see layout note inside). Players whose flag can't be located keep the walked address.
		static void FixAnimationFlagsExact(std::map<int, FlagAddresses>& result, uint64_t searchBase)
		{
			const uint64_t start = searchBase >= 0x8000 ? searchBase - 0x8000 : searchBase;
			const size_t size = 0x30000;
			std::vector<BYTE> chunk(size, 0);
			// Read page by page so one unreadable page doesn't lose the whole window
			for (size_t page = 0; page < size; page += 0x1000)
			{
				std::vector<BYTE> bytes = Memory::read_bytes(start + page, 0x1000, "FixAnimationFlagsExact");
				if (bytes.size() == 0x1000)
					memcpy(chunk.data() + page, bytes.data(), 0x1000);
			}

			// A string32 GameData flag (ksys::gdt::Flag<FixedSafeString<32>>, 0xC0 bytes on Wii U) holds
			// FlagConfig { initial, min, max } followed by the live value; each FixedSafeString<32> is
			// 0x2C bytes, so the live value's text is 0x84 after the initial value's text. Both start as
			// "Jugador<N>_animationthing", and only writes to the live value change the animation.
			const size_t VALUE_AFTER_INITIAL = 0x84;
			auto textAt = [&](size_t pos) -> std::string {
				if (pos >= chunk.size()) return "";
				size_t end = pos;
				while (end < chunk.size() && end - pos < 40 && chunk[end] != 0) end++;
				return std::string(chunk.begin() + pos, chunk.begin() + end);
			};
			auto isAnimValue = [](const std::string& s, const std::string& needle) {
				return s == needle || s.rfind("Anim_", 0) == 0 || s.rfind("Attack_", 0) == 0;
			};

			// Diagnostics: show where players 1, 2 and 10 resolved and what memory looks like there.
			auto dumpAround = [&](uint64_t addr, const char* label) {
				std::stringstream ss;
				ss << label << " @0x" << std::hex << addr << ": ";
				for (int i = -0x40; i < 0xC0; i++)
				{
					BYTE b = 0;
					if (addr + i >= start && addr + i < start + size) b = chunk[(size_t)(addr + i - start)];
					ss << (char)((b >= 0x20 && b < 0x7F) ? b : '.');
				}
				Logging::LoggerService::LogInformation(ss.str(), "AnimFlagDiag");
			};
			for (int n : { 1, 2, 10 })
			{
				std::string needle = "Jugador" + std::to_string(n) + "_animationthing";
				std::stringstream ss;
				ss << "Jugador" << n << " walked=0x" << std::hex << result[n].AnimationFlag << " matches:";
				for (auto it = chunk.begin(); (it = std::search(it, chunk.end(), needle.begin(), needle.end())) != chunk.end(); ++it)
					ss << " 0x" << (start + (uint64_t)(it - chunk.begin()));
				Logging::LoggerService::LogInformation(ss.str(), "AnimFlagDiag");
				if (result[n].AnimationFlag >= start && result[n].AnimationFlag < start + size)
					dumpAround(result[n].AnimationFlag, ("Jugador" + std::to_string(n) + " walked").c_str());
			}

			int fixedCount = 0;
			for (int n = 1; n <= 32; n++)
			{
				std::string needle = "Jugador" + std::to_string(n) + "_animationthing";

				// Find this player's initial-value copy (exact text, zero before and after)
				auto it = std::search(chunk.begin(), chunk.end(), needle.begin(), needle.end());
				while (it != chunk.end() && (it + needle.size() == chunk.end() || *(it + needle.size()) != 0 ||
					(it != chunk.begin() && *(it - 1) != 0)))
					it = std::search(it + 1, chunk.end(), needle.begin(), needle.end());
				if (it == chunk.end())
					continue; // not in this window: keep the walked address

				size_t initialPos = (size_t)(it - chunk.begin());
				size_t valuePos = initialPos + VALUE_AFTER_INITIAL;
				if (!isAnimValue(textAt(valuePos), needle))
					continue; // layout not as expected: keep the walked address

				uint64_t exact = start + valuePos;
				if (result[n].AnimationFlag != exact)
				{
					std::stringstream ss;
					ss << "Animation flag for Jugador" << n << " corrected from 0x" << std::hex << result[n].AnimationFlag << " to 0x" << exact;
					Logging::LoggerService::LogInformation(ss.str(), __FUNCTION__);
				}
				result[n].AnimationFlag = exact;
				fixedCount++;
			}
			Logging::LoggerService::LogInformation("Animation flags corrected by exact search: " + std::to_string(fixedCount), __FUNCTION__);
		}

		std::map<int, FlagAddresses> scanPlayerFlags()
		{
			const int BOOL_OFFSET = 0x10;
			const int STR32_OFFSET = 0xC0;
			const int INT_OFFSET = 0x20;
			const int ANIM_OFFSET = 0x220;
			const int EQUIP_OFFSET = 0x98;
			const int EQUIP_EXTRA_OFFSET = 0x8;

			std::map<int, FlagAddresses> result;

			Logging::LoggerService::LogInformation("Scanning game flags...", __FUNCTION__);

			for (int i = 1; i < 33; i++)
				result[i] = FlagAddresses();

			std::vector<int> sig = { 0x82, 0xCB, 0xCC, 0x60, -1, -1, -1, -1, 0x10, 0x29, 0x84, 0x10 }; // Jugador10_DispNameFlag
			std::vector<int> FlagOrder1 = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 1, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 2, 30, 31, 32, 3, 4, 5, 6, 7, 8, 9 };
			std::vector<int> FlagOrder2 = { 1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 2, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 3, 30, 31, 32, 4, 5, 6, 7, 8, 9 };

			uint64_t rawAddr = Memory::PatternScan(sig, Memory::getBaseAddress(), 8, 0);
			if (rawAddr >= 30000)
			{
				uint64_t addr = rawAddr - 0x1;
				int verifiedDisp = 0, verifiedExists = 0;
				for (size_t i = 0; i < FlagOrder1.size(); i++)
				{
					int pNum = FlagOrder1[i];
					uint32_t expDispHash = CalculateCRC32("Jugador" + std::to_string(pNum) + "_DispNameFlag");
					uint32_t actualDispHash = 0;
					if (SafeReadU32BE(addr + 1, actualDispHash) && actualDispHash == expDispHash)
					{
						result[pNum].DispNameFlag = addr;
						verifiedDisp++;
					}
					else
					{
						result[pNum].DispNameFlag = 0;
					}
					addr += BOOL_OFFSET;

					uint32_t expExistsHash = CalculateCRC32("Jugador" + std::to_string(pNum) + "_Exists");
					uint32_t actualExistsHash = 0;
					if (SafeReadU32BE(addr + 1, actualExistsHash) && actualExistsHash == expExistsHash)
					{
						result[pNum].ExistsFlag = addr;
						verifiedExists++;
					}
					else
					{
						result[pNum].ExistsFlag = 0;
					}
					addr += BOOL_OFFSET;
				}
				Logging::LoggerService::LogInformation("DispName (" + std::to_string(verifiedDisp) + "/32) & Exists (" + std::to_string(verifiedExists) + "/32) flags validated via CRC32.", __FUNCTION__);
			}
			else
			{
				Logging::LoggerService::LogWarning("DispName & Exists anchor not found in region 8; running diagnostic...", __FUNCTION__);
				DiagnoseFlagHash(0x82cbcc60, "Jugador10_DispNameFlag");
			}

			sig = { 0x87, 0x68, 0xFA, 0x81, -1, -1, -1, -1, 0x10, 0x29, 0x84, 0xC8 }; // Jugador10_Status
			rawAddr = Memory::PatternScan(sig, Memory::getBaseAddress(), 8, 0);
			if (rawAddr >= 30000)
			{
				uint64_t addr = rawAddr - 0x4;
				int verifiedStatus = 0;
				for (size_t i = 0; i < FlagOrder1.size(); i++)
				{
					int pNum = FlagOrder1[i];
					uint32_t expStatusHash = CalculateCRC32("Jugador" + std::to_string(pNum) + "_Status");
					uint32_t actualStatusHash = 0;
					if (SafeReadU32BE(addr + 4, actualStatusHash) && actualStatusHash == expStatusHash)
					{
						result[pNum].StatusFlag = addr;
						verifiedStatus++;
					}
					else
					{
						result[pNum].StatusFlag = 0;
					}
					addr += INT_OFFSET;
				}
				Logging::LoggerService::LogInformation("Status flags validated via CRC32 (" + std::to_string(verifiedStatus) + "/32).", __FUNCTION__);
			}
			else
			{
				Logging::LoggerService::LogWarning("Status anchor not found in region 8; running diagnostic...", __FUNCTION__);
				DiagnoseFlagHash(0x8768fa81, "Jugador10_Status");
			}

			sig = { 0xF2, 0xA5, 0xD4, 0xA2, -1, -1, -1, -1, 0x10, 0x29, 0x86, 0x38 }; // Jugador10_Name
			rawAddr = Memory::PatternScan(sig, Memory::getBaseAddress(), 8, 0);
			if (rawAddr >= 30000)
			{
				uint64_t addr = rawAddr - 0x20;
				int verifiedName = 0;
				for (size_t i = 0; i < FlagOrder1.size(); i++)
				{
					int pNum = FlagOrder1[i];
					uint32_t expNameHash = CalculateCRC32("Jugador" + std::to_string(pNum) + "_Name");
					uint32_t actualNameHash = 0;
					if (SafeReadU32BE(addr + 0x20, actualNameHash) && actualNameHash == expNameHash)
					{
						result[pNum].NameFlag = addr;
						verifiedName++;
					}
					else
					{
						result[pNum].NameFlag = 0;
					}
					addr += STR32_OFFSET;
				}
				Logging::LoggerService::LogInformation("Name flags validated via CRC32 (" + std::to_string(verifiedName) + "/32).", __FUNCTION__);
			}
			else
			{
				Logging::LoggerService::LogWarning("Name anchor not found in region 8; running diagnostic...", __FUNCTION__);
				DiagnoseFlagHash(0xf2a5d4a2, "Jugador10_Name");
			}

			// Hold & Animation flags (Optional eventflow strings)
			sig = { 0x0D, -1, 0x4A, 0x75, 0x67, 0x61, 0x64, 0x6F, -1, 0x31, 0x5F, 0x48, 0x6F, 0x6C, 0x64 }; // Jugador1_Hold
			uint64_t holdRaw = Memory::PatternScan(sig, Memory::getBaseAddress(), 8);
			if (holdRaw < 30000)
			{
				std::vector<int> sigShort = { 0x4A, 0x75, 0x67, 0x61, 0x64, 0x6F, 0x72, 0x31, 0x5F, 0x48, 0x6F, 0x6C, 0x64 };
				holdRaw = Memory::PatternScan(sigShort, Memory::getBaseAddress(), 8);
			}

			if (holdRaw >= 30000)
			{
				uint64_t addr = holdRaw + 0x1;
				for (int i = 1; i < 33; i++)
				{
					result[i].HoldFlag = addr;
					addr += EQUIP_OFFSET;
					if (i > 9)
						addr += EQUIP_EXTRA_OFFSET;
				}

				addr += 0x1000;
				const uint64_t animSearchBase = addr;
				std::vector<BYTE> animthing = { 0x4A, 0x75, 0x67, 0x61, 0x64, 0x6F, 0x72, 0x31 };
				uint64_t offset = 0;

				while (offset < 0x8000)
				{
					if (Memory::read_bytes(addr + offset, 8, __FUNCTION__) == animthing)
					{
						addr = addr + offset;
						break;
					}
					offset++;
				}

				if (offset < 0x8000 && addr >= 30000)
				{
					for (int i = 1; i < 34; i++)
					{
						if (Memory::read_string(addr + 8, 1, __FUNCTION__) == "_")
							result[std::stoi(Memory::read_string(addr + 7, 1, __FUNCTION__))].AnimationFlag = addr;
						else
							result[std::stoi(Memory::read_string(addr + 7, 2, __FUNCTION__))].AnimationFlag = addr;

						uint64_t off = 25;
						if (i == 33) break;

						while (off < 0x1000)
						{
							if (Memory::read_string(addr + off, 14, __FUNCTION__) == "animationthing")
							{
								if (Memory::read_string(addr + off - 3, 1, __FUNCTION__) == "r")
									addr = addr + off - 9;
								else
									addr = addr + off - 10;
								break;
							}
							off++;
						}
					}

					// The walk above starts at the first "Jugador1" it meets, which can be another
					// player's string (e.g. "Jugador10_animationthing"); player 1 then got a wrong
					// address and stayed in T-pose. Pin every player to its exact default value.
					FixAnimationFlagsExact(result, animSearchBase);

					offset = 0;
					std::vector<BYTE> attack = { 0x5F, 0x41, 0x74, 0x74, 0x61, 0x63, 0x6B, 0x41, 0x6E, 0x69, 0x6D, 0x61, 0x74, 0x69, 0x6F, 0x6E };
					while (offset < 0x20000)
					{
						if (Memory::read_bytes(addr + offset, attack.size(), __FUNCTION__) == attack)
						{
							addr = addr + offset;
							break;
						}
						offset++;
					}

					if (offset < 0x20000 && addr >= 30000)
					{
						for (int i = 1; i < 33; i++)
						{
							if (Memory::read_bytes(addr - 0x9, 1, __FUNCTION__)[0] == 0x18)
							{
								int pNum = std::stoi(Memory::read_string(addr - 0x1, 1, __FUNCTION__));
								result[pNum].AttackAnimation = addr - 0x8;
								addr += 29;
							}
							else
							{
								int pNum = std::stoi(Memory::read_string(addr - 0x2, 2, __FUNCTION__));
								result[pNum].AttackAnimation = addr - 0x9;
								addr += 28;
							}
						}
					}
				}
				Logging::LoggerService::LogInformation("Hold & Animation flags mapped successfully.", __FUNCTION__);
				ScanEventFlowStrings(); // the strings the game actually runs (see ScanEventFlowStrings)
			}
			else
			{
				Logging::LoggerService::LogWarning("Hold & Animation strings not found; defaulting to 0 (non-fatal).", __FUNCTION__);
			}

			// MapPin (Optional)
			sig = { 0x06, 0x46, 0x40, 0xD2, 0x00, 0x45, 0x12, 0x98 };
			uint64_t mapAddr = Memory::PatternScan(sig, Memory::getBaseAddress(), 8);
			if (mapAddr >= 30000)
			{
				mapAddr += 0x1;
				for (int i = 0; i < 32; i++)
				{
					result[FlagOrder2[i]].MapPin = mapAddr;
					uint64_t offset = 0x40;
					std::vector<BYTE> MapPinBytes = { 0x46, 0x40, 0xD2, 0x00, 0x45, 0x12, 0x98, 0x00 };
					while (offset < 0x1000)
					{
						if (Memory::read_bytes(mapAddr + offset, 8, __FUNCTION__) == MapPinBytes)
						{
							mapAddr = mapAddr + offset;
							break;
						}
						offset += 0x4;
					}
				}
				Logging::LoggerService::LogInformation("MapPin flags mapped successfully.", __FUNCTION__);
			}
			else
			{
				Logging::LoggerService::LogWarning("MapPin address not found in region 8; defaulting to 0 (non-fatal).", __FUNCTION__);
			}

			int dispFound = 0, existsFound = 0, statusFound = 0, nameFound = 0, mapFound = 0, animFound = 0;
			for (int i = 1; i <= 32; i++)
			{
				if (result[i].DispNameFlag >= 30000) dispFound++;
				if (result[i].ExistsFlag >= 30000) existsFound++;
				if (result[i].StatusFlag >= 30000) statusFound++;
				if (result[i].NameFlag >= 30000) nameFound++;
				if (result[i].MapPin >= 30000) mapFound++;
				if (result[i].AnimationFlag >= 30000) animFound++;
			}
			std::stringstream sum;
			sum << "[SCAN SUMMARY] Player Flags: DispName=" << dispFound << "/32, Exists=" << existsFound
				<< "/32, Status=" << statusFound << "/32, Name=" << nameFound << "/32, MapPin=" << mapFound
				<< "/32, Anim=" << animFound << "/32";
			Logging::LoggerService::LogInformation(sum.str(), __FUNCTION__);

			Logging::LoggerService::LogInformation("Scanned game flags successfully.", __FUNCTION__);
			return result;
		}

		DTO::ClientDTO* get(bool ServicesStarted)
		{
			DTO::ClientDTO* result = new DTO::ClientDTO();

			int LastHealth = (this->Health && this->Health->isAddressSet()) ? this->Health->LastKnown : 0;

			result->WorldData = this->World ? this->World->get() : new DTO::WorldDTO();
			result->PlayerData = get_characterData();
			if (ServicesStarted)
			{
				result->EnemyData = EnemyService ? EnemyService->UpdateHealth() : new DTO::EnemyDTO(); //TODO: Implement enemy and quest sync

				if (float(LastQuestUpdate - GetTickCount()) > 5000)
				{
					result->QuestData = QuestService ? QuestService->UpdateQuests() : new DTO::QuestDTO();
					LastQuestUpdate = GetTickCount();
				}
				else
				{
					result->QuestData = new DTO::QuestDTO();
				}

			}
			else
			{
				result->EnemyData = new DTO::EnemyDTO();
				result->QuestData = new DTO::QuestDTO();
			}

			if (this->Health && this->Health->isAddressSet() && this->Health->LastKnown != LastHealth)
				LastHealthUpdate = GetTickCount();

			return result;
		}

		bool IsPaused()
		{
			PauseMutex.lock();
			bool res = this->IsGamePaused;
			PauseMutex.unlock();

			return res;
		}

	private:
		DTO::ClientCharacterDTO* get_characterData()
		{
			DTO::ClientCharacterDTO* result = new DTO::ClientCharacterDTO();

			if (!this->Position || !this->Position->isAddressSet())
			{
				this->resolvePosition();
			}

			if (!this->Animation || !this->Animation->isAddressSet())
			{
				this->resolveAnimation();
			}

			result->Position = this->Position->get(__FUNCTION__);
			result->Rotation1 = this->Rotation1->get(__FUNCTION__);
			result->Rotation2 = this->Rotation2->get(__FUNCTION__);
			result->Rotation3 = this->Rotation3->get(__FUNCTION__);
			result->Rotation4 = this->Rotation4->get(__FUNCTION__);
			result->Animation = (int)this->CorrectAnimation((uint32_t)this->getAnimation());
			result->UpperAnimation = (int)this->GetUpperAnimation();
			LogASTargets();
			result->Health = (this->Health && this->Health->isAddressSet()) ? this->Health->get(__FUNCTION__) : 0;
			result->AtkUp = (this->AtkUp && this->AtkUp->isAddressSet()) ? this->AtkUp->get(__FUNCTION__) : 0;
			result->IsEquipped = (this->IsEquipped && this->IsEquipped->isAddressSet()) ? this->IsEquipped->get(__FUNCTION__) : false;
			result->Equipment = this->Equipment->getEquipment(__FUNCTION__);
			result->Location = this->Location->get(__FUNCTION__);
			result->Bomb = this->Bomb->get(__FUNCTION__);
			result->Bomb2 = this->Bomb2->get(__FUNCTION__);
			result->BombCube = this->BombCube->get(__FUNCTION__);
			result->BombCube2 = this->BombCube2->get(__FUNCTION__);

			// A big jump of the local player means a teleport / fast travel: the next spawn must use
			// freshly captured createActor_ parameters (only a flag, live actors are left alone).
			Vec3f pos = result->Position;
			if (pos.x() != 0 || pos.y() != 0 || pos.z() != 0)
			{
				if (hasLastTeleportCheckPos)
				{
					float dx = pos.x() - lastTeleportCheckPos.x(), dy = pos.y() - lastTeleportCheckPos.y(), dz = pos.z() - lastTeleportCheckPos.z();
					if (dx * dx + dy * dy + dz * dz > 50.0f * 50.0f)
						Main::MarkStageUnstable(5000);
				}
				lastTeleportCheckPos = pos;
				hasLastTeleportCheckPos = true;
			}

			return result;
		}

		std::map<std::string, int> readWeaponDamages()
		{
			std::map<std::string, int> result = {};
			try
			{
				char* appdata = nullptr;
				size_t sz = 0;
				if (_dupenv_s(&appdata, &sz, "APPDATA") == 0 && appdata != nullptr)
				{
					std::string filepath = std::string(appdata) + "\\BOTWM\\WeaponDamages.txt";
					free(appdata);

					std::ifstream file(filepath);
					if (file.is_open())
					{
						std::stringstream buffer;
						buffer << file.rdbuf();
						std::string content = buffer.str();

						rapidjson::Document doc;
						doc.Parse(content.c_str());

						if (!doc.HasParseError() && doc.IsObject())
						{
							for (auto iter = doc.MemberBegin(); iter != doc.MemberEnd(); ++iter)
							{
								if (iter->name.IsString() && iter->value.IsInt())
								{
									result.insert({ iter->name.GetString(), iter->value.GetInt() });
								}
							}
						}
					}
				}
			}
			catch (...)
			{
			}
			return result;
		}
	};
}