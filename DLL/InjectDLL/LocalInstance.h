#pragma once

#include "Actor.h"

using namespace DataTypes;

namespace MemoryAccess
{
	class WorldObj
	{
	public:
		BigEndian<int>* Day;
		BigEndian<float>* WritableTime;
		BigEndian<float>* ReadableTime;
		int Weather = 0;
		int LocalWeather = 0;

		void set(DTO::WorldDTO* input)
		{
			Mutex.lock();

			int currentDay = this->Day->get(__FUNCTION__);
			float currentTime = this->ReadableTime->get(__FUNCTION__);

			if (currentTime < 350 && (input->Day > currentDay || currentDay - input->Day > 1 || (currentDay == input->Day && input->Time - currentTime > 2)))
			{
				this->Day->set(input->Day, __FUNCTION__);
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

			result->Day = this->Day->get(__FUNCTION__);

			result->Time = this->ReadableTime->get(__FUNCTION__);

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
			LittleEndian<bool>* HidingPhase;
			LittleEndian<bool>* RunningPhase;
			LittleEndian<bool>* Countdown;
			LittleEndian<bool>* HidePlayer;
			LittleEndian<bool>* ShowPlayer;
			LittleEndian<bool>* StopMusic;
		};

		BigEndian<int>* Health;
		BigEndian<int>* Defense;
		BigEndian<float>* AtkUp;
		LittleEndian<bool>* IsEquipped;
		LocationAccess* Location;
		EquippedItems* Equipment;
		BigEndian<int>* Animation;
		BombAccess* Bomb = new BombAccess();
		BombAccess* Bomb2 = new BombAccess();
		BombAccess* BombCube = new BombAccess();
		BombAccess* BombCube2 = new BombAccess();
		EnemyAccess* EnemyService = new EnemyAccess();
		QuestAccess* QuestService = new QuestAccess();

		std::shared_mutex PauseMutex;

		WorldObj* World = new WorldObj();

		LittleEndian<bool>* NotPaused;

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

		void setAddress(uint64_t addr)
		{
			Actor::setAddress(addr);
		}

		void scan()
		{
			Animation = new BigEndian<int>(this->baseAddr, { 0x448, 0x4C, 0x10, 0x19C, 0x38, 0xA0, 0x10, -0x5C }, 0x10, "Memory::LocalInstance::setAddress::Animation");

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

			int LastHealth = this->Health->LastKnown;

			result->WorldData = this->World->get();
			result->PlayerData = get_characterData();
			if (ServicesStarted)
			{
				result->EnemyData = EnemyService->UpdateHealth(); //TODO: Implement enemy and quest sync

				if (float(LastQuestUpdate - GetTickCount()) > 5000)
				{
					result->QuestData = QuestService->UpdateQuests();
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

			if(this->Health->LastKnown != LastHealth)
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

			result->Position = this->Position->get(__FUNCTION__);
			result->Rotation1 = this->Rotation1->get(__FUNCTION__);
			result->Rotation2 = this->Rotation2->get(__FUNCTION__);
			result->Rotation3 = this->Rotation3->get(__FUNCTION__);
			result->Rotation4 = this->Rotation4->get(__FUNCTION__);
			result->Animation = this->Animation->get(__FUNCTION__);
			result->Health = this->Health->get(__FUNCTION__);
			result->AtkUp = this->AtkUp->get(__FUNCTION__);
			result->IsEquipped = this->IsEquipped->get(__FUNCTION__);
			result->Equipment = this->Equipment->getEquipment(__FUNCTION__);
			result->Location = this->Location->get(__FUNCTION__);
			result->Bomb = this->Bomb->get(__FUNCTION__);
			result->Bomb2 = this->Bomb2->get(__FUNCTION__);
			result->BombCube = this->BombCube->get(__FUNCTION__);
			result->BombCube2 = this->BombCube2->get(__FUNCTION__);

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