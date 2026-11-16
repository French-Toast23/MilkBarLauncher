#pragma once

#include <shared_mutex>
#include "KeyCodeActor.h"
#include "BotwEdit.h"
#include <map>
#include "ActorData.h"
#include "Memory.h"
#include "dllmain_Variables.h"
#include "Entities.h"
#include "Game.h"

// This stuff here was yoinked from BetterVR
// -----------------------------------------
	extern union FPR_t {
		double fpr;
		struct
		{
			double fp0;
			double fp1;
		};
		struct
		{
			uint64_t guint;
		};
		struct
		{
			uint64_t fp0int;
			uint64_t fp1int;
		};
	};

	extern struct PPCInterpreter_t {
		uint32_t instructionPointer;
		uint32_t gpr[32];
		FPR_t fpr[32];
		uint32_t fpscr;
		uint8_t crNew[32]; // 0 -> bit not set, 1 -> bit set (upper 7 bits of each byte must always be zero) (cr0 starts at index 0, cr1 at index 4 ..)
		uint8_t xer_ca;  // carry from xer
		uint8_t LSQE;
		uint8_t PSE;
		// thread remaining cycles
		int32_t remainingCycles; // if this value goes below zero, the next thread is scheduled
		int32_t skippedCycles; // if remainingCycles is set to zero to immediately end thread execution, this value holds the number of skipped cycles
		struct
		{
			uint32_t LR;
			uint32_t CTR;
			uint32_t XER;
			uint32_t UPIR;
			uint32_t UGQR[8];
		}sprNew;
	};
	// -----------------------------------------

	typedef void (*osLib_registerHLEFunctionType)(const char* libraryName, const char* functionName, void(*osFunction)(PPCInterpreter_t* hCPU));
	typedef uint32_t (*osLib_getHLEFunctionAddressType)(const char* libraryName, const char* functionName);


#pragma pack(1)
	extern struct TransferableData { // This is reversed compared to the gfx pack because we read as big endian.
		int f_r10;
		int f_r9;
		int f_r8;
		int f_r7;
		int f_r6;
		int f_r5;
		int f_r4;
		int f_r3;

		int ringPtr;

		int fnAddr;

		byte bytepadding[2];
		bool interceptRegisters;

		bool enabled;
	};

#pragma pack(1)
	extern struct InstanceData {
		char name[152]; // We'll allocate all unused storage for use for name storage.. just in case of a really long actor name
		uint8_t actorStorage[104];
	};

	extern struct QueueActor {
		float PosX;
		float PosY;
		float PosZ;
		std::string Name;
	};

// ---------------------------------------------------------------------------------
// This is an example function call.
// - Feel free to expand / change -
// Note: This does not take into account stuff like actually setting desired params.
// ---------------------------------------------------------------------------------
std::map<char, std::vector<KeyCodeActor>> keyCodeMap;

std::shared_mutex keycode_mutex;
std::shared_mutex queue_mutex;
std::shared_mutex data_mutex;

std::map<char, bool> prevKeyStateMap; // Used for key press logic - keeps track of previous key state

std::vector<QueueActor> queuedActors;

// createActor_ is called with parameters captured from the game's own last createActor_ call.
// Those point into the current stage, so after a stage change / teleport the next spawn waits for
// a fresh capture; reusing old ones created random actors (a Hyrule Bass) instead of the player.
static bool s_needFreshCapture = true;

// Set when the game tears down a stage (teleport/fast travel). Spawning while the old stage is being
// destroyed crashed Cemu, so createActor_ is held off until this time has passed.
static DWORD s_stageUnstableUntil = 0;

// Last good capture, kept when a remote actor vanishes on a stage that didn't change (soft reset).
// If the game creates no actor of its own for a while (player standing still), the respawn reuses it
// instead of stalling forever. A real stage change (Link erased, teleport) drops it.
static bool s_haveCaptureBackup = false;
static int s_backupR3 = 0, s_backupR5 = 0, s_backupR7 = 0;
static DWORD s_captureWaitSince = 0;
static bool s_keepBackupOnNextReset = false;

namespace Main {
	void RequestFreshSpawnCapture() { s_needFreshCapture = true; s_keepBackupOnNextReset = false; }

	// keepCaptureBackup: the stage itself did not change (a remote actor vanished), see above.
	void MarkStageUnstable(DWORD ms, bool keepCaptureBackup)
	{
		if (!keepCaptureBackup || !s_needFreshCapture)
			s_keepBackupOnNextReset = keepCaptureBackup;
		s_needFreshCapture = true;
		DWORD until = GetTickCount() + ms;
		if ((int)(until - s_stageUnstableUntil) > 0)
			s_stageUnstableUntil = until;
	}

	bool IsStageUnstable() { return (int)(s_stageUnstableUntil - GetTickCount()) > 0; }
}

static uint32_t s_startTrnsDataGuestAddr = 0;
static uint32_t s_storageStartGuestAddr = 0;
static uint32_t s_storageEndGuestAddr = 0;

MemoryInstance* memInstance;

bool isSetup = false;

void setup(PPCInterpreter_t* hCPU, uint32_t startTrnsData, uint64_t baseAddress) {

	TransferableData trnsData;
	data_mutex.lock(); //////////////////////////////////////////////////
	memInstance->memory_readMemoryBE(startTrnsData, &trnsData, baseAddress); // Just make sure to intercept stuff..
	trnsData.interceptRegisters = true;
	memInstance->memory_writeMemoryBE(startTrnsData, trnsData, baseAddress);
	data_mutex.unlock(); //===============================================

	isSetup = true;
}

void queueActor(int playerNumber, float Position[3])
{
	if (queuedActors.size() > 0)
		for (int i = 0; i < queuedActors.size(); i++)
			if (queuedActors[i].Name == "Jugador" + std::to_string(playerNumber))
				return;

	//for (int j = queuedActors.size() - 1; j >= 0; j--)
	//{
	//	if (queuedActors[j].Name == "Jugador" + std::to_string(playerNumber))
	//	{
	//		return;
	//	}
	//}

	QueueActor queueActor;
	std::string actorName = "Jugador";
	actorName.append(std::to_string(playerNumber));
	queueActor.Name = actorName;
	queueActor.PosX = Position[0];
	queueActor.PosY = Position[1];
	queueActor.PosZ = Position[2];

	queuedActors.push_back(queueActor);
	Logging::LoggerService::LogInformation("Queued player actor '" + actorName + "' at (" + std::to_string(Position[0]) + ", " + std::to_string(Position[1]) + ", " + std::to_string(Position[2]) + "). Queue size: " + std::to_string(queuedActors.size()), __FUNCTION__);
}

void queueActor(std::string actorName, float Position[3])
{
	QueueActor queueActor;
	queueActor.Name = actorName;
	queueActor.PosX = Position[0];
	queueActor.PosY = Position[1];
	queueActor.PosZ = Position[2];

	queuedActors.push_back(queueActor);
	Logging::LoggerService::LogInformation("Queued actor '" + actorName + "' at (" + std::to_string(Position[0]) + ", " + std::to_string(Position[1]) + ", " + std::to_string(Position[2]) + "). Queue size: " + std::to_string(queuedActors.size()), __FUNCTION__);
}


// Fills instData with a copy of the game's own actor storage (captured from r7 of the last
// vanilla createActor_ call), patched with our position / name. Offsets are counted from the
// end because InstanceData is stored byte-reversed (read/written as big endian).
static void PrepareInstanceData(InstanceData& instData, const QueueActor& qAct)
{
	auto put = [&](int offsetFromEnd, const void* src, size_t size) {
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - offsetFromEnd], src, size);
	};

	put(15 * 4, &qAct.PosX, sizeof(float));
	put(16 * 4, &qAct.PosY, sizeof(float));
	put(17 * 4, &qAct.PosZ, sizeof(float));

	const float traverseDist = 0.f;
	const short traverseDistInt = 0;
	put(18 * 4, &traverseDist, sizeof(float));
	put(37 * 2, &traverseDistInt, sizeof(short));

	const int null = 0;
	put(11 * 4, &null, sizeof(int)); // mLinkData
	put(9 * 4, &null, sizeof(int));  // mData
	put(10 * 4, &null, sizeof(int)); // mProc
	put(7 * 4, &null, sizeof(int));
	put(3 * 4, &null, sizeof(int));
	put(14 * 4, &null, sizeof(int)); // HashId

	const int traverseDistFixer = 0x043B0000;
	const int traverseDistFixer2 = 0x00000016;
	put(2 * 4, &traverseDistFixer, sizeof(int));
	put(1 * 4, &traverseDistFixer2, sizeof(int));

	const int invalid = -1;
	put(12 * 4, &invalid, sizeof(int)); // mRevivalGameDataFlagHash
	put(13 * 4, &invalid, sizeof(int));
	put(8 * 4, &invalid, sizeof(int));

	// The storage is a ksys::map::Object (zeldaret/botw src/KingSystem/Map/mapObject.h; Wii U layout: a field
	// at guest offset g is written with put(g + size)). Fields the copy still took from the game's last spawned
	// actor tied our player to that actor's map data, and the game sometimes deleted it within a second
	// (copied from 'SitRemainsWind_Far', a far-LOD static object). Reset them to "standalone actor" values.
	const int idAndNoCompound = 0x0000FFFF; // 0x0C mId = 0, 0x0E mStaticCompoundActorId = -1 (not in a static compound)
	put(0x0C + 4, &idAndNoCompound, sizeof(int));
	put(0x10 + 4, &null, sizeof(int));      // 0x10 _10 (pointer into the source actor's map data)
	put(0x14 + 4, &null, sizeof(int));      // 0x14 _18 (pointer into the source actor's map data)
	put(0x48 + 4, &null, sizeof(int));      // 0x48 mTraverseDistInt = 0, 0x4A mHardModeFlags = 0 (not master-mode / one-hit only)

	// Name, stored reversed and null terminated
	int pos = sizeof(instData.name) - 1;
	for (char c : qAct.Name)
	{
		if (pos <= 0) break;
		instData.name[pos--] = c;
	}
	instData.name[pos] = 0;
}

void setupActor(PPCInterpreter_t* hCPU, TransferableData& trnsData, InstanceData& instData, uint32_t startRingBuffer, uint32_t endRingBuffer, uint64_t baseAddress) {
	if (queuedActors.empty())
		return;

	// Wait until the game has called createActor_ itself in this stage so we have valid params.
	if (trnsData.f_r3 == 0 || trnsData.f_r7 == 0)
		return;

	QueueActor qAct = queuedActors[0];

	// Diagnostics: which game actor the copied placement data comes from. A fresh capture still has the
	// game's own name pointer in r4; after our first spawn r4 points into our ring buffer.
	{
		static std::string s_templateName = "?";
		if (trnsData.f_r4 != 0 && (trnsData.f_r4 < (int)startRingBuffer || trnsData.f_r4 >= (int)endRingBuffer))
		{
			std::string n = Memory::read_string(baseAddress + (uint32_t)trnsData.f_r4, 48, __FUNCTION__);
			if (!n.empty()) s_templateName = n;
		}
		Logging::LoggerService::LogInformation("Spawn template: placement data copied from '" + s_templateName + "'", __FUNCTION__);
	}

	// Copy the game's actor storage into our own slot so we don't modify the game's data
	data_mutex.lock_shared();
	memInstance->memory_readMemoryBE(trnsData.f_r7, &instData.actorStorage, baseAddress);
	data_mutex.unlock_shared();

	PrepareInstanceData(instData, qAct);

	int actorStorageLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name) - sizeof(instData.actorStorage);
	int mubinLocation = actorStorageLocation + (7 * 4); // The MubinIter lives inside the actor
	int nameLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name);

	hCPU->gpr[3] = trnsData.f_r3;
	hCPU->gpr[4] = nameLocation;
	hCPU->gpr[5] = trnsData.f_r5 ? trnsData.f_r5 : 0xFFFFFFFF;
	hCPU->gpr[6] = mubinLocation;
	hCPU->gpr[7] = actorStorageLocation;
	hCPU->gpr[8] = 0;
	hCPU->gpr[9] = 1;
	hCPU->gpr[10] = 0;

	trnsData.f_r4 = nameLocation;
	trnsData.f_r5 = trnsData.f_r5 ? trnsData.f_r5 : 0xFFFFFFFF;
	trnsData.f_r6 = mubinLocation;
	trnsData.f_r7 = actorStorageLocation;
	trnsData.f_r8 = 0;
	trnsData.f_r9 = 1;
	trnsData.f_r10 = 0;
	trnsData.fnAddr = 0x037b6040; // ksys::act::ActorCreator::createActor_
	trnsData.enabled = true;              // asm patch performs exactly one call
	trnsData.interceptRegisters = false;  // don't capture our own call

	std::stringstream ss;
	ss << "setupActor: Invoking createActor_ for '" << qAct.Name << "' at ("
	   << qAct.PosX << ", " << qAct.PosY << ", " << qAct.PosZ << ") ringPtr=0x"
	   << std::hex << trnsData.ringPtr;
	Logging::LoggerService::LogInformation(ss.str(), __FUNCTION__);

	data_mutex.lock();
	memInstance->memory_writeMemoryBE(trnsData.ringPtr, instData, baseAddress);
	data_mutex.unlock();

	trnsData.ringPtr += sizeof(InstanceData);
	if (trnsData.ringPtr >= (int)endRingBuffer)
		trnsData.ringPtr = startRingBuffer;

	queuedActors.erase(queuedActors.begin());
}

void mainFn(PPCInterpreter_t* hCPU, uint32_t startTrnsData, uint32_t startRingBuffer, uint32_t endRingBuffer, uint64_t baseAddress) {
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (baseAddress == 0 || !memInstance || startTrnsData < 0x1000)
		return;

	if (!Memory::IsStageReady() || !Game::GameInstance || Game::GameInstance->baseAddr == 0)
		return;

	if (Main::IsStageUnstable())
		return; // stage is being torn down / reloaded: never call createActor_ now

	s_startTrnsDataGuestAddr = startTrnsData;
	s_storageStartGuestAddr = startRingBuffer;
	s_storageEndGuestAddr = endRingBuffer;

	if (!isSetup) {
		setup(hCPU, startTrnsData, baseAddress);
		return;
	}

	data_mutex.lock_shared();
	TransferableData trnsData;
	memInstance->memory_readMemoryBE(startTrnsData, &trnsData, baseAddress);

	uint32_t startInstData = trnsData.ringPtr;
	InstanceData instData;
	memInstance->memory_readMemoryBE(startInstData, &instData, baseAddress);
	data_mutex.unlock_shared();

	trnsData.interceptRegisters = true;

	if (s_needFreshCapture)
	{
		s_needFreshCapture = false;
		if (s_keepBackupOnNextReset && trnsData.f_r3 != 0 && trnsData.f_r7 != 0)
		{
			s_haveCaptureBackup = true;
			s_backupR3 = trnsData.f_r3; s_backupR5 = trnsData.f_r5; s_backupR7 = trnsData.f_r7;
		}
		else if (!s_keepBackupOnNextReset)
			s_haveCaptureBackup = false;
		s_keepBackupOnNextReset = false;
		s_captureWaitSince = GetTickCount();
		trnsData.f_r3 = trnsData.f_r4 = trnsData.f_r5 = trnsData.f_r6 = 0;
		trnsData.f_r7 = trnsData.f_r8 = trnsData.f_r9 = trnsData.f_r10 = 0;
	}
	else
	{
		if ((trnsData.f_r3 == 0 || trnsData.f_r7 == 0) && s_haveCaptureBackup &&
			GetTickCount() - s_captureWaitSince > 15000 && !queuedActors.empty())
		{
			trnsData.f_r3 = s_backupR3; trnsData.f_r5 = s_backupR5; trnsData.f_r7 = s_backupR7;
			s_haveCaptureBackup = false;
			Logging::LoggerService::LogInformation("No fresh createActor_ capture for 15 s on an unchanged stage; reusing the previous one.", __FUNCTION__);
		}
		else if (trnsData.f_r3 != 0 && trnsData.f_r7 != 0)
			s_haveCaptureBackup = false; // fresh capture arrived

		queue_mutex.lock();
		setupActor(hCPU, trnsData, instData, startRingBuffer, endRingBuffer, baseAddress);
		queue_mutex.unlock();
	}

	data_mutex.lock();
	memInstance->memory_writeMemoryBE(startTrnsData, trnsData, baseAddress);
	data_mutex.unlock();
}

void mainFn(PPCInterpreter_t* hCPU) {
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;
	static bool s_loggedMainFn = false;
	if (!s_loggedMainFn) {
		s_loggedMainFn = true;
		Logging::LoggerService::LogInformation("mainFn HLE invoked by Cemu.");
	}
	if (Main::baseAddr == 0) return;
	mainFn(hCPU, hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5], Main::baseAddr);
}

enum Weather : uint32_t
{
	BlueSky,
	Cloudy,
	Rain,
	HeavyRain,
	Snow,
	HeavySnow,
	Thunderstorm,
	ThunderRain,
	BlueSkyRain
};

Weather newWeather = (Weather)0;

void WeatherFn(PPCInterpreter_t* hCPU) {
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;
	static bool s_loggedWeather = false;
	if (!s_loggedWeather) {
		s_loggedWeather = true;
		Logging::LoggerService::LogInformation("WeatherFn HLE invoked by Cemu.");
	}

	if (Main::baseAddr == 0 || !memInstance)
		return;

	if (Game::GameInstance && Game::GameInstance->World)
	{
		Game::GameInstance->World->LocalWeather = (int)(Weather)hCPU->gpr[26];
		newWeather = (Weather)Game::GameInstance->World->Weather;

		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 0, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 1, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 2, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 3, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 4, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 5, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 6, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 7, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 8, (char)newWeather, Main::baseAddr);
	}
}

int LastCreated = 0;
bool started = false;

void OnActorCreate(PPCInterpreter_t* hCPU)
{
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (Main::baseAddr == 0)
		Main::baseAddr = Memory::getBaseAddress();

	if (Main::baseAddr == 0 || hCPU->gpr[3] < 0x1000)
		return;

	std::string name = Memory::read_string(Main::baseAddr + hCPU->gpr[3] + 0x10, 100, __FUNCTION__);
	if (name.empty())
		return;

	static int s_actorLogCount = 0;
	if (s_actorLogCount < 25 || name.rfind("GameROMPlayer") != std::string::npos || name.rfind("Jugador", 0) == 0)
	{
		s_actorLogCount++;
		Logging::LoggerService::LogInformation("OnActorCreate: actor '" + name + "' at 0x" + std::to_string(hCPU->gpr[3]));
	}

	std::vector<std::string> BombChoices = { "CustomRemoteBomb", "CustomRemoteBomb2", "CustomRemoteBombCube", "CustomRemoteBombCube2" };

	if (name.rfind("Enemy_Bokoblin_Junior") != std::string::npos)
	{
		std::stringstream stream;
		stream << "Bokoblin: " << std::hex << hCPU->gpr[3];
		Logging::LoggerService::LogDebug(stream.str());
	}

	if (name.rfind("GameROMPlayer") != std::string::npos)
	{
		std::stringstream stream;
		stream << "Link detected at: 0x" << std::hex << hCPU->gpr[3];
		Logging::LoggerService::LogInformation(stream.str());
		Main::s_LocalLinkAddr = hCPU->gpr[3];
		if (Game::GameInstance)
		{
			Game::GameInstance->setAddress(hCPU->gpr[3]);
			if (started)
			{
				try
				{
					std::map<int, MemoryAccess::LocalInstance::FlagAddresses> Flags = Game::GameInstance->scanPlayerFlags();
					for (auto& kv : Flags)
					{
						if (Instances::PlayerList.count(kv.first) && Instances::PlayerList[kv.first])
						{
							Instances::PlayerList[kv.first]->UpdateFlags(kv.second);
						}
					}
					Logging::LoggerService::LogInformation("Re-scanned player flags after Link re-creation.", __FUNCTION__);
				}
				catch (...) {}
			}
		}
	}

	if (name.rfind("Jugador", 0) == 0)
	{
		try
		{
			int spawnedPlayer = std::stoi(name.substr(7));

			std::stringstream stream;
			stream << "Spawned player " << spawnedPlayer << " at: " << std::hex << Main::baseAddr + hCPU->gpr[3] << ". Setting up addresses.";
			Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);

			if (Instances::PlayerList.find(spawnedPlayer) != Instances::PlayerList.end() && Instances::PlayerList[spawnedPlayer])
			{
				Instances::PlayerList[spawnedPlayer]->setAddress(hCPU->gpr[3]);
				Instances::PlayerList[spawnedPlayer]->SpawnedAt = GetTickCount();
				if (Instances::PlayerList[spawnedPlayer]->Equipment)
					Instances::PlayerList[spawnedPlayer]->Equipment->SetWeapons(hCPU->gpr[3]);
				if (Instances::PlayerList[spawnedPlayer]->Bumii)
					Instances::PlayerList[spawnedPlayer]->Bumii->setAddress(hCPU->gpr[3]);

				Logging::LoggerService::LogInformation("Player " + std::to_string(spawnedPlayer) + " setup correctly.", __FUNCTION__);
			}
		}
		catch (...) {}
	}

	if (name.rfind("Enemy_", 0) == 0)
	{
		if (Game::GameInstance && Game::GameInstance->EnemyService)
			Game::GameInstance->EnemyService->UpdateEnemyAddress(Main::baseAddr + hCPU->gpr[3], true);
	}

	if (!started || !Game::GameInstance) return;

	if (name == "RemoteBomb" && Game::GameInstance->Bomb)
	{
		Game::GameInstance->Bomb->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->Bomb->changeState(Normal);
	}
	else if (name == "RemoteBomb2" && Game::GameInstance->Bomb2)
	{
		Game::GameInstance->Bomb2->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->Bomb2->changeState(Normal);
	}
	else if (name == "RemoteBombCube" && Game::GameInstance->BombCube)
	{
		Game::GameInstance->BombCube->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->BombCube->changeState(Normal);
	}
	else if (name == "RemoteBombCube2" && Game::GameInstance->BombCube2)
	{
		Game::GameInstance->BombCube2->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->BombCube2->changeState(Normal);
	}

	int BombType = -1;
	for (int i = 0; i < 4; i++)
	{
		if (name == BombChoices[i])
			BombType = i;
	}

	if (BombType == -1)
		return;

	for (int j = 1; j < 33; j++)
	{
		if (Instances::PlayerList.find(j) == Instances::PlayerList.end() || !Instances::PlayerList[j] || !Instances::PlayerList[j]->connected)
			continue;

		BombAccess* Bomb = nullptr;
		if (BombType == 0)
			Bomb = Instances::PlayerList[j]->Bomb;
		else if (BombType == 1)
			Bomb = Instances::PlayerList[j]->Bomb2;
		else if (BombType == 2)
			Bomb = Instances::PlayerList[j]->BombCube;
		else if (BombType == 3)
			Bomb = Instances::PlayerList[j]->BombCube2;

		if (Bomb && Bomb->getStatus() == Processing)
		{
			Bomb->setAddress(hCPU->gpr[3], __FUNCTION__);
			Bomb->changeState(Normal);
			std::stringstream stream;
			stream << "Assigned " << name << " to player " << std::to_string(j) << " at: " << std::hex << hCPU->gpr[3];
			Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
			return;
		}
	}
}

// Local Link was erased (stage unload, fast travel, death, save reload): every remote actor and
// every pending spawn belongs to the old stage.
static void ResetSpawnStateForStageChange()
{
	Main::s_LocalLinkAddr = 0;
	if (Game::GameInstance)
	{
		Game::GameInstance->setAddress(0);
		Game::GameInstance->ClearQueue();
	}
	{
		std::unique_lock<std::shared_mutex> qLock(queue_mutex);
		queuedActors.clear();
	}
	Main::MarkStageUnstable(8000);
	for (auto& pair : Instances::PlayerList)
	{
		if (pair.second)
		{
			pair.second->setAddress(0);
			if (pair.second->Status && pair.second->Status->isAddressSet())
				pair.second->Status->set(0, __FUNCTION__);
		}
	}
}

void OnActorErase(PPCInterpreter_t* hCPU)
{
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (Main::baseAddr == 0)
		Main::baseAddr = Memory::getBaseAddress();

	if (Main::baseAddr == 0 || hCPU->gpr[3] < 0x1000)
		return;

	std::string name = Memory::read_string(Main::baseAddr + hCPU->gpr[3] + 0x10, 100, __FUNCTION__);
	if (name.empty())
		return;

	if (name.rfind("GameROMPlayer") != std::string::npos)
	{
		Logging::LoggerService::LogInformation("Link actor erased.", __FUNCTION__);
		ResetSpawnStateForStageChange();
	}

	std::vector<std::string> BombChoices = {"CustomRemoteBomb", "CustomRemoteBomb2", "CustomRemoteBombCube", "CustomRemoteBombCube2"};

	if (name.rfind("Jugador", 0) == 0) {
		try
		{
			int spawnedPlayer = std::stoi(name.substr(7));
			if (Instances::PlayerList.find(spawnedPlayer) != Instances::PlayerList.end() && Instances::PlayerList[spawnedPlayer])
			{
				// A remote player we didn't ask to delete is being erased: the stage is unloading.
				if (!Instances::PlayerList[spawnedPlayer]->DeleteRequested && Instances::PlayerList[spawnedPlayer]->baseAddr != 0)
				{
					Logging::LoggerService::LogInformation("Player " + std::to_string(spawnedPlayer) + " erased by the game: stage change, pausing spawns.", __FUNCTION__);
					Instances::PlayerList[spawnedPlayer]->CheckInstantDeletion();
					Main::MarkStageUnstable(8000, true);
				}
				Instances::PlayerList[spawnedPlayer]->DeleteRequested = false;
				if (Instances::PlayerList[spawnedPlayer]->Status)
					Instances::PlayerList[spawnedPlayer]->Status->set(0, __FUNCTION__);
				Instances::PlayerList[spawnedPlayer]->setAddress(0);
				std::stringstream stream;
				stream << "Player " << spawnedPlayer << " erased.";
				Logging::LoggerService::LogDebug(stream.str(), __FUNCTION__);
			}
		}
		catch (...) {}
	}

	if (name.rfind("Enemy_", 0) == 0)
	{
		if (Game::GameInstance && Game::GameInstance->EnemyService)
			Game::GameInstance->EnemyService->UpdateEnemyAddress(Main::baseAddr + hCPU->gpr[3], false);
	}

	if (!started || !Game::GameInstance) return;

	if (name == "RemoteBomb" && Game::GameInstance->Bomb)
	{
		Game::GameInstance->Bomb->setAddress(0, __FUNCTION__);
		Game::GameInstance->Bomb->changeState(Cancelled);
	}
	else if (name == "RemoteBomb2" && Game::GameInstance->Bomb2)
	{
		Game::GameInstance->Bomb2->setAddress(0, __FUNCTION__);
		Game::GameInstance->Bomb2->changeState(Cancelled);
	}
	else if (name == "RemoteBombCube" && Game::GameInstance->BombCube)
	{
		Game::GameInstance->BombCube->setAddress(0, __FUNCTION__);
		Game::GameInstance->BombCube->changeState(Cancelled);
	}
	else if (name == "RemoteBombCube2" && Game::GameInstance->BombCube2)
	{
		Game::GameInstance->BombCube2->setAddress(0, __FUNCTION__);
		Game::GameInstance->BombCube2->changeState(Cancelled);
	}

	int BombType = -1;
	for (int i = 0; i < 4; i++)
	{
		if (name == BombChoices[i])
			BombType = i;
	}

	if (BombType == -1)
		return;

	for (int j = 1; j < 33; j++)
	{
		if (Instances::PlayerList.find(j) == Instances::PlayerList.end() || !Instances::PlayerList[j] || !Instances::PlayerList[j]->connected)
			continue;

		BombAccess* Bomb = nullptr;
		if (BombType == 0)
			Bomb = Instances::PlayerList[j]->Bomb;
		else if (BombType == 1)
			Bomb = Instances::PlayerList[j]->Bomb2;
		else if (BombType == 2)
			Bomb = Instances::PlayerList[j]->BombCube;
		else if (BombType == 3)
			Bomb = Instances::PlayerList[j]->BombCube2;

		if (Bomb && Bomb->BaseAddr == hCPU->gpr[3])
		{
			Bomb->setAddress(0, __FUNCTION__);
			Bomb->changeState(Deallocated);
			std::stringstream stream;
			stream << "Bomb " << name << " of player " << std::to_string(j) << " despawned.";
			Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
			return;
		}
	}
}

void remoteBomb_onAICalc(PPCInterpreter_t* hCPU)
{
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (Main::baseAddr == 0 || !memInstance || hCPU->gpr[3] < 0x1000)
		return;

	unsigned int aiPtr = hCPU->gpr[3];
	unsigned int actPtr = 0;
	std::vector<std::string> BombChoices = { "CustomRemoteBomb", "CustomRemoteBomb2", "CustomRemoteBombCube", "CustomRemoteBombCube2" };

	memInstance->memory_readMemoryBE(aiPtr, &actPtr, Main::baseAddr);
	if (actPtr == 0) return;

	std::string name = Memory::read_string(Main::baseAddr + actPtr + 0x10, 50, __FUNCTION__);

	auto b1 = Memory::read_bytes(aiPtr + 0x78 + Main::baseAddr, 1, __FUNCTION__);
	auto b2 = Memory::read_bytes(aiPtr + 0x9C + Main::baseAddr, 1, __FUNCTION__);
	bool explode = (!b1.empty() && b1[0] == 0x1);
	bool cancel = (!b2.empty() && b2[0] == 0x1);

	if ((explode || cancel) && Game::GameInstance)
	{
		if (name == "RemoteBomb" && Game::GameInstance->Bomb)
		{
			Game::GameInstance->Bomb->setAddress(0, __FUNCTION__);
			Game::GameInstance->Bomb->changeState(explode ? Exploded : Cancelled);
		}
		else if (name == "RemoteBomb2" && Game::GameInstance->Bomb2)
		{
			Game::GameInstance->Bomb2->setAddress(0, __FUNCTION__);
			Game::GameInstance->Bomb2->changeState(explode ? Exploded : Cancelled);
		}
		else if (name == "RemoteBombCube" && Game::GameInstance->BombCube)
		{
			Game::GameInstance->BombCube->setAddress(0, __FUNCTION__);
			Game::GameInstance->BombCube->changeState(explode ? Exploded : Cancelled);
		}
		else if (name == "RemoteBombCube2" && Game::GameInstance->BombCube2)
		{
			Game::GameInstance->BombCube2->setAddress(0, __FUNCTION__);
			Game::GameInstance->BombCube2->changeState(explode ? Exploded : Cancelled);
		}
	}

	int BombType = -1;
	for (int i = 0; i < 4; i++)
	{
		if (name == BombChoices[i])
			BombType = i;
	}

	if (BombType == -1)
		return;

	for (int j = 1; j < 33; j++)
	{
		if (Instances::PlayerList.find(j) == Instances::PlayerList.end() || !Instances::PlayerList[j] || !Instances::PlayerList[j]->connected)
			continue;

		BombAccess* Bomb = nullptr;
		if (BombType == 0)
			Bomb = Instances::PlayerList[j]->Bomb;
		else if (BombType == 1)
			Bomb = Instances::PlayerList[j]->Bomb2;
		else if (BombType == 2)
			Bomb = Instances::PlayerList[j]->BombCube;
		else if (BombType == 3)
			Bomb = Instances::PlayerList[j]->BombCube2;

		if (!Bomb || Bomb->getStatus() == Deallocated)
			continue;

		if (Bomb->BaseAddr == actPtr)
		{
			BombStatus status = Bomb->getStatus();
			if (status == Exploded)
			{
				Memory::write_byte(aiPtr + 0x78 + Main::baseAddr, 0x1, __FUNCTION__);
				Bomb->changeState(Deallocated);
				std::stringstream stream;
				stream << "Bomb " << name << " of player " << std::to_string(j) << " exploded.";
				Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
				Bomb->setAddress(0, __FUNCTION__);
				return;
			}
			else if (status == Cancelled)
			{
				Memory::write_byte(aiPtr + 0x9C + Main::baseAddr, 0x1, __FUNCTION__);
				Bomb->changeState(Deallocated);
				std::stringstream stream;
				stream << "Bomb " << name << " of player " << std::to_string(j) << " erased.";
				Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
				Bomb->setAddress(0, __FUNCTION__);
				return;
			}
		}
	}
}

void timemgr_OnInit(PPCInterpreter_t* hCPU)
{
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (Main::baseAddr == 0 || !Game::GameInstance || !Game::GameInstance->World)
		return;

	Game::GameInstance->World->ReadableTime = new BigEndian<float>(hCPU->gpr[3] + 0x98 + Main::baseAddr, __FUNCTION__);
	Game::GameInstance->World->WritableTime = new BigEndian<float>(hCPU->gpr[3] + 0xA0 + Main::baseAddr, __FUNCTION__);
	Logging::LoggerService::LogInformation("timemgr_OnInit: Bound ReadableTime and WritableTime.");
}

void dummySelfTestFn(PPCInterpreter_t* hCPU)
{
	if (hCPU && hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;
}

void init() {
	static bool s_initialized = false;
	if (s_initialized) return;

	HMODULE hCemu = GetModuleHandleA(NULL);
	if (!hCemu) hCemu = GetModuleHandleA("Cemu.exe");
	if (!hCemu)
	{
		Logging::LoggerService::LogError("init(): GetModuleHandle Cemu.exe failed!");
		return;
	}

	osLib_registerHLEFunctionType osLib_registerHLEFunction = (osLib_registerHLEFunctionType)GetProcAddress(hCemu, "osLib_registerHLEFunction");
	if (!osLib_registerHLEFunction)
	{
		Logging::LoggerService::LogError("init(): GetProcAddress osLib_registerHLEFunction failed!");
		return;
	}

	// Self-test: register dummy function to verify index lookup offset (+0x10) per Claude Spec §2.3 #5
	osLib_registerHLEFunction("selftest", "dummy", static_cast<void (*) (PPCInterpreter_t*)>(&dummySelfTestFn));
	osLib_getHLEFunctionAddressType osLib_getHLEFunctionAddress = (osLib_getHLEFunctionAddressType)((char*)osLib_registerHLEFunction + 0x10);
	uint32_t dummyIdx = osLib_getHLEFunctionAddress("selftest", "dummy");
	if (dummyIdx == 0 || dummyIdx > 0xFFFF)
	{
		std::stringstream ss;
		ss << "init(): Self-test FAILED! Lookup returned invalid index 0x" << std::hex << dummyIdx
		   << ". Offset +0x10 is invalid for this Cemu build!";
		Logging::LoggerService::LogError(ss.str());
		return;
	}
	Logging::LoggerService::LogInformation("init(): Self-test PASSED (dummyIdx=0x" + std::to_string(dummyIdx) + "). HLE offset verified.");

	osLib_registerHLEFunction("spawnactors", "fnCallMain", static_cast<void (*) (PPCInterpreter_t*)>(&mainFn));
	osLib_registerHLEFunction("multiplayer", "WeatherSync", static_cast<void (*) (PPCInterpreter_t*)>(&WeatherFn));
	osLib_registerHLEFunction("ukl_actorinterceptor", "OnActorCreate", static_cast<void (*) (PPCInterpreter_t*)>(&OnActorCreate));
	osLib_registerHLEFunction("ukl_actorinterceptor", "OnActorDeleteLater", static_cast<void (*) (PPCInterpreter_t*)>(&OnActorErase));
	osLib_registerHLEFunction("ukl_actorinterceptor", "OnActorErase", static_cast<void (*) (PPCInterpreter_t*)>(&OnActorErase));
	osLib_registerHLEFunction("ukl_remotebombaiinterceptor", "OnCalc", &remoteBomb_onAICalc);
	osLib_registerHLEFunction("ukl_timemgrinterceptor", "OnInit", &timemgr_OnInit);

	s_initialized = true;
	Logging::LoggerService::LogInformation("init(): All HLE interceptors registered successfully.");
}

// memcmp that tolerates unmapped guest pages in the codecave scan range.
static bool SafeMatch(const uint8_t* p, const char* str, size_t len)
{
	__try { return memcmp(p, str, len) == 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void BindSlot(uint64_t slotHost, uint32_t idx, const char* name)
{
	if (idx == 0 || idx > 0xFFFF) return; // rejects 0, > 0xFFFF, and 0xFFFFFFFF (unregistered)
	int op = (int)(0x04000000u | idx);
	int current = Memory::read_bigEndian4Bytes(slotHost, __FUNCTION__);
	if (current != op)
	{
		Memory::write_bigEndian4Bytes(slotHost, op, __FUNCTION__);
		int readback = Memory::read_bigEndian4Bytes(slotHost, __FUNCTION__);
		std::stringstream ss;
		ss << "BindSlot: " << name << " at 0x" << std::hex << slotHost
			<< " (idx=0x" << idx << ") wrote op=0x" << op
			<< ", readback=0x" << readback
			<< (readback == op ? " (VERIFIED MATCH)" : " (MISMATCH!)");
		Logging::LoggerService::LogInformation(ss.str());
	}
}

void BindHLEAddresses()
{
	if (Main::baseAddr == 0)
		return;

	HMODULE hCemu = GetModuleHandleA(NULL);
	if (!hCemu) hCemu = GetModuleHandleA("Cemu.exe");
	if (!hCemu) return;

	osLib_registerHLEFunctionType osLib_registerHLEFunction = (osLib_registerHLEFunctionType)GetProcAddress(hCemu, "osLib_registerHLEFunction");
	if (!osLib_registerHLEFunction) return;

	osLib_getHLEFunctionAddressType osLib_getHLEFunctionAddress = (osLib_getHLEFunctionAddressType)((char*)osLib_registerHLEFunction + 0x10);

	uint32_t addr_OnActorCreate = osLib_getHLEFunctionAddress("ukl_actorinterceptor", "OnActorCreate");
	uint32_t addr_OnActorDeleteLater = osLib_getHLEFunctionAddress("ukl_actorinterceptor", "OnActorDeleteLater");
	uint32_t addr_OnActorErase = osLib_getHLEFunctionAddress("ukl_actorinterceptor", "OnActorErase");
	uint32_t addr_WeatherSync = osLib_getHLEFunctionAddress("multiplayer", "WeatherSync");
	uint32_t addr_fnCallMain = osLib_getHLEFunctionAddress("spawnactors", "fnCallMain");
	uint32_t addr_RemoteBomb = osLib_getHLEFunctionAddress("ukl_remotebombaiinterceptor", "OnCalc");
	uint32_t addr_TimeMgr = osLib_getHLEFunctionAddress("ukl_timemgrinterceptor", "OnInit");

	// Safety check: ensure lookup returned a sane HLE table index
	if (addr_OnActorCreate == 0 || addr_OnActorCreate > 0xFFFF)
	{
		static bool s_loggedLookupError = false;
		if (!s_loggedLookupError)
		{
			s_loggedLookupError = true;
			std::stringstream ss;
			ss << "BindHLEAddresses: osLib_getHLEFunctionAddress lookup invalid (OnActorCreate=0x"
				<< std::hex << addr_OnActorCreate << "). Aborting slot binding.";
			Logging::LoggerService::LogError(ss.str());
		}
		return;
	}

	static bool s_loggedAddresses = false;
	if (!s_loggedAddresses)
	{
		s_loggedAddresses = true;
		std::stringstream ss;
		ss << "BindHLEAddresses: OnActorCreate idx=0x" << std::hex << addr_OnActorCreate
			<< ", fnCallMain idx=0x" << addr_fnCallMain
			<< ", WeatherSync idx=0x" << addr_WeatherSync
			<< ", RemoteBomb idx=0x" << addr_RemoteBomb
			<< ", TimeMgr idx=0x" << addr_TimeMgr;
		Logging::LoggerService::LogInformation(ss.str());
	}

	// Pure dynamic scan of graphic pack codecaves in range [0x01800000, 0x01820000]
	// NEVER write to hardcoded fallback offsets, which corrupt adjacent modules!
	uint8_t* pStart = (uint8_t*)(Main::baseAddr + 0x01800000);
	bool foundActor = false;
	bool foundSpawn = false;
	bool foundWeather = false;
	bool foundBomb = false;
	bool foundTime = false;

	const char targetStrActor[] = "ukl_actorinterceptor";
	const char targetStrSpawn[] = "spawnactors";
	const char targetStrWeather[] = "WeatherSync";
	const char targetStrBomb[] = "ukl_remotebombaiinterceptor";
	const char targetStrTime[] = "ukl_timemgrinterceptor";

	for (size_t offset = 0; offset < 0x20000; offset += 4)
	{
		if (!foundActor && SafeMatch(pStart + offset, targetStrActor, sizeof(targetStrActor) - 1))
		{
			uint64_t modBase = Main::baseAddr + 0x01800000 + offset;
			BindSlot(modBase + 0x2C, addr_OnActorCreate, "OnActorCreate (scan)");
			BindSlot(modBase + 0x44, addr_OnActorDeleteLater, "OnActorDeleteLater (scan)");
			BindSlot(modBase + 0x58, addr_OnActorErase, "OnActorErase (scan)");
			foundActor = true;
		}

		if (!foundSpawn && SafeMatch(pStart + offset, targetStrSpawn, sizeof(targetStrSpawn) - 1))
		{
			if (offset >= 0x14)
			{
				uint32_t spawnActorsGuestBase = 0x01800000 + (uint32_t)(offset - 0x14);
				uint64_t modBase = Main::baseAddr + spawnActorsGuestBase;
				BindSlot(modBase, addr_fnCallMain, "fnCallMain (scan)");
				s_storageStartGuestAddr = spawnActorsGuestBase + 0x20;
				s_storageEndGuestAddr = spawnActorsGuestBase + 0x8020;
				s_startTrnsDataGuestAddr = spawnActorsGuestBase + 0x8020;

				// Enable register interception immediately so createActor_ captures r3..r10 on the very first actor created!
				Memory::write_byte(Main::baseAddr + s_startTrnsDataGuestAddr + 1, 1, "BindHLEAddresses::InterceptRegisters");
				foundSpawn = true;
			}
		}

		if (!foundWeather && SafeMatch(pStart + offset, targetStrWeather, sizeof(targetStrWeather) - 1))
		{
			if (offset >= 4)
			{
				uint64_t modBase = Main::baseAddr + 0x01800000 + offset - 4;
				BindSlot(modBase, addr_WeatherSync, "WeatherSync (scan)");
				foundWeather = true;
			}
		}

		if (!foundBomb && SafeMatch(pStart + offset, targetStrBomb, sizeof(targetStrBomb) - 1))
		{
			uint64_t modBase = Main::baseAddr + 0x01800000 + offset;
			BindSlot(modBase + 0x28, addr_RemoteBomb, "RemoteBomb (scan)");
			foundBomb = true;
		}

		if (!foundTime && SafeMatch(pStart + offset, targetStrTime, sizeof(targetStrTime) - 1))
		{
			uint64_t modBase = Main::baseAddr + 0x01800000 + offset;
			BindSlot(modBase + 0x24, addr_TimeMgr, "TimeMgr (scan)");
			foundTime = true;
		}

		if (foundActor && foundSpawn && foundWeather && foundBomb && foundTime)
			break;
	}

	// Log once the result changes (the first calls usually run before the codecaves are mapped).
	static int s_lastScanMask = -1;
	int scanMask = (foundActor << 0) | (foundSpawn << 1) | (foundWeather << 2) | (foundBomb << 3) | (foundTime << 4);
	if (scanMask != s_lastScanMask)
	{
		s_lastScanMask = scanMask;
		std::stringstream ss;
		ss << "BindHLEAddresses Scan Result: Actor=" << (foundActor ? "OK" : "MISSING")
		   << ", Spawn=" << (foundSpawn ? "OK" : "MISSING")
		   << ", Weather=" << (foundWeather ? "OK" : "MISSING")
		   << ", Bomb=" << (foundBomb ? "OK" : "MISSING")
		   << ", Time=" << (foundTime ? "OK" : "MISSING");
		if (foundSpawn)
		{
			ss << " | storageStart=0x" << std::hex << s_storageStartGuestAddr
			   << ", trnsData=0x" << s_startTrnsDataGuestAddr;
		}
		Logging::LoggerService::LogInformation(ss.str());
	}
}

